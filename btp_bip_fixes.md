# Zephyr Tester BIP 模块 — 修复记录（批次 1：B1–B5；批次 2：B6、B7、B8、B9、B10、B11、B12（1/3/4）、B13）

**修复对象**
- `tests/bluetooth/tester/src/btp_bip.c`
- `tests/bluetooth/tester/src/btp/btp_bip.h`

**基准**：`btp_bip_review.md`（批次 1 = B1–B5 + C1/C7；批次 2 = B6–B13，本文档目前落地 B10、B11、B12（子项 1/3/4；子项 2 明确不修）、B13；B13 曾因 auto-pts 侧 `hdl_wid_38` 的既有 bug 被阻塞而临时还原，Stack Spec 核实改动本身正确后已重新应用，auto-pts 侧仍待单独修复）
**仓库**：`C:\_DDM\zephyr\zephyrproject_2\zephyr`，分支 `feature/bip_pts`，基线 HEAD `f47bb53f1f1`

> 本文档记录**已实施**的修复及其与原评审的偏差。凡评审 `btp_bip_review.md` 里的分析被本次核实推翻的，均在对应条目的「订正」段说明——以本文档为准。
>
> **验证状态**：本批次改动**编译通过**（B1、B2、B4、B10、B11、B12（1/3/4）、B13 均已编译；B5 动了实例生命周期 + 写 host 私有字段，建议单独跑回归）。除非另行注明，均**尚未在 PTS 上线实测**。

---

## 总览

| ID | 严重度 | 问题 | 状态 | 与评审的偏差 |
|---|---|---|---|---|
| B1 | P0 | 4 个 disconnect/abort 事件多发 1 字节 | ✅ 已修 | 方案改为 B（无 final 专用序列化）+ 结构体私有化到 `.c` |
| B2 | P0 | 变长命令不校验 `cmd_len` | ✅ 已修 | **严重度订正**：非 64KB 越界，实为一个 MTU 量级静态内存读 |
| B3 | P0 | 事件长度无上限，`rsp_buf` 溢出 | ✅ 已修 | 覆盖 **7** 处（评审记为 6），连带删除 C7 死代码 |
| B4 | P0 | `supported_commands` 漏报 17 个命令 | ✅ 已修 | 无偏差 |
| B5 | P0 | `server_unregister` 泄漏实例 | ✅ 已修 | **因果链订正**：host 不清 `_bip`，泄漏比评审描述更顽固 |
| C1 | P2 | 全文零日志 | ✅ 部分 | 已引入 `LOG_MODULE_REGISTER` + 失败日志（非全量） |
| C7 | P2 | `if (ev_data == NULL)` 死代码 | ✅ 已修 | 随 B3 一并删除（6 处） |
| B6 | P1 | `server_register` 无条件新分配 → 同地址重复实例，路由二义 | ✅ 已修 | 复用实例 + 地址类型校验 + 幂等 SUCCESS |
| B7 | P1 | GetPartialImage primary 请求 / secondary 响应，路径不对称 | ✅ 已修 | 方案 A：从 primary server 移除 get_partial_image 注册 |
| B8 | P1 | secondary 传输连接/断开事件复用 primary opcode，无法区分 | ✅ 已修 | 新增 4 个 SECOND 传输事件 opcode（0xc0–0xc3）；auto-pts 侧 SECOND 断开改为 no-op 避免误删 primary 状态 |
| B9 | P1 | 缺 secondary 传输断开命令 | ✅ 已修 | 新增 SECOND_DISCONNECT_L2CAP/RFCOMM（0x4b/0x4c） |
| B10 | P1 | `second_conn` 重复绑定造成 `bt_conn` 引用泄漏 | ✅ 已修 | 批次 2 首项；含一处死代码发现（见 B10 末） |
| B11 | P1 | `bip_inst_get_address()` 无传输时静默返回全零地址 | ✅ 已修 | 判据改为 `inst->in_use`；已核实无竞态窗口（见 B11 详情） |
| B13 | P1 | `second_connect` 给 secondary 连接 announce 了 primary 的能力集 | ✅ 已修（重新应用） | 按 `conn_type` 选 `functions` mask；改动经 Stack Spec 核实正确；曾因 auto-pts 侧 `hdl_wid_38` bug 被临时还原，现已重新应用（见 B13 详情） |
| B12(1) | P1 | `sdp_discover`：UUID 端序缺 `sys_le16_to_cpu()` | ✅ 已修（用户手动） | 当前小端目标板上非活 bug，仅补齐语义；与 `btp_sdp.c:307` 处理方式一致 |
| B12(2) | P1 | `sdp_discover`：`uuid`/`sdp_bip_params` 静态单例，多实例并发 discover 会互相覆盖 | ⏸️ 明确不修 | 评估当前 PTS 测试单实例串行触发，不会命中；问题描述保留，见 B12 详情 |
| B12(3) | P1 | `sdp_discover`：caps 恒为 0 | ✅ 已修 | **范围收窄**：只在发现主 Imaging 记录（`BT_SDP_IMAGING_RESPONDER_SVCLASS`）时才解析 caps；RefObj/Archive 记录本来就没有 caps 属性（Stack Spec 6.1.2/6.1.3），继续留 0 是正确行为，不是 bug |
| B12(4) | P2 | `sdp_discover`：4 个解析函数返回值被忽略，失败与"对端填 0"无法区分 | ✅ 已修 | **范围收窄**：只对"该有却没解出来"的情况加 `LOG_DBG`；transport 只在 rfcomm/l2cap 都失败时才报；caps/features 只在发现主记录时才报——避免对 RefObj/Archive 记录（本来就没有 caps/features）误报 |

---

## B1 — 4 个事件的线格式与头文件不一致（多 1 字节）

### 问题

`send_server_event()` 无条件写入 `final` 字节，但 disconnect/abort 类事件的结构体没有该字段（`btp_bip_server_disconnect_req_ev` 等 4 个）。调用点传 `final = 0`，实发 10+N 字节，上位机按 9 字节头解析 → `data_len` 错位、OBEX header 全部偏移。

受影响事件：`SERVER_DISCONNECT_REQ`(0x85)、`SERVER_ABORT_REQ`(0x86)、`SECOND_SERVER_DISCONNECT_REQ`(0xac)、`SECOND_SERVER_ABORT_REQ`(0xad)。

### 核实（上线前的下游确认）

- auto-pts 侧 `_bip_ev_server_data_req()`（`autopts/pybtp/btp/bip.py:1961`）用 `'<B6sH'`（**9 字节**头）解析这 4 个事件，与头文件一致、与 C 侧实发的 10 字节不一致。
- 用脚本全量扫描 `btp_bip.h`：**缺参数字节的事件结构体恰好只有这 4 个**，无漏项。
- 下游只到 `bip.rx(addr, ev_id, (body,))`，`autopts/wid/`、`autopts/ptsprojects/` 无任何代码引用这些事件 → 不存在"迁就旧格式而剥前导字节"的补偿逻辑会被反向弄坏。
- **故障只在携带 OBEX header 时暴露**：`data_len = 0` 时新旧代码解析结果相同（这正是一直没被发现的原因）。

### 修复方案（评审方案 B）

新增无 final 的专用序列化函数，让这 4 个事件的线格式回到 9 字节，**auto-pts 侧零改动**。

- `send_server_event_nofinal()`（`btp_bip.c:662`）：用 `struct bip_plain_ev` 布局，不写参数字节。
- 4 个回调改调它：`bip_server_disconnect` / `bip_server_abort` / `bip_second_server_disconnect` / `bip_second_server_abort`。

同时把手工偏移序列化改为**结构体赋值**（消除评审 C4「两份真相」的即时诱因）：引入两个**私有于 `.c`** 的规范布局结构体（`btp_bip.c:594`、`601`）：

```c
struct bip_param_ev {   /* 带 final / rsp_code 的事件族 */
	bt_addr_le_t address;
	uint8_t param;
	uint16_t data_len;
	uint8_t data[];
} __packed;

struct bip_plain_ev {   /* 无参数字节的 disconnect/abort 事件族 */
	bt_addr_le_t address;
	uint16_t data_len;
	uint8_t data[];
} __packed;
```

`send_server_event()` / `send_client_event()` 也改为通过 `struct bip_param_ev *ev = (void *)ev_data;` 赋值，不再手拼偏移。

### 与评审的偏差

1. **选方案 B 而非评审推荐的方案 A**（给结构体补 reserved 字节）。理由：方案 A 会给协议引入一个无语义字节，且需同步改 auto-pts 的 `'<B6sH'` → `'<B6sBH'`；方案 B 线格式干净、Python 零改动。
2. **规范布局结构体放在 `.c` 而非 `.h`**，且去掉 `btp_` 前缀（`bip_param_ev` / `bip_plain_ev`）。理由：它们无 opcode，不是上位机协议元素，而是序列化实现细节；`btp_bip.h` 被 `btp/btp.h` 全局引入，其他 `btp_*.h` 无此类"无 opcode 通用布局结构体"先例。**最终 B1 是纯 `.c` 改动，头文件不变**——这准确反映了「头文件本来是对的，错的是序列化函数」。
3. **未采用 BUILD_ASSERT 断言墙**。评审草案曾建议用 ~55 行 `BUILD_ASSERT` 把每个事件结构体钉死到规范布局上；因整个 tester 无此风格（`BUILD_ASSERT` 全仓仅 `btp_gap.c`/`btp_sdp.c` 各 1 处），为保持一致性去掉。**代价**：B1 这类"改事件结构体字段、序列化函数不受影响"的问题失去编译期防护，靠 `.c` 注释里的约定维持。

### 残留

- 未上线实测。差别只在 disconnect/abort **携带 OBEX header** 时可见，需一个会发带 header 的 OBEX ABORT/DISCONNECT 的 PTS 用例。

---

## B2 — 变长命令不校验 `cmd_len`

### 问题

49 个命令注册为 `BTP_HANDLER_LENGTH_VARIABLE`，框架跳过长度检查（`btp.c` 的 `expect_len >= 0` 分支不成立），校验责任落到 handler。但 7 处 handler（4 个宏 + `connect_rsp` / `get_partial_image_rsp` / `second_connect_rsp`）一律直接信任 `cp->data_len`，从不与 `cmd_len` 比对。

### 严重度订正（重要）

**评审写的「64KB 越界读」是错的。** `alloc_buf_with_data_bip()`（`btp_bip.c`）有 `net_buf_tailroom(buf) < data_len` 检查，`data_len = 0xFFFF` 会被拒绝返回 NULL，命令只是失败。（评审自己在「优点 7」表扬了这个检查，两处结论互相矛盾。）

真实越界窗口：`(cmd_len - sizeof(*cp)) < data_len ≤ net_buf_tailroom`，即最多**一个 GOEP MTU 量级（几百字节）**的超读，起点在 1024 字节静态数组 `cmd_buf[].data` 内部，越界后落进相邻 `cmd_buf[]` 或邻近静态变量。后果：**相邻静态内存（含另一条排队中的 BTP 命令）被当作 OBEX header 发给对端**。全程在 BSS 内，此板不触发异常——所以一直没被发现。不是崩溃级，但是确定的内存泄露 + PDU 不可复现。

反向情形（`data_len` 小于实际收到字节数）也被静默接受，尾部被忽略不报错。

### 修复方案

照 `btp_l2cap.c` / `btp_gap.c` 既有写法，加校验宏（`btp_bip.c:1401`）：

```c
#define BIP_CHECK_VAR_LEN(_cp, _cmd_len)                                      \
	do {                                                                   \
		if ((_cmd_len) < sizeof(*(_cp))) {                             \
			LOG_ERR("BIP cmd too short: %u < %zu", ...);           \
			return BTP_STATUS_FAILED;                              \
		}                                                              \
		if ((_cmd_len) != sizeof(*(_cp)) +                             \
		    sys_le16_to_cpu((_cp)->data_len)) {                        \
			LOG_ERR("BIP cmd len mismatch: ...");                  \
			return BTP_STATUS_FAILED;                              \
		}                                                              \
	} while (0)
```

拆成两个 `if` 而非 `||` 短路：诊断更具体，且「固定部分长度先检查」这个安全依赖变成显式控制流，不依赖读者理解短路语义。

**7 处插入**，全部在 `cp` 绑定后、`cp->data_len` 首次读取前：
- 4 个宏体：`BIP_CLIENT_OP_HANDLER`、`BIP_SERVER_RSP_HANDLER`、`BIP_SECOND_CLIENT_OP_HANDLER`、`BIP_SECOND_SERVER_RSP_HANDLER`
- 3 个独立 handler：`connect_rsp` / `get_partial_image_rsp` / `second_connect_rsp`（这 3 处把 `uint16_t data_len = ...` 拆成先声明、校验后赋值）

### 核实（回归风险已排除）

auto-pts 侧 6 个变长命令构造函数（`_bip_operation_cmd` 等）的 `data_len` 一律由 `len(payload)` 导出，且 BTP header 的 `len` 由 socket 层按实际字节数填写 → 恰好满足 `cmd_len == sizeof(*cp) + data_len`。**新校验不会拒收 auto-pts 现有命令**，Python 零改动。

### 附带收益

这 49 个 handler 原本都把 `cmd_len` 声明为形参却从不使用（编译器默认不报 unused parameter，无信号）。修完后 `cmd_len` 全部被用到。

---

## B3 — 事件长度无上限 → `rsp_buf` 溢出

### 问题

事件发送把 `buf->len`（由 GOEP MOPL 决定）直接算入 `ev_len`，交给 `tester_rsp_buffer_allocate()`——后者是 `net_buf_simple_add()` 到 1024 字节静态 buffer，溢出时只有 `__ASSERT`（release 构建编译掉 → 踩内存）。`CONFIG_BT_GOEP_L2CAP_MTU` 默认取 `BT_BUF_ACL_RX_SIZE`，可能让 `ev_len > BTP_DATA_MAX_SIZE`(1019)。

### 修复方案

新增长度上限守卫（`btp_bip.c:614`）：

```c
static bool bip_ev_len_ok(uint8_t ev_opcode, size_t ev_len)
{
	if (ev_len > BTP_DATA_MAX_SIZE) {
		LOG_ERR("BIP event 0x%02x dropped: %zu bytes exceed BTP limit %zu",
			ev_opcode, ev_len, (size_t)BTP_DATA_MAX_SIZE);
		return false;
	}
	return true;
}
```

### 覆盖范围订正

评审 B3 记为影响 4 处、修法覆盖不明。实际核实：**有 7 处变长事件路径**（唯一有溢出风险的），全部已加守卫：

| 路径 | 说明 |
|---|---|
| `send_server_event`（含 final，通用） | ✓ |
| `send_server_event_nofinal`（通用） | ✓ |
| `send_client_event`（通用） | ✓ |
| `SERVER_CONNECT_REQ`（内联） | ✓ |
| `CLIENT_CONNECTED`（内联） | ✓ |
| `SECOND_SERVER_CONNECT_REQ`（内联） | ✓ |
| `SECOND_CLIENT_CONNECTED`（内联） | ✓ |

另有 **9 处固定大小事件**（SDP discovered、RFCOMM/L2CAP connected/disconnected，primary+secondary 传输回调）用栈局部结构体 + `sizeof(ev)`，长度是编译期常量且远小于 1019，无需检查。

### 连带 C7

评审 C7 指出 `if (ev_data == NULL)` 是死代码（`net_buf_simple_add()` 从不返回 NULL）。**6 处死 NULL 判断在 B3 改动中全部删除**，替换为前置的 `bip_ev_len_ok()` 检查。

---

## B4 — `supported_commands()` 手工位图漏报 17 个命令

### 问题

`supported_commands()` 手工调用约 60 个 `tester_set_bit()`，末尾写死 `*rsp_len = sizeof(*rp) + 8`。两个问题：
1. 漏报 0x3a–0x4a 共 17 个命令（`SECOND_GET_*_RSP`、`SECOND_CONNECT_L2CAP/RFCOMM`、`SECOND_GET_*`）。
2. 8 字节位图物理上只能表达 0x00–0x3f，最大注册 opcode 0x4a 需 10 字节。

### 修复方案

照 `btp_sdp.c` / `btp_l2cap.c`，改用框架的 `tester_supported_commands()` 自动从 handler 表生成位图（`btp_bip.c:1414`）：

```c
static uint8_t supported_commands(const void *cmd, uint16_t cmd_len, void *rsp, uint16_t *rsp_len)
{
	struct btp_bip_read_supported_commands_rp *rp = rsp;

	*rsp_len = tester_supported_commands(BTP_SERVICE_ID_BIP, rp->data);
	*rsp_len += sizeof(*rp);

	return BTP_STATUS_SUCCESS;
}
```

`tester_supported_commands()`（`btp.c:372`）遍历已注册 handler 表逐个置位，按最大 opcode 返回字节数（此处 `(0x4a/8)+1 = 10`）。

### 效果

- 17 个漏报命令自动补齐；位图长度自适应，不再被写死 8 字节截断。
- **消除整类问题**：今后增删 handler，位图自动跟随。
- 核实：auto-pts 无用例实际解析这条响应（`read_supported_cmds` 仅定义在 `BIP` 字典，无消费方），位图变长无回归。

---

## B5 — `server_unregister` 泄漏实例

### 问题

`server_unregister` / `second_server_unregister` 调 `bt_bip_server_unregister()` 成功后直接返回，`inst->in_use` 仍为 true，从不调用 `bip_instance_free()`。池仅 `CONFIG_BT_MAX_CONN`=3 槽，数轮 register/unregister 后 `bip_instance_allocate()` 全部失败。

### 因果链订正（重要）

**评审称「注销后 `server._bip` 被 host 清空，于是 `is_preregistered()` 返回 false」——错的。** 核实 host 侧 `bt_bip_server_unregister()`（`subsys/bluetooth/host/classic/bip.c:1204`）：只做 `sys_slist_find_and_remove(&server->_bip->_servers, ...)`，**不动 `server->_bip`**。

所以注销后 `inst->server._bip` 仍 `== &inst->bip`，`bip_instance_is_preregistered()` 仍返回 **true**。后果**比评审更严重**：

1. **槽位永久占用**（与评审一致）。
2. **传输断开也救不回来**（评审未意识到）：即使之后 ACL/传输断开走到 `bip_instance_release_transport()`，由于 `is_preregistered` 仍 true，`in_use` 也不会被置 false。评审以为断开能触发回收，实际不能。
3. **误绑定**（与评审一致）：僵尸实例满足 `in_use && conn==NULL && 地址匹配`，`find_preregistered_instance_by_address()` 会把后续同地址入向传输绑到它，但其 OBEX server 已从 host 链表移除 → CONNECT 回 NOT_FOUND。

### 附带缺陷（评审未提）

两个 unregister 原本用 `find_instance_by_address()`（要求 `conn != NULL`）。纯预注册实例（`server_register` 时对端未连，`conn==NULL`）在对端仍未连时想注销会**直接失败**——独立于泄漏的第二个缺陷。

### 修复方案（评审方案 A：最小止泄漏）

新增 gc 辅助（`btp_bip.c:369`）：

```c
static void bip_instance_gc(struct bip_app *inst)
{
	if (inst->conn == NULL && inst->second_conn == NULL &&
	    !bip_instance_is_preregistered(inst)) {
		bip_instance_free(inst);
	}
}
```

因 host 不清 `_bip`，两个 unregister handler 在注销成功后**手动清 `_bip`**，让 `is_preregistered()` 恢复真实语义，再调 gc：

- `server_unregister`（`btp_bip.c:1797`）：查找加 fallback（`find_instance_by_address` 未命中试 `find_preregistered_instance_by_address`，修附带缺陷）→ `inst->server._bip = NULL`（`:1826`）→ `bip_instance_gc(inst)`。
- `second_server_unregister`（`btp_bip.c:2194`）：改用 `find_second_server_instance_by_address`（按 `second_server` 绑定查找，不要求 conn）→ `inst->second_server._bip = NULL`（`:2217`）→ gc。

### 领域约束下的边界（据用户澄清订正）

**同一 `bip_app` 实例、同一时刻，primary server 与 secondary server 只能有一个被注册**（互斥）。因此评审草案讨论过的「双 server 并存」场景**不存在**，相关边界分析已撤回。

真实的 secondary 场景：`bt_bip_secondary_server_register()` 的最后一个参数是 `&inst->client`，即 secondary server **依附于同一实例上已连接的 primary client**（host 要求 `client._bip != NULL`）。所以跑 secondary server 的实例**同时持有一条活着的 primary client 传输**（`inst->conn != NULL`）。于是 `second_server_unregister` 之后：

- gc 的首个条件 `inst->conn == NULL` **不成立** → gc **不回收**。
- **这是正确的**：secondary server 注销了，但实例还持有 primary client 连接，不该被回收。待该传输断开时由 `bip_instance_release_transport()` 处理。

方案 A 实现不受此约束影响，逻辑闭合。

### 代价

`inst->server._bip = NULL` / `inst->second_server._bip = NULL` 直接写 host 私有字段（评审 C10 已标记的坏味道）。这是权宜——host 未提供 `bt_bip_server_is_registered()` 之类 API。彻底做法（在 `bip_app` 自维护 `server_registered` 标志）留待批次 3 与 C10 一并处理。

### 残留 / 建议回归

- 未上线实测。评审建议：脚本化跑 4 轮 `SERVER_REGISTER`→`SERVER_UNREGISTER`，第 4 轮仍应成功。

---

## C1 — 引入日志模块（部分）

`btp_bip.c` 原是唯一无日志的 BTP 模块（其余 11 个 tester 文件均有 `LOG_MODULE_REGISTER`）。本批次已加：

```c
#include <zephyr/logging/log.h>
#define LOG_MODULE_NAME btp_bip
LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_BTTESTER_LOG_LEVEL);
```

并在 B2（`BIP_CHECK_VAR_LEN`）、B3（`bip_ev_len_ok`）的失败路径加了 `LOG_ERR`。**非全量**——评审 C1 建议的"每个失败分支一条日志"留待后续批次。

---

# 批次 2

> 批次 2（B6–B13）为 P1「正确性/对称性」。经场景分析确认：这些多为**健壮性/防御性**缺陷，在标准 PTS 流程下不一定发作，但在重试、非标准时序、跨用例连跑时会咬人。已落地 B8、B9、B10、B11、B12（子项 1/3/4）、B13。

## B8 — secondary 传输连接/断开事件复用 primary opcode

### 问题

4 个 secondary 传输回调（`bip_second_{rfcomm,l2cap}_transport_{connected,disconnected}`）复用 primary 的传输事件 opcode（`BTP_BIP_EV_{RFCOMM,L2CAP}_{CONNECTED,DISCONNECTED}`，0x80–0x83）。Auto-Archive 场景 primary 与 secondary 传输共享同一条 ACL（同地址），上位机无法判断断开的是哪条传输。

### 修复

- **Zephyr 侧**：新增 4 个事件 opcode `BTP_BIP_EV_SECOND_{RFCOMM,L2CAP}_{CONNECTED,DISCONNECTED}`（0xc0–0xc3）+ 4 个事件结构体，4 个 secondary 传输回调改发新 opcode。
- **auto-pts 侧**：`defs.py` 新增 4 个 opcode 常量；`bip.py` 新增 4 个 `_bip_ev_*` 处理函数 + 4 个 `bip_ev_*` wrapper，注册进 `BIP_EV_HANDLERS` / `BIP_EV` 两张表。

### auto-pts 侧的语义决策（重要）

auto-pts 的 `BIPConnection.transport_type` 只有 RFCOMM/L2CAP 两种，**不区分 primary/secondary 传输**，且 primary 与 secondary 共享同一条 ACL。因此：

- **SECOND connected** → 复用 `add_bip_connection()`（幂等，保留已存在的 primary session 状态）。
- **SECOND disconnected** → 故意 no-op（仅日志），**不调用** `remove_bip_connection()`，避免误删仍活着的 primary 传输状态；完整清理交给 primary 断开事件。

这正好修掉了修改前的下游 bug：secondary 断发 primary opcode 时，`_bip_ev_rfcomm_disconnected` 会 `remove_bip_connection` 误删 primary 状态。

### 残留

- 未上线实测。建议回归：Auto-Archive（AAI/ACH）用例中 secondary 传输断开后，auto-pts 的 primary 连接状态仍保留。

## B9 — 缺 secondary 传输断开命令

### 问题

有 `BTP_BIP_SECOND_CONNECT_L2CAP/RFCOMM`（0x41/0x42）但没有对应的 disconnect 命令。`disconnect_l2cap`/`disconnect_rfcomm` 硬编码 `&inst->bip`。上位机在 AAI/ACH 初始化方流程里主动建立 secondary 传输后无法主动拆除。

### 修复

- **Zephyr 侧**：新增命令 opcode `BTP_BIP_SECOND_DISCONNECT_L2CAP/RFCOMM`（0x4b/0x4c）+ 2 个命令结构体 + 2 个 handler（`second_disconnect_l2cap`/`second_disconnect_rfcomm`），作用域 `&inst->second_bip`，用 `find_second_connect_instance()` 查找实例（与 `second_connect_*` 对称），并注册进 handler 表。
- **auto-pts 侧**：`defs.py` 新增 2 个 opcode 常量；`bip.py` `BIP` 字典 + 2 个构造函数（`bip_second_disconnect_l2cap`/`bip_second_disconnect_rfcomm`）。

### 残留

- 构造函数已就绪，暂无 WID/测试用例调用方。待需要主动拆 secondary 传输的用例时使用。

## B10 — `second_conn` 重复绑定造成 `bt_conn` 引用泄漏

### 问题

5 处 `inst->second_conn = bt_conn_ref(conn)` 在赋值前**不检查旧值**。`second_conn` 的 ref 只在传输 disconnected 回调（`bip_second_{rfcomm,l2cap}_transport_disconnected`）里被 unref 清零。若旧 second 传输尚未断开时再次执行任一赋值，旧 `bt_conn` ref 被直接覆盖丢失 → 该 `bt_conn` 引用计数永不归零。

**触发场景**：second_connect 失败后重试（上位机常规操作），或同一实例先后建两次 second 传输。后果：`bt_conn` 对象池（`CONFIG_BT_MAX_CONN`）被耗尽，**跨用例连跑时累积**，最终后续用例连不上。与正在跑的 AAI 用例场景直接相关。

### 5 处位置

| 位置 | 函数 |
|---|---|
| accept 路径 | `bip_second_transport_accept` |
| connect_rfcomm second 分支 | `connect_rfcomm` |
| connect_l2cap second 分支 | `connect_l2cap` |
| second_connect_l2cap | `second_connect_l2cap` |
| second_connect_rfcomm | `second_connect_rfcomm` |

### 修复方案

每处改为**先 drop 旧 ref 再 ref**：

```c
/* Release any stale secondary transport ref before taking a new one. */
bt_conn_drop(&inst->second_conn);
inst->second_conn = bt_conn_ref(conn);
```

`bt_conn_drop()` 是标准公共 API（`include/zephyr/bluetooth/conn.h:1047`）：`*ptr` 非 NULL 则 unref 并置 NULL，NULL 安全。tester 里 `btp_rfcomm.c:75` 已在用，风格一致。

### 正确性（基于用户澄清的四条约束）

用户明确了 BIP 连接的层次约束：**ACL → L2CAP → primary OBEX → second OBEX 严格先后建立，且 primary 与 second 共享同一条 ACL**（`bt_conn_lookup_addr_br` 对同一对端返回同一 `bt_conn` 对象，已在 `conn.h:3335` 与 BIP connect API 签名核实）。据此：

- **不影响 primary connection**：执行任一 `second_conn` 赋值时，primary 必然已建立 → `inst->conn` 已持有该 ACL 的一份独立 ref。`bt_conn_drop(&inst->second_conn)` 只释放 second 那一份，对象 refcount 仍 ≥ 2（`inst->conn` + 底层 ACL），对象不回收，`inst->conn` 不悬垂。
- **正常单次流程**：`second_conn` 旧值为 NULL，`bt_conn_drop` 是 no-op，行为与改前完全一致。
- **重复/重试**：旧 ref 被显式释放而非覆盖丢失，不再泄漏。
- **不动 `inst->conn` 的 NULL 检查分支**：那两处（site 4/5）语义是"仅在无 primary 传输时补设 primary conn"，与本次替换语义不同，保持原样。

### 附带发现（B10 之外的独立问题，未修）

用户的约束暴露出 `second_connect_l2cap` / `second_connect_rfcomm` 里的这段：

```c
if (inst->conn == NULL) {
	inst->conn = bt_conn_ref(conn);
	bt_addr_copy(&inst->address, &cp->address.a);
}
```

按"primary 必先于 second"约束，走到 `second_connect_*` 时 `inst->conn` **必然非 NULL** → 此分支在**合法流程下永不进入**，是死代码；且一旦进入（违反约束的异常调用），会把 primary conn 补设到一条本属于 second 的传输上，语义错误。**不在本次改动范围**，单独记录待后续处理。

### 残留

- 已编译通过。未上线实测。建议回归：同一实例重复 `SECOND_CONNECT_L2CAP`（含失败重试）后，`bt_conn` 池不减少；连跑多个 AAI 用例中途不重启，后续用例仍能建连。

---

## B11 — `bip_inst_get_address()` 无传输时静默返回全零地址

### 问题

```c
/* btp_bip.c:408-416（修改前） */
static void bip_inst_get_address(struct bip_app *inst, bt_addr_le_t *addr)
{
	if (inst != NULL && (inst->conn != NULL || inst->second_conn != NULL)) {
		bt_addr_copy(&addr->a, &inst->address);
		addr->type = BTP_BR_ADDRESS_TYPE;
	} else {
		memset(addr, 0, sizeof(*addr));
	}
}
```

判据要求 `conn` 或 `second_conn` 非空，但预注册实例（`server_register` 在连接前调用,`conn == NULL`）的 `inst->address` 本身一直有效。这个多余的判据会让上位机在预注册期间收到无法归属的全零地址事件,而不是能定位实例的正确地址。

### 核实（竞态窗口排查）

`bip_instance_allocate()`（`btp_bip.c:286`）共 5 处调用点：
- 4 处传入真实 `conn`：分配函数内部同步执行 `bt_addr_copy(&bip_apps[i].address, bt_conn_get_dst_br(conn))`，无窗口。
- 1 处传入 `NULL`（`server_register` 连接前预注册分支,`btp_bip.c:1779`）：`memset` 清零后,下一行（`btp_bip.c:1783`）同步 `bt_addr_copy(&inst->address, &cp->address.a)` 补上；中间没有 `return` 或事件上报路径,handler 单线程同步执行,不存在观察窗口。

结论：`inst->in_use == true` 时 `inst->address` 始终已正确填充，可安全地把判据简化为 `in_use`。

### 修复

```c
static void bip_inst_get_address(struct bip_app *inst, bt_addr_le_t *addr)
{
	if (inst != NULL && inst->in_use) {
		bt_addr_copy(&addr->a, &inst->address);
		addr->type = BTP_BR_ADDRESS_TYPE;
	} else {
		memset(addr, 0, sizeof(*addr));
	}
}
```

单行判据替换，`btp_bip.c:410`。

### 残留

- 已编译通过。未上线实测。建议回归：`SERVER_REGISTER`（连接前预注册）后立即触发一次事件上报（如对端主动发起传输前的窗口），确认地址不再是全零。

---

## B13 — `second_connect` 给 secondary 连接 announce 了 primary 的能力集 —— ✅ 已修（曾还原，现已重新应用）

### 问题

`second_connect()`（`btp_bip.c:2235`，tester 作为 secondary client 连接对端 Referenced-Objects / Archived-Objects 服务器）无条件 announce 全量能力集：

```c
/* 修改前，btp_bip.c:2259-2261 */
bt_bip_set_supported_capabilities(&inst->second_bip, bip_supported_caps);
bt_bip_set_supported_features(&inst->second_bip, bip_supported_features);
bt_bip_set_supported_functions(&inst->second_bip, bip_supported_functions);  /* primary 全量 16 个 */
```

而 tester 自己作为 secondary server 时,SDP 记录声明的能力小得多：`bip_archive_supported_functions`（`btp_bip.c:110`,7 个）、`bip_refobj_supported_functions`（`btp_bip.c:120`,2 个）。同一个 secondary 连接类型,SDP 广播的能力集与 OBEX CONNECT 里实际 announce 的不一致,PTS 若做交叉校验会判失败。

### 订正（范围收窄）

评审建议"应按 `cp->conn_type` 选择对应的 mask"，核实后发现 `caps`（`bip_supported_caps`）和 `features`（`bip_supported_features`）**只有一份全局定义**，没有 archive/refobj 专属的变体——只有 `functions` 才分了三份（primary 16 个 / archive 7 个 / refobj 2 个）。故实际能修、也只需要修的是 `functions` 这一行。

### 曾经的修复（一度还原，现已重新应用）

```c
bt_bip_set_supported_functions(&inst->second_bip,
				type == BT_BIP_2ND_CONN_TYPE_ARCHIVED_OBJECTS
					? bip_archive_supported_functions
					: bip_refobj_supported_functions);
```

`btp_bip.c:2261` 附近。改动本身经 **BIP Stack Spec** 核实是正确的：

- **Table 4.4**（Advanced Image Printing 的 Function overview，Stack Spec p.25）：Referenced Objects 一侧只列了 **GetPartialImage (M)**；GetCapabilities 标 N/A（不适用于该 service，是 Imaging Service 的功能）。
- **Section 6.1.2**（Referenced objects service record，Stack Spec p.68）：Supported Imaging Functions 位图明确只定义了 **Bit 0 = GetCapabilities、Bit 12 = GetPartialImage**，其余 Reserved。

也就是说 `bip_refobj_supported_functions`（`GET_CAPS | GET_PARTIAL_IMAGE`，仅 2 个）本身就是对的，`second_connect()` 理应只 announce 这 2 个，不该照抄 primary 的 16 个全集。

### 为什么曾经还原、现在又重新应用

用 `BIP/AIPR/ADP/BV-01-C` 实测后失败，定位到 `autopts/wid/bip.py` 的 `hdl_wid_38`（"Take action to send GetPartialImage"）：它的通用实现是 **GetImageProperties（解析出 attachment name）→ GetPartialImage**。B13 生效后,tester 的 `inst->second_bip` 只 announce 2 个 function,不含 GetImageProperties,host 侧 `bip_client_get_req_cb()`（`bip.c:1786`）按位检查直接拒绝,连 OBEX 报文都没发出去就返回 FAILED。

进一步查 **BIP Stack Spec**：

- **Section 4.5.13 / 图 4.4（Advanced Image Printing 时序图，p.74）**：Advanced Image Printing 里 GetPartialImage 的文件名**不是**通过 GetImageProperties 解析出来的，而是直接取自 StartPrint 请求体（printer-control object）里的 **`IMG SRC` 标签**——tester 在 `_bip_ev_server_start_print_req` 收到的报文里其实已经带了这个信息（日志可见 `<IMG SRC = "../DCIM/100ABCDE/ABCD0001.JPG">`）。
- 换言之,`hdl_wid_38` 里"先 GetImageProperties 拿名字"这一步，对 AIPR/ADP 这个测试族**根本不该发**——不管发到 primary 还是 secondary 都是错的（primary 也会失败，因为 tester 在这个场景里从未做过 primary client_connect，`inst->client._bip` 一直是 NULL）。这是套用了别的测试族（可能是 Automatic Archive / Remote Camera）的通用实现，没有为 AIPR/ADP 单独处理。

**结论**：B13 本身没问题，挡住测试的是 auto-pts 侧这个更深的既有 bug——两者是独立问题。曾一度还原 `second_connect()` 排除干扰，现已确认改动本身经 Stack Spec 核实正确，**重新应用**。`BIP/AIPR/ADP/BV-01-C` 这条测试仍会因 auto-pts 侧的 `hdl_wid_38` bug 失败，需在 auto-pts 侧单独修复（见下），与本仓库的 tester 代码无关。

### 待办

1. **auto-pts 侧**（不在本仓库/本文档范围,需在 `C:\Zephyr_PTS\auto-pts` 单独跟踪）：
   - 在 `_bip_ev_server_start_print_req`（`autopts/pybtp/btp/bip.py:2692`）或 `image_db.start_print_rsp`（`autopts/ptsprojects/stack/layers/bip.py:884`）解析 printer-control object 里的 `IMG SRC` 标签,存到可供 WID handler 取用的位置。
   - `hdl_wid_38`（`autopts/wid/bip.py:566`）对 AIPR/ADP 系列测试用例,跳过 GetImageProperties,直接用解析出的文件名调 `bip_second_get_partial_image`。
2. **tester 侧**：B13 已重新应用；待 auto-pts 侧修好后跑 `BIP/AIPR/ADP/BV-01-C` 验证不回归。

---

## B12(1)+B12(3)+B12(4) — `sdp_discover()`：端序 + caps 恒 0 + 解析失败被静默忽略

评审原文把 B12 记成三个问题（端序、静态单例并发冲突、caps 恒 0）外加"解析函数返回值被忽略"。本次落地 **端序（子项 1）**、**caps 恒 0（子项 3）**、**解析失败无日志（子项 4）**。**静态单例并发冲突（子项 2）明确不修**——用户已决定暂缓，但问题描述保留在下方，供后续参考。

### 子项 1：端序 — ✅ 已修（用户手动修复）

`sdp_discover()`（`btp_bip.c:1775`）原来 `uuid.val = cp->uuid;` 缺 `sys_le16_to_cpu()`。`cp->uuid` 是 BTP wire 上的小端字段（日志可验证：auto-pts 发 `SDP_DISCOVER` 时字节序是 `1d 11`，小端解释为 `0x111d`，对应 `BIPImagingSvclass.IMAGING_REFOBJS`），`bt_uuid_16.val` 要求本机序。目标板 `mimxrt1170_evk`（Cortex-M7）本身是小端，所以这个遗漏**目前不是活 bug**——本机序恰好等于 wire 小端序，只是没有把"这是小端字段"的语义显式表达出来，换到大端平台才会出问题。

修复：

```c
uuid.val = sys_le16_to_cpu(cp->uuid);
```

与 `btp_sdp.c:307` 已有的处理方式保持一致。已由用户手动修复并确认。

### 子项 2：静态单例并发冲突 — ⏸️ 明确不修（保留问题描述）

`static struct bt_uuid_16 uuid;`（函数内 static，`sdp_discover()` 内）和 `static struct bt_sdp_discover_params sdp_bip_params;`（文件级 static，`btp_bip.c:423`）全局共享。`bt_sdp_discover()` 是异步的，这两个对象的生命周期要跨到 `bip_discover_func()` 回调触发为止；如果两个不同对端并发调用 `sdp_discover`（多实例设计本来就允许），后一个会在前一个还没回调完之前把 `uuid`/`sdp_bip_params` 覆盖掉。

修法（未实施）：把这两个字段挪进 `struct bip_app`（比如 `sdp_uuid` / `sdp_params`），`sdp_discover()` 里通过 `find_instance_by_address()` 找到对应实例后用 `inst->sdp_uuid` / `inst->sdp_params`。

**决定**：用户评估当前 PTS 测试基本是单实例串行触发 SDP discover，这个并发窗口实际不会命中，暂不修复。保留在这里以防后续多实例并发场景下复现。

---

## B12(3)+B12(4) 详情 — `sdp_discover()` 的 caps 恒 0 + 解析失败被静默忽略

### 问题

`bip_discover_func()`（`btp_bip.c:497`）原来：
1. `ev.caps` 硬编码为 0（`btp_bip.c:527`，修改前），从不解析。
2. 4 个属性解析调用（RFCOMM channel、L2CAP PSM、Features、Functions）返回值全部丢弃，解析失败和"对端真填 0"在事件里无法区分。

### 订正（收窄范围，避免误报）

先按 caps/features 是否语义上存在来分类，而不是无差别地"失败就该报"：

- **Stack Spec 6.1.1**（主 Imaging service record）：同时有 Supported Capabilities + Supported Features + Supported Functions。
- **Stack Spec 6.1.2**（Referenced Objects service record）、**6.1.3**（Automatic Archive service record）：**只有** Supported Functions，没有 Capabilities/Features 属性——这两种记录里 caps/features 解析"失败"是**规范内的正常情况**，不是 bug，不该报错、也不该硬性尝试解析。

`bip_discover_func()` 的回调参数 `params`（`const struct bt_sdp_discover_params *`）自带 `params->uuid`，即本次 discover 请求的服务类 UUID，可以和主记录的服务类常量 `BT_SDP_IMAGING_RESPONDER_SVCLASS` 比较，从而判断这次发现的是不是主记录。

### 修复

新增 `bip_sdp_get_caps()`（`btp_bip.c:475`附近，仿照已有的 `bip_sdp_get_functions()`，用 `BT_SDP_ATTR_SUPPORTED_CAPABILITIES`，1 字节）：

```c
static int bip_sdp_get_caps(const struct net_buf *buf, uint8_t *caps)
{
	int err;
	struct bt_sdp_attribute attr;
	struct bt_sdp_attr_value value;

	err = bt_sdp_get_attr(buf, BT_SDP_ATTR_SUPPORTED_CAPABILITIES, &attr);
	if (err != 0) {
		return err;
	}

	err = bt_sdp_attr_read(&attr, NULL, &value);
	if (err != 0) {
		return err;
	}

	if ((value.type != BT_SDP_ATTR_VALUE_TYPE_UINT) || (value.uint.size != sizeof(*caps))) {
		return -EINVAL;
	}

	*caps = value.uint.u8;
	return 0;
}
```

`bip_discover_func()`（`btp_bip.c:497-561`）改为：

```c
is_responder = bt_uuid_cmp(params->uuid,
			    BT_UUID_DECLARE_16(BT_SDP_IMAGING_RESPONDER_SVCLASS)) == 0;

{
	int rfcomm_err = bt_sdp_get_proto_param(result->resp_buf, BT_SDP_PROTO_RFCOMM,
						 &rfcomm_channel);
	int l2cap_err = bip_sdp_get_goep_l2cap_psm(result->resp_buf, &l2cap_psm);

	if (rfcomm_err != 0 && l2cap_err != 0) {
		LOG_DBG("No RFCOMM channel or L2CAP PSM attribute");
	}
}

if (bip_sdp_get_functions(result->resp_buf, &functions) != 0) {
	LOG_DBG("No supported functions attribute");
}

/* caps/features 只在主 Imaging 记录里存在，见上面的 Stack Spec 订正 */
if (is_responder) {
	if (bip_sdp_get_caps(result->resp_buf, &caps) != 0) {
		LOG_DBG("No supported capabilities attribute");
	}
	if (bt_sdp_get_features(result->resp_buf, &features) != 0) {
		LOG_DBG("No supported features attribute");
	}
}
...
ev.caps = caps;   /* 原来硬编码 0 */
```

- `rfcomm_err`/`l2cap_err` 必须先各自无条件调用一次再判断（不能写成 `&&` 短路形式，否则第一个成功时第二个根本不会执行，拿不到该拿的值）。
- transport 只在**两者都失败**时才报——每条记录本来就是 rfcomm/l2cap 二选一，只失败一个是正常的。
- caps/features 只在 `is_responder` 为真时才尝试解析、才在失败时报——避免对 RefObj/Archive 记录（本来就没这两个属性）误报。

### 对 auto-pts 的影响

无需改动。`_bip_ev_sdp_discovered`（`autopts/pybtp/btp/bip.py:2932`，`'<B6sBHBHI'`）已经在正确的字节位置解析 `caps` 字段并存进 `bip.add_sdp_connection(...)`；本次只改了 tester 往这个已有字段里填的**值**（主记录时给真实 caps，RefObj/Archive 时保持 0——这本身就是对的），wire 格式未变。B12(4) 纯 tester 内部日志，同样不影响 auto-pts。

### 残留

- 已编译通过（`west build` 无警告无错误）。未上线实测。建议回归：对主 Imaging 记录 discover 后 `ev.caps` 是非零的真实值；对 RefObj/Archive 记录 discover 后 `ev.caps` 仍是 0（这是对的,不是回归）。
- B12 子项 1（端序）已由用户手动修复并确认。子项 2（静态单例并发冲突）明确不修，见上方说明。

---

## B6 — `server_register` 复用实例 + 地址类型校验

### 问题

`server_register` 无条件新分配实例 → 同地址重复实例，路由二义；且不校验地址类型（评审 B6）。

### 修复方案

`server_register`（`btp_bip.c:1850`）改为：

1. 地址类型校验：`cp->address.type != BTP_BR_ADDRESS_TYPE` → FAILED（与 `connect_rfcomm`/`connect_l2cap` 等一致）。
2. `type` 校验提前（`bip_uuids[type] == NULL` → FAILED）。
3. 复用实例：先 `find_instance_by_address()`，未命中再 `find_preregistered_instance_by_address()`；命中已注册 primary server（`inst->server._bip == &inst->bip`）则**幂等返回 SUCCESS**（策略定为幂等）。
4. `allocated` flag：仅在本次新分配实例且注册失败时 `bip_instance_free()`，复用既有实例失败时不误删。

### 残留

- 已编译通过，未上线实测。建议回归：同一地址重复 `SERVER_REGISTER`（含先连后注册）不产生重复实例。

## B7 — GetPartialImage 的 primary/secondary 不对称

### 问题

- 请求侧：`bip_server_cb`（primary）与 `bip_second_server_cb`（secondary）都注册 `get_partial_image`，且都发 `BTP_BIP_EV_SERVER_GET_PARTIAL_IMAGE_REQ`(0x8d)。
- 响应侧：`get_partial_image_rsp()`(0x1f) 硬编码走 `&inst->second_server`。

若 PTS 在 primary 连接上发 GetPartialImage（primary cb 已注册会被接受），响应会被发到 secondary server → "Invalid state"。

### 核实

GetPartialImage 是 **Referenced-Objects-only** 操作（BIP Stack Spec Table 4.4 / 6.1.2），primary 连接永远不会有该请求。因此 secondary 路径功能是通的（0x8d → auto-pts 回 0x1f → C 侧硬编码 secondary，正确命中）；唯一会坏的是 primary 误注册，但标准 PTS 流程不触发。client 侧已对称（0x1e/0x49 命令、0xa0/0xbe 事件），不涉及。

### 修复方案（评审方案 A）

从 primary server 移除 GetPartialImage 注册：

1. 删除 `BIP_SERVER_REQ_CB(bip_server_get_partial_image, BTP_BIP_EV_SERVER_GET_PARTIAL_IMAGE_REQ)`。
2. 删除 `bip_server_cb` 的 `.get_partial_image = bip_server_get_partial_image,`。
3. 更新 secondary 侧例外注释，说明 primary 不再注册。

效果：primary 连接上的 GetPartialImage 被 host 拒绝（Not Implemented 0x51/0xD1）；0x8d/0x1f 只由 secondary 独享，无混淆。

### 与评审的偏差

评审给两个选项（A 移除 / B 补 SECOND 事件+命令）。选 A：GetPartialImage 是 Referenced-Objects-only，移除即完整修复；B 的补 opcode 是纯对称性重构，需跨仓库改 auto-pts + wire 协议变更，风险大于收益。auto-pts 零改动。

### 残留

- 已编译通过，未上线实测。建议回归：Referenced Objects 用例（如 `BIP/AIPR/ADP/BV-01-C`）中 GetPartialImage 仍正常。

---

## D4 — 运行期可配置能力集（暂缓，未实施）

### 问题

6 个能力变量是文件级静态、编译期写死（`btp_bip.c:77-121`）：`bip_supported_caps`(u8)、`bip_supported_features`(u16)、`bip_supported_functions`(u32)、`bip_max_memory_space`(u64)、`bip_archive_supported_functions`(u32)、`bip_refobj_supported_functions`(u32)。上位机无法按用例调整 SupportedFunctions/Caps/Features。

### 核实（重要：改动其实很简单）

两处消费点都是**运行时解引用变量地址**，不是编译期烘焙：

1. **SDP 记录**：能力属性用指针绑定，如 `{ BT_SDP_ATTR_SUPPORTED_FUNCTIONS, {BT_SDP_TYPE_SIZE(BT_SDP_UINT32), &bip_supported_functions} }`（`btp_bip.c:159-160`），`bt_sdp_data_elem.data` 是 `const void *` 指向变量本身 → SDP 查询读的是当前值。
2. **OBEX CONNECT 声明**：`server_register`（`btp_bip.c:1962-1964`）与 `second_connect`（`btp_bip.c:2367-2372`）在 register/connect 时 `bt_bip_set_supported_*()` 读变量值拷进 `bt_bip`。

因此只需加一个命令写这 6 个变量，SDP 广播与 OBEX CONNECT 声明都会自动反映新值，无需重新注册 SDP、无需改 `bip_responder_attrs`。

### 决定

**暂缓，不实施**。用户反馈目前测试中未遇到需要按用例调整能力集的能力协商/负向用例（`BIP/SR/SGSIT/ATTR/*` 一族）。问题描述与改法保留，待遇到相关用例再落地。

### 改法（供后续参考）

- **C 侧**：`btp_bip.h` 加 `BTP_BIP_SET_CAPABILITIES` 命令 opcode + 结构体（全量覆盖：caps u8 + features u16 + functions u32 + max_memory u64 + archive_functions u32 + refobj_functions u32）；`btp_bip.c` 加 handler 写这 6 个变量。
- **auto-pts 侧**：`defs.py` 加 opcode 常量；`bip.py` 加构造函数；用例 precondition 在 `server_register`/`client_connect` 前下发。

---

## 验证清单

- [x] 编译通过（B1/B2/B4/B6/B7/B8/B9/B10/B11/B12(3/4)/B13；B5 建议单独确认）
- [ ] 上位机对每个变长 opcode 发超长 `data_len`（如 0xFFFF 实带 0 字节），确认返回 `BTP_STATUS_FAILED` 而非崩溃（覆盖 B2）
- [ ] `SERVER_DISCONNECT_REQ` 事件断言 `len == 9 + data_len`（覆盖 B1）
- [ ] 4 轮 `SERVER_REGISTER`→`SERVER_UNREGISTER`，第 4 轮仍成功（覆盖 B5）
- [ ] `READ_SUPPORTED_COMMANDS` 返回位图与 `handlers[]` 表一致（覆盖 B4）
- [ ] 同一实例重复 `SECOND_CONNECT_L2CAP`（含失败重试）后 `bt_conn` 池不减少；连跑多个 AAI 用例中途不重启，后续用例仍能建连（覆盖 B10）
- [ ] `SERVER_REGISTER` 连接前预注册后立即触发事件上报，地址字段非全零（覆盖 B11）
- [ ] `SECOND_CONNECT`（ARCHIVED_OBJECTS / REFERENCED_OBJECTS）后，OBEX CONNECT App-Parameters 的 Supported Functions 与对应 SDP 记录一致（覆盖 B13；`BIP/AIPR/ADP/BV-01-C` 会在此之前卡在 auto-pts 侧 `hdl_wid_38` 的 bug 上，需先修那边才能走到这一步）
- [ ] `SDP_DISCOVER` 主 Imaging 记录后 `ev.caps` 是真实非零值；discover RefObj/Archive 记录后 `ev.caps` 仍是 0（覆盖 B12(3)）
- [ ] Auto-Archive（AAI/ACH）用例中 secondary 传输断开后，auto-pts 的 primary 连接状态仍保留（覆盖 B8）
- [ ] `SECOND_DISCONNECT_L2CAP/RFCOMM` 命令能成功拆除 secondary 传输（覆盖 B9）
- [ ] 同一地址重复 `SERVER_REGISTER`（含先连后注册）不产生重复实例（覆盖 B6）
- [ ] Referenced Objects 用例（如 `BIP/AIPR/ADP/BV-01-C`）中 GetPartialImage 仍正常（覆盖 B7）

---

## 待办（批次 2/3）

**批次 1（B1–B5）已全部落地并编译通过（B5 待硬件回归）。批次 2 已落地 B6、B7、B8、B9、B10、B11、B12（子项 1/3/4）、B13，均已编译通过，待上线实测。**

批次 2 剩余：
- **B12 子项 2（静态单例并发冲突）** — 明确不修（评估当前不会命中），问题描述保留在 B12 详情，供后续参考。

批次 3：B14–B16 健壮性 + C2/C3/C4 结构重构 + D1–D9 参数缺口。其中 **C10（不再直接写 `_bip`）应与 B5 的权宜做法一并在批次 3 收口**；`second_connect_*` 里 `inst->conn == NULL` 死代码（见 B10 末）一并处理。
- **D4（运行期可配置能力集）** — 暂缓（用户目前未遇到能力协商/负向用例），分析结论与改法见 D4 详情。

**跨仓库待办**：`C:\Zephyr_PTS\auto-pts` 的 `autopts/wid/bip.py:hdl_wid_38` 有既有 bug（详见 B13 章节），会导致 `BIP/AIPR/ADP/BV-01-C` 持续失败，与本仓库改动无关，需单独修复。
