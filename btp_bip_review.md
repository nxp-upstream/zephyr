# Zephyr Tester BIP 模块（BTP_SERVICE_ID_BIP）代码评审

**评审对象**
- `tests/bluetooth/tester/src/btp_bip.c`（2786 行）
- `tests/bluetooth/tester/src/btp/btp_bip.h`（1109 行）

**对照基准**：同目录 `btp_l2cap.c` / `btp_sdp.c` / `btp_rfcomm.c` 的既有约定，以及 `btp.c` 的框架契约

**规模**：命令 71 个（已注册 handler 71 条）、事件 64 个、实例池 `CONFIG_BT_MAX_CONN`（tester 默认 = 3）

**仓库信息**：`C:\_DDM\zephyr\zephyrproject_2\zephyr`，分支 `feature/bip_pts`，HEAD `f47bb53f1f1`

---

## 一、总体结论

架构方向是对的：`bip_app` 按对端地址多实例化、primary/secondary 各自独立 `bt_bip`/`server`/`client`、请求与响应用宏批量生成、每个非平凡的路由决策都写了长注释。这套设计足以覆盖 BIP 的 6 个 primary 角色 + 2 个 secondary 角色（Referenced/Archived Objects），包括最难的 Auto-Archive 双 OBEX 会话共享单一 PSM 的场景。

但代码处于"调试驱动"阶段：**存在 1 处线格式（wire format）与头文件结构体不一致的确定性 bug、2 处可越界/溢出的输入校验缺失、1 处能力位图漏报 17 个命令、1 处实例泄漏（3 次注册/注销后功能失效）**。此外 primary/secondary 的对称性只做了 70%，缺 secondary 传输断开命令和 secondary 传输事件区分。

---

## 二、优点

| # | 优点 | 位置 |
|---|---|---|
| 1 | **按地址多实例 + 预注册实例生命周期分离**。`find_preregistered_instance_by_address()` 让"peer 连接前先注册 server"这一 BIP 必需流程成立，`bip_instance_release_transport()` 让预注册实例跨传输断开存活，正确支持"断开 primary 传输后用另一个 PSM 连 secondary server"。 | `btp_bip.c:271-350` |
| 2 | **secondary 使用独立 `bt_bip`（`second_bip`）而非复用 `bip`**。这是本模块最关键的正确决策：单个 `bt_bip` 只承载一条传输，且 role 会被 primary 的 connect 固化为 INITIATOR，复用会导致 secondary connect 被 host 以 "Invalid role" 拒绝。 | `btp_bip.c:38-46, 2141-2154` |
| 3 | **Auto-Archive 单 PSM 双会话的路由**。`archive_*_accept()` 用"secondary server 是否已注册"作为判据在 primary/secondary 之间分流，避免 OBEX target 匹配失败返回 0xC4。这是纯经验性的正确解法。 | `btp_bip.c:1229-1265` |
| 4 | **secondary 回调不复用 primary 回调**。`inst_from_second_server()` / `inst_from_second_client()` 的区分是必须的（`CONTAINER_OF` 基于不同字段偏移），注释也明确说明了复用会算出错误的 `bip_app` 指针。 | `btp_bip.c:862-874, 984-1001` |
| 5 | **secondary 请求/响应使用独立 opcode**（`BTP_BIP_EV_SECOND_SERVER_GET_*_REQ` / `BTP_BIP_SECOND_*_RSP`）。让上位机能区分请求落在哪条 OBEX 连接上，并把响应路由回正确的 server，避免 "Invalid state"。 | `btp_bip.h:967-1105` |
| 6 | **宏化消除重复**。`BIP_SERVER_REQ_CB` / `BIP_CLIENT_RSP_CB` / `BIP_CLIENT_OP_HANDLER` / `BIP_SERVER_RSP_HANDLER` / `BIP_SECOND_*` 五组宏把 ~60 个近似函数压缩到可维护规模。 | `btp_bip.c:670, 768, 1886, 1993, 2283` |
| 7 | **PDU 从正确的 GOEP 传输分配**。`alloc_buf_with_data_bip(bip, ...)` 强制调用方显式传 `&inst->bip` 或 `&inst->second_bip`，并在 `net_buf_tailroom()` 不足时失败而非溢出。 | `btp_bip.c:1275-1294` |
| 8 | **UUID 复合字面量提升到文件作用域**，附注释说明函数内使用会成为悬垂指针导致 server 以全零 UUID 注册。这类坑记录下来很有价值。 | `btp_bip.c:2046-2056` |
| 9 | **三条 SDP 记录的属性集符合 BIP 规范分工**：Responder 记录带 SupportedCapabilities/Features/Functions/TotalImagingDataCapacity，Archive/RefObj 记录只带 SupportedFunctions + GoepL2capPsm；三个 GOEP L2CAP PSM(0x1009/0x100b/0x100d) 均为合法奇数动态 PSM。 | `btp_bip.c:120-250` |
| 10 | **透传式参数设计**。命令/事件统一用 `{address, final\|rsp_code, data_len, data[]}`，`data` 直接是 OBEX header 原始字节，上位机可构造任意（含非法）header——对 PTS 测试是正确取向。 | `btp_bip.h` 全篇 |

---

## 三、问题清单

### P0 — 确定性缺陷，必须修

#### B1. 4 个事件的线格式与头文件结构体不一致（多 1 字节）

`send_server_event()` 无条件写入 `final` 字节：

```c
/* btp_bip.c:556-587 */
ev_len = sizeof(bt_addr_le_t) + sizeof(uint8_t) + sizeof(uint16_t) + data_len;  /* 7+1+2 */
...
ev_data[off++] = final;
```

但 disconnect/abort 类事件的结构体**没有** `final` 字段：

```c
/* btp_bip.h:609-621 */
struct btp_bip_server_disconnect_req_ev {
	bt_addr_le_t address;   /* 7 */
	uint16_t data_len;      /* 2  → sizeof = 9 */
	uint8_t data[];
} __packed;
```

调用点 `btp_bip.c:660-668, 850-860` 传 `final = 0`，于是实际发出 10+N 字节，而上位机按 9 字节头解析 → **`data_len` 读到的是 `(0x00 | 真实低字节<<8)`，OBEX header 全部错位**。

受影响事件：
- `BTP_BIP_EV_SERVER_DISCONNECT_REQ` (0x85)
- `BTP_BIP_EV_SERVER_ABORT_REQ` (0x86)
- `BTP_BIP_EV_SECOND_SERVER_DISCONNECT_REQ` (0xac)
- `BTP_BIP_EV_SECOND_SERVER_ABORT_REQ` (0xad)

`send_client_event()` 无此问题（`rsp_code` 在所有 client 事件结构体中都存在）。

**修法**（二选一，推荐 A）：

```c
/* A. 给 4 个事件结构体补一个显式保留/final 字节，保持单一序列化函数 */
struct btp_bip_server_disconnect_req_ev {
	bt_addr_le_t address;
	uint8_t reserved;   /* 与 send_server_event() 的 final 槽位对齐 */
	uint16_t data_len;
	uint8_t data[];
} __packed;

/* B. 为无 final 的事件另写 send_server_event_nofinal() */
```

#### B2. 变长命令完全不校验 `cmd_len` → 越界读

所有 `data[]` 类命令都注册为 `BTP_HANDLER_LENGTH_VARIABLE`（`btp_bip.c:2376-2676`），框架因此**跳过**长度检查（`btp.c:118`），把校验责任交给 handler。但 BIP 的 handler 一律直接信任 `cp->data_len`：

```c
/* btp_bip.c:1886-1908  BIP_CLIENT_OP_HANDLER，另 3 组宏同样 */
uint16_t data_len = sys_le16_to_cpu(cp->data_len);
...
buf = alloc_buf_with_data_bip(&inst->bip, cp->data, data_len);   /* 无任何边界检查 */
```

上位机发 `data_len = 0xFFFF` 而实际只带 2 字节，`net_buf_add_mem()` 就会从 1024 字节的 `cmd_buf[].data` 之后读 64KB。

同目录的 `btp_l2cap.c:835` 与 `btp_gap.c:906` 是有校验的：

```c
if (cmd_len < sizeof(*cp) || cmd_len != sizeof(*cp) + sys_le16_to_cpu(cp->data_len)) {
	return BTP_STATUS_FAILED;
}
```

**修法**：在 4 个宏和 `connect_rsp` / `second_connect_rsp` / `get_partial_image_rsp` 里统一插入一行宏：

```c
#define BIP_CHECK_VAR_LEN(cp, cmd_len)                                                  \
	do {                                                                            \
		if ((cmd_len) < sizeof(*(cp)) ||                                         \
		    (cmd_len) != sizeof(*(cp)) + sys_le16_to_cpu((cp)->data_len)) {      \
			return BTP_STATUS_FAILED;                                       \
		}                                                                       \
	} while (0)
```

#### B3. 事件长度不设上限 → `rsp_buf` 溢出

`send_server_event()` / `send_client_event()` / `bip_server_connect()` / `bip_client_connect()` 把 `buf->len`（由 GOEP MOPL 决定）直接算入 `ev_len`，再交给 `tester_rsp_buffer_allocate()`。后者是 `net_buf_simple_add()` 到一块 `BTP_MTU`(1024) 的静态 buffer（`btp.c:284-289`），**溢出时只有 `__ASSERT`**，release 构建下直接踩内存。`CONFIG_BT_GOEP_L2CAP_MTU` 默认取 `BT_BUF_ACL_RX_SIZE`，完全可能让 `ev_len > BTP_DATA_MAX_SIZE`(1019)。

同时 `if (ev_data == NULL)` 是死代码——`net_buf_simple_add()` 从不返回 NULL，这给了错误的安全感。

**修法**：

```c
if (ev_len > BTP_DATA_MAX_SIZE) {
	LOG_ERR("BIP event 0x%02x too long: %zu", ev_opcode, ev_len);
	return;   /* 或截断并置一个 truncated 标志位 */
}
```

#### B4. `supported_commands()` 手工位图漏报 17 个命令

```c
/* btp_bip.c:1297-1358 */
tester_set_bit(rp->data, BTP_BIP_SECOND_SERVER_UNREGISTER);   /* 0x39，最后一个 */
*rsp_len = sizeof(*rp) + 8;                                   /* 8 字节 → 只覆盖 0x00-0x3f */
```

共 54 次 `tester_set_bit`，但 handler 表里有 71 个 opcode。**0x3a–0x4a 全部漏报**：

- `BTP_BIP_SECOND_GET_*_RSP` (0x3a–0x40)
- `BTP_BIP_SECOND_CONNECT_L2CAP` (0x41)
- `BTP_BIP_SECOND_CONNECT_RFCOMM` (0x42)
- `BTP_BIP_SECOND_GET_*` (0x43–0x4a)

且 8 字节响应长度物理上无法表达 0x40 以上的位。

`btp_sdp.c:235` 和 `btp_l2cap.c:1306` 已经用了自动化方式，BIP 应照抄：

```c
static uint8_t supported_commands(const void *cmd, uint16_t cmd_len, void *rsp, uint16_t *rsp_len)
{
	struct btp_bip_read_supported_commands_rp *rp = rsp;

	*rsp_len = tester_supported_commands(BTP_SERVICE_ID_BIP, rp->data);
	*rsp_len += sizeof(*rp);

	return BTP_STATUS_SUCCESS;
}
```

这一改同时消除了"新增命令忘记加 bit"的整类问题。

#### B5. `server_unregister` 泄漏实例 → 3 轮后功能失效

```c
/* btp_bip.c:1734-1751 */
err = bt_bip_server_unregister(&inst->server);
if (err != 0) { return BTP_STATUS_FAILED; }
return BTP_STATUS_SUCCESS;      /* inst->in_use 永远是 true */
```

注销后 `server._bip` 被 host 清空，于是 `bip_instance_is_preregistered()` 返回 false，但没人再调用 `bip_instance_free()`。后果有两个：

1. **槽位永久占用**。池只有 `CONFIG_BT_MAX_CONN`=3 个槽，3 次 register/unregister 之后 `bip_instance_allocate()` 全部失败。
2. **误绑定**。这个僵尸实例满足 `in_use && conn == NULL`，`find_preregistered_instance_by_address()` 会把后续入向传输绑到它上面 —— 而它已经没有 OBEX server 了，OBEX CONNECT 被回 NOT_FOUND。

`second_server_unregister`（`btp_bip.c:2106-2124`）同样问题。

**修法**：注销后判断实例是否还有存在价值：

```c
static void bip_instance_gc(struct bip_app *inst)
{
	if (inst->conn == NULL && inst->second_conn == NULL &&
	    !bip_instance_is_preregistered(inst)) {
		bip_instance_free(inst);
	}
}
```

---

### P1 — 影响正确性/可测试性

#### B6. `server_register` 注释与实现不符，产生同地址重复实例

注释写的是 "Reuse an existing instance if the connection is already up"，代码却无条件新分配：

```c
/* btp_bip.c:1692-1718 */
conn = bt_conn_lookup_addr_br(&cp->address.a);
if (conn != NULL) {
	inst = bip_instance_allocate(conn);   /* 总是取一个空槽，从不复用 */
```

若该地址已有实例（accept 回调分配的传输实例、或上一次 `server_register` 的实例），就出现**两个同地址实例**。此后 `find_instance_by_address()` 返回下标最小的那个 —— 可能不是持有已注册 server 的那个，导致所有 `*_RSP` 命令路由到空 server。`find_second_server_instance_by_address()` 同样存在这个二义性（它甚至不检查 `conn`）。

另外此函数**不校验 `cp->address.type != BTP_BR_ADDRESS_TYPE`**，而 `connect_rfcomm` / `connect_l2cap` / `sdp_discover` / `second_connect_*` 都校验了。校验不一致本身就是隐患。

**修法**：先 `find_instance_by_address()` / `find_preregistered_instance_by_address()`，命中则复用；并补齐地址类型校验。

#### B7. GetPartialImage 的 primary/secondary 不对称

- **请求侧**：`bip_server_cb.get_partial_image = bip_server_get_partial_image`（primary，`btp_bip.c:703`）**和** `bip_second_server_cb.get_partial_image = bip_second_server_get_partial_image`（secondary，`btp_bip.c:918`）都发同一个事件 `BTP_BIP_EV_SERVER_GET_PARTIAL_IMAGE_REQ`(0x8d)。
- **响应侧**：`get_partial_image_rsp()` **硬编码**走 secondary（`btp_bip.c:1969-1974`，`&inst->second_bip` / `&inst->second_server`）。

于是若 PTS 在 **primary** 连接上发 GetPartialImage（primary cb 已注册，会被接受并上报），上位机的响应会被发到 secondary server 上 → "Invalid state"。注释里说 "GetPartialImage 是 Referenced-Objects-only 操作"，那就不该在 `bip_server_cb` 里注册它。

**修法**：二者取一 —— 要么从 `bip_server_cb` 移除 `get_partial_image`，要么补一个 `BTP_BIP_EV_SECOND_SERVER_GET_PARTIAL_IMAGE_REQ` + `BTP_BIP_SECOND_GET_PARTIAL_IMAGE_RSP` 让两条路径彻底分开（与其余 7 个 Archived-Objects 操作的做法一致）。

#### B8. secondary 传输的连接/断开事件与 primary 混用同一 opcode

```c
/* btp_bip.c:1122-1184 */
static void bip_second_rfcomm_transport_connected(...)
{
	tester_event(BTP_SERVICE_ID_BIP, BTP_BIP_EV_RFCOMM_CONNECTED, &ev, sizeof(ev));
}
```

四个 secondary 传输回调全部复用 `BTP_BIP_EV_{RFCOMM,L2CAP}_{CONNECTED,DISCONNECTED}`。在 Auto-Archive 场景下两条传输同地址同 PSM，上位机**无法判断**收到的断开事件是 primary 还是 secondary 传输，只能靠时序猜。这与模块其余部分（OBEX 层严格区分 SECOND 事件）的设计原则直接矛盾。

**修法**：新增 `BTP_BIP_EV_SECOND_RFCOMM_CONNECTED/DISCONNECTED`、`BTP_BIP_EV_SECOND_L2CAP_CONNECTED/DISCONNECTED`（建议 0xc0–0xc3）。

#### B9. 缺 secondary 传输断开命令

有 `BTP_BIP_SECOND_CONNECT_L2CAP`(0x41) / `SECOND_CONNECT_RFCOMM`(0x42)，但**没有对应的 disconnect**。`disconnect_l2cap` / `disconnect_rfcomm` 硬编码 `&inst->bip`（`btp_bip.c:1438, 1632`）。上位机在 AAI/ACH 初始化方流程里主动建立了 secondary 传输后，无法主动拆除它，只能等对端或 ACL 断开。

**修法**：加 `BTP_BIP_SECOND_DISCONNECT_L2CAP` / `BTP_BIP_SECOND_DISCONNECT_RFCOMM`，作用于 `&inst->second_bip`。

#### B10. 重复绑定造成 `bt_conn` 引用泄漏

`inst->second_conn = bt_conn_ref(conn)` 出现在 5 处（`btp_bip.c:1206, 1391, 1472, 1566, 1607`），**均不先检查旧值**。若上位机重复下发 `SECOND_CONNECT_L2CAP`（失败重试是常规操作），旧 ref 被覆盖丢失 → conn 永不释放。`second_connect_l2cap:1562-1565` 的 `inst->conn` 也有同样模式（虽然有 NULL 检查，但 `bt_addr_copy` 会在实例已有别的地址时覆盖）。

**修法**：统一用一个 setter，或参考 `btp_rfcomm.c:78` 的 `bt_conn_drop(&chan->conn)` 模式先释放旧引用。

#### B11. `bip_inst_get_address()` 在无传输时静默返回全零地址

```c
/* btp_bip.c:382-390 */
if (inst != NULL && (inst->conn != NULL || inst->second_conn != NULL)) {
	bt_addr_copy(&addr->a, &inst->address);
	addr->type = BTP_BR_ADDRESS_TYPE;
} else {
	memset(addr, 0, sizeof(*addr));   /* 事件带 00:00:00:00:00:00 且 type=0 */
}
```

`inst->address` 在实例存活期间一直有效（预注册实例即使 `conn == NULL` 也有正确地址），这个 `conn` 判据既不必要又有害：上位机会收到无法归属的事件，而不是明确的错误。

**修法**：改为 `if (inst != NULL && inst->in_use)`。

#### B12. `sdp_discover()` 的三个问题

```c
/* btp_bip.c:1641-1672 */
static struct bt_uuid_16 uuid;              /* 文件级静态单例 */
...
uuid.val = cp->uuid;                        /* 缺 sys_le16_to_cpu() */
sdp_bip_params.uuid = &uuid.uuid;           /* sdp_bip_params 也是静态单例 */
```

1. **端序**：BTP 是小端协议，`btp_sdp.c:307` 明确做了 `sys_le16_to_cpu()`。此处遗漏，只在 LE 目标上"碰巧"正确。
2. **单例竞争**：`sdp_bip_params` 和 `uuid` 都是文件级静态。对两个对端并发发起 discover（多实例设计本就允许）会互相破坏参数。
3. **`caps` 恒为 0**：事件结构体有 `uint8_t caps` 字段（`btp_bip.h:911`），但 `bip_discover_func()` 里写死 `ev.caps = 0`（`btp_bip.c:477`）——已经有 `bip_sdp_get_functions()` 这个样板了，缺的只是一个 `BT_SDP_ATTR_SUPPORTED_CAPABILITIES` 版本。BIP 的 Responder 记录必带此属性，上位机拿不到就无法验证。

另外 4 个解析函数的返回值全部被忽略（`btp_bip.c:462-465`），解析失败时事件带 0 值上报，无法与"对端确实填 0"区分。

#### B13. `second_connect` 给 secondary 连接 announce 了 primary 的能力集

```c
/* btp_bip.c:2152-2154 */
bt_bip_set_supported_capabilities(&inst->second_bip, bip_supported_caps);
bt_bip_set_supported_features(&inst->second_bip, bip_supported_features);
bt_bip_set_supported_functions(&inst->second_bip, bip_supported_functions);  /* 16 个 function */
```

而 SDP 记录里 Archive 服务只声明 7 个 function（`bip_archive_supported_functions`，`btp_bip.c:106-112`）、RefObj 只声明 2 个（`bip_refobj_supported_functions`，`btp_bip.c:116-117`）。**SDP 广播的能力集与 OBEX 连接上声明的不一致**，PTS 若做交叉校验会失败。应按 `cp->conn_type` 选择对应的 mask。

#### B14. `tester_unregister_bip()` 留下悬垂注册

```c
/* btp_bip.c:2777-2786 */
for (...) { if (bip_apps[i].in_use) { bip_instance_free(&bip_apps[i]); } }
```

只把 `in_use` 置 false，但**不调用 `bt_bip_server_unregister()`**。host 侧 OBEX 仍持有 `&inst->server` / `&inst->second_server` 指针，而下一次 `bip_instance_allocate()` 会 `memset` 整个槽（`btp_bip.c:286`）→ host 内部链表被清零。同时 RFCOMM/L2CAP server 和 3 条 SDP 记录也未注销，重新 `register_service` 时 `bt_bip_rfcomm_register()` 会失败。

> 说明：`btp_rfcomm.c:487` / `btp_sdp.c:476` 的 unregister 直接 `return SUCCESS` 什么都不做——所以这不是 BIP 独有的框架缺陷。但 BIP 是唯一"半清理"的模块，半清理比不清理更危险。

#### B15. 无 ACL 断开清理、无 reset 命令 → 粘滞状态

模块没有注册任何 `bt_conn_cb`（全文无 `BT_CONN_CB_DEFINE`）。所有清理都挂在传输 disconnected 回调上。

更麻烦的是 `find_second_server_instance_by_address()` 一旦为某地址返回非 NULL，`archive_*_accept()` 就**永久**把该地址在 archive PSM 上的所有入向传输判为 secondary（`btp_bip.c:1248, 1260`）。在同一块板子上连续跑多个 AAI 测试用例、中间不重启，第二个用例的 primary Auto-Archive CONNECT 会被误路由到 secondary 上下文。

**修法**：注册 `bt_conn_cb.disconnected` 清理该地址的全部实例；并增加一个 `BTP_BIP_RESET` 命令供上位机在用例间显式复位。

#### B16. 实例表跨线程无保护

`bip_apps[]` 同时被 BTP 命令线程（`btp.c:93` 的 `cmd_handler`，`K_PRIO_COOP(7)`）和蓝牙 RX 线程（accept / transport / OBEX 回调）读写。`bip_instance_allocate()` 的"扫描空槽 → 置 `in_use`"不是原子的，`bip_transport_accept()` 与并发的 `connect_l2cap()` 可能拿到同一个槽。协作式优先级降低了概率但不消除（回调里有 `tester_event()` → UART 输出 → 可能让出）。

**修法**：实例表操作加一把 `k_mutex`，或至少给 `in_use` 用 `atomic_t` + CAS。

---

### P2 — 可读性与一致性

| # | 问题 | 位置 | 建议 |
|---|---|---|---|
| C1 | **全文零日志**（`grep -c LOG_` = 0）。2786 行、几十条失败返回路径，全部只返回 `BTP_STATUS_FAILED`，无法从串口日志定位是"找不到实例"还是"host 拒绝"。`btp_rfcomm.c:15` 有 `LOG_MODULE_REGISTER`。 | 全文 | 加 `LOG_MODULE_REGISTER(btp_bip, ...)`，每个失败分支一条 `LOG_DBG/LOG_ERR`（含 opcode + 地址 + errno）。这是投入产出比最高的一项改动。 |
| C2 | **靠长注释而非命名/结构表达设计**。`btp_bip.c` 有 20+ 处 5–20 行的注释解释"为什么必须用 second_bip 而不是 bip"。信息很宝贵，但说明抽象层次不对——路由选择应该被封装进函数名。 | `btp_bip.c:1101, 1229, 1377, 1462, 2141` 等 | 引入 `struct bip_link { struct bt_bip *bip; struct bt_bip_server *srv; struct bt_bip_client *cli; }`，用 `bip_link_primary(inst)` / `bip_link_second(inst)` 返回。所有 handler 宏改为接受一个 `link` 参数，四组宏（primary/second × client/server）合并为两组。长注释可压缩为对 `bip_link` 的一段设计说明。 |
| C3 | **60+ 个字节级完全相同的事件结构体**（`{addr, u8, u16, data[]}`）。`btp_bip.h` 1109 行里约 700 行是这种重复。 | `btp_bip.h:623-1105` | 定义一次 `struct btp_bip_data_ev { bt_addr_le_t address; uint8_t param; uint16_t data_len; uint8_t data[]; } __packed;`，其余只保留 opcode `#define` + 一行注释说明 `param` 的语义（final / rsp_code）。同理命令侧 `struct btp_bip_data_cmd`。这样 B1 那类结构体/序列化不一致从根上不会再发生。 |
| C4 | **手工序列化重复了结构体布局**。`send_server_event()` 用 `off += sizeof(...)` 逐字段拼，与头文件里的 `__packed` 结构体是两份独立真相 —— B1 就是这么来的。 | `btp_bip.c:556-621` | 改为 `struct btp_bip_data_ev *ev = (void *)ev_data;` 直接赋值，和 `bip_server_connect()`（`btp_bip.c:644`）的写法统一。 |
| C5 | 死代码：空 `if` 体 `if (err != 0) { }` | `btp_bip.c:1499-1501` | 删除 |
| C6 | 死代码：空宏 `#define BIP_2ND_CONN_TYPE_ARCHIVED_OBJECTS`（与 `bip.h` 的 enum 重名，易误读） | `btp_bip.c:2126` | 删除 |
| C7 | 死代码：`if (ev_data == NULL)` 4 处 —— `tester_rsp_buffer_allocate()` 从不返回 NULL | `btp_bip.c:569, 639, 829, 947` | 替换为 B3 的长度上限检查 |
| C8 | 代码风格违规（checkpatch 会报）：空格缩进、`){` 缺空格 | `btp_bip.c:191, 232, 289-291` | `if (conn != NULL) {` + tab 缩进 |
| C9 | **opcode 空洞 0x02 / 0x05 / 0x08 无任何说明**。读者无法判断是保留、废弃还是笔误。 | `btp_bip.h:21, 32, 43` | 加 `/* 0x02: reserved (was ...) */` 或重排压实 |
| C10 | **直接访问 host 私有字段 `_bip`**（下划线前缀 = 私有约定） | `btp_bip.c:327-328, 1114-1115` | 在 `bip_app` 里加 `bool server_registered; bool second_server_registered;` 自己维护；或向 host 申请 `bt_bip_server_is_registered()` API |
| C11 | 命名混用 `second_` / `secondary_` / `2ND_` | 全文 | 统一为 `second_`（已是多数派） |
| C12 | **无上位机文档**。`README.rst` 无 BIP 章节，仓库内也没有 BIP 的 pytest/上位机实现（`grep -rl bip tests/bluetooth --include=*.py` 无结果）。71 命令 + 64 事件的契约、以及 primary/secondary 的调用时序完全靠读 C 代码推断。 | — | 补一份 `doc/btp_bip.md`：opcode 表 + AAI/ACH/RefObj 三个流程的时序图 |

---

## 四、命令/响应参数设计上的具体缺口

| # | 缺口 | 影响 |
|---|---|---|
| D1 | `BTP_BIP_CLIENT_CONNECT`(0x0c) / `SECOND_CONNECT`(0x33) 结构体无 `data[]`，handler 传 `NULL` buf（`btp_bip.c:1769, 2156`） | 上位机无法在 OBEX CONNECT 请求里附加 header（Count、自定义/非法 Target 等），一批 PTS 用例做不了 |
| D2 | `BTP_BIP_OBEX_DISCONNECT`(0x0d) / `OBEX_ABORT`(0x0e) / `SECOND_*` 同样无 `data[]`，传 `NULL`（`btp_bip.c:1789, 1808, 2177, 2196`） | 无法测试带 Description header 的 ABORT |
| D3 | `BTP_BIP_DISCONNECT_RSP`(0x10) / `ABORT_RSP`(0x11) / `SECOND_*` 结构体无 `data[]`，传 `NULL`（`btp_bip.c:1858, 1877, 2248, 2267`） | 响应侧同样无法带 header。而 `CONNECT_RSP`(0x0f) 有 `data[]` —— **同类命令设计不一致** |
| D4 | 能力集（caps / features / functions / max memory）是文件级静态变量（`btp_bip.c:73-102`），SDP 记录在编译期绑定 | 上位机无法按用例调整 SupportedFunctions —— 而 BIP PTS 大量用例正是靠改这个 mask 触发不同分支。建议加 `BTP_BIP_SET_CAPABILITIES` 命令（在 `server_register`/`client_connect` 前生效） |
| D5 | L2CAP PSM / RFCOMM channel 编译期固定（0x1009/0x9、0x100b/0xa、0x100d/0xb） | 同上，无法测试 PSM 协商变体 |
| D6 | `data_len` 与 BTP header 的 `len` 是冗余信息，从不交叉校验 | 见 B2；即使修了越界，冗余字段仍应显式校验一致性 |
| D7 | `final` 字段为 `uint8_t`，直接传给 `bool` 形参，不校验 0/1 | 非 0/1 值被静默当作 true，行为不可预测（低危，但应 `!!cp->final` 或显式拒绝） |
| D8 | `BTP_BIP_SDP_DISCOVER` 只上报第一条记录（`bip_discover_func` 恒返回 `BT_SDP_DISCOVER_UUID_STOP`，`btp_bip.c:483`），且无"发现结束/失败"事件 | 上位机只能靠超时判断 discover 结束 |
| D9 | `mopl` / `conn_id` 只在 connected 事件里出现一次，无查询命令 | 上位机必须自己缓存；跨用例复用连接时易失步 |

---

## 五、建议的修改顺序

### 批次 1（阻塞级，约 100 行改动）

1. **B4** — 换 `tester_supported_commands()`（4 行，立即修复 17 个命令的可发现性）
2. **B1** — 4 个事件结构体补对齐字节
3. **B2** — 加 `BIP_CHECK_VAR_LEN()` 并在 4 个宏 + 3 个独立 handler 中调用
4. **B3** — 事件长度上限检查，同时删掉 C7 的死 NULL 判断
5. **B5** — `bip_instance_gc()`，在两个 `*_unregister` 中调用
6. **C1** — 加日志模块与失败路径日志
7. **C5/C6/C8** — 清理死代码与风格

### 批次 2（正确性与对称性）

8. **B6** — `server_register` 复用实例 + 补地址类型校验
9. **B7** — GetPartialImage 路径二选一定案
10. **B8 + B9** — secondary 传输事件独立 opcode + secondary disconnect 命令
11. **B10/B11** — conn 引用管理与地址上报
12. **B12** — sdp_discover 端序 / 去单例 / 补 caps 解析
13. **B13** — 按 conn_type 选择 secondary 能力 mask

### 批次 3（健壮性与结构）

14. **B14/B15** — unregister 完整清理 + `bt_conn_cb.disconnected` + `BTP_BIP_RESET`
15. **B16** — 实例表加锁
16. **C2/C3/C4** — 引入 `bip_link` 抽象，合并宏组，合并事件结构体（这一步会把文件从 2786 行压到约 1800 行，并让 `btp_bip.h` 减半）
17. **D1–D3** — 给 CONNECT/DISCONNECT/ABORT 请求与响应统一补 `data[]`
18. **D4/D5** — 运行期可配置能力集与 PSM
19. **C12** — 补上位机文档

---

## 六、验证建议

- 批次 1 之后必须回归的最小集：**上位机对每个 opcode 发一次超长 `data_len`（如 0xFFFF 而实际带 0 字节）**，确认返回 `BTP_STATUS_FAILED` 而不是崩溃 —— 这直接覆盖 B2。
- 用 `READ_SUPPORTED_COMMANDS` 的返回位图与 `handlers[]` 表做一致性断言（B4 修完后自动成立）。
- 针对 B1，在上位机侧对 `SERVER_DISCONNECT_REQ` 事件断言 `len == 9 + data_len`。
- 针对 B5，脚本化跑 4 轮 `SERVER_REGISTER` → `SERVER_UNREGISTER`，第 4 轮仍应成功。
- 针对 B15，同一 PTS session 内连续跑两个 AAI 用例、中间不复位板子。

---

## 七、问题速查表

| ID | 严重度 | 摘要 | 主要位置 |
|---|---|---|---|
| B1 | P0 | 4 个 server disconnect/abort 事件多发 1 字节，与头文件结构体不符 | `btp_bip.c:556-587` / `btp_bip.h:609-621` |
| B2 | P0 | 变长命令不校验 `cmd_len`，`data_len` 可致 64KB 越界读 | `btp_bip.c:1886, 1910, 1993, 2283` |
| B3 | P0 | 事件长度无上限，`rsp_buf`(1024) 可溢出 | `btp_bip.c:556, 590, 624, 716` |
| B4 | P0 | `supported_commands` 手工位图漏报 0x3a–0x4a 共 17 个命令 | `btp_bip.c:1297-1358` |
| B5 | P0 | `server_unregister` 不回收实例，3 轮后池耗尽 + 误绑定 | `btp_bip.c:1734, 2106` |
| B6 | P1 | `server_register` 无条件新分配 → 同地址重复实例，路由二义 | `btp_bip.c:1683-1732` |
| B7 | P1 | GetPartialImage primary 请求 / secondary 响应，路径不对称 | `btp_bip.c:703, 918, 1955` |
| B8 | P1 | secondary 传输 connect/disconnect 复用 primary 事件 opcode | `btp_bip.c:1122-1184` |
| B9 | P1 | 缺 `SECOND_DISCONNECT_L2CAP/RFCOMM` 命令 | `btp_bip.c:1427, 1621` |
| B10 | P1 | `second_conn`/`conn` 重复绑定造成 `bt_conn` 引用泄漏 | `btp_bip.c:1206, 1391, 1472, 1566, 1607` |
| B11 | P1 | `conn == NULL` 时事件上报全零地址 | `btp_bip.c:382-390` |
| B12 | P1 | sdp_discover：静态单例 + 缺 `le16_to_cpu` + `caps` 恒 0 | `btp_bip.c:1641-1672, 477` |
| B13 | P1 | secondary 连接 announce 了 primary 能力集，与 SDP 记录不一致 | `btp_bip.c:2152-2154` |
| B14 | P1 | `tester_unregister_bip` 半清理 → host 侧悬垂指针 | `btp_bip.c:2777-2786` |
| B15 | P1 | 无 ACL 断开清理、无 reset，secondary 路由判据永久粘滞 | `btp_bip.c:1248, 1260` |
| B16 | P1 | `bip_apps[]` 跨线程无锁 | 全文 |
| C1–C12 | P2 | 零日志、抽象层次、结构体重复、死代码、风格、文档 | 见上表 |
| D1–D9 | P2 | 参数缺口：CONNECT/ABORT 无 `data[]`、能力集/PSM 不可配置等 | 见上表 |

---

**一句话总结**：设计骨架（多实例 + primary/secondary 双 `bt_bip` + 宏化）是可靠的，值得保留；当前的风险几乎全部集中在 **BTP 边界的输入/输出校验**（B1–B4）和**实例生命周期**（B5、B6、B14、B15）这两处，且都是局部可修的。批次 1 的 7 项约 100 行改动即可把模块从"能跑通已知用例"提升到"能承受上位机异常输入"。
