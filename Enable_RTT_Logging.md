# 通过 SEGGER RTT 采集 Zephyr Tester 日志（固件 + auto-pts 改动说明）

## 背景

Zephyr bluetooth tester 固件使用 BTP 传输占用了串口
（`CONFIG_UART_PIPE=y`、`CONFIG_UART_CONSOLE=n`），因此没有可读的串口 console。
该固件也没有任何 `printf` 输出，所有诊断只能走 Zephyr 的 LOG 子系统
（`LOG_INF`/`LOG_WRN`），并通过 SEGGER RTT 导出，由 auto-pts 采集。

本文档记录为了让 RTT 日志可用而在“固件端”和“auto-pts 端”所做的全部改动。

---

## 一、固件端改动（Zephyr tester）

文件：`zephyr/tests/bluetooth/tester/prj.conf`

新增的临时诊断配置块（调试完成后应移除）：

```kconfig
# --- TEMP DIAGNOSTIC: route Zephyr LOG to SEGGER RTT ---
CONFIG_USE_SEGGER_RTT=y
CONFIG_LOG=y
CONFIG_LOG_BACKEND_RTT=y
CONFIG_LOG_BACKEND_UART=n
CONFIG_LOG_MODE_IMMEDIATE=y

# auto-pts 内置的 RTT 日志采集器读取名为 "Logger" 的专用 up-buffer。
# Zephyr 只有在 CONFIG_LOG_BACKEND_RTT_BUFFER > 0 时才会创建这个命名 buffer；
# 默认值 0 时日志会进入未命名的 "Terminal" buffer 0，auto-pts 采集不到。
# 因此使用 buffer 1。
CONFIG_LOG_BACKEND_RTT_BUFFER=1
CONFIG_LOG_BACKEND_RTT_BUFFER_SIZE=4096

# 确保 GOEP/OBEX 的 warning 级别 trace（OBEXDBG 用 LOG_WRN）被编译进固件。
CONFIG_BT_GOEP_LOG_LEVEL_WRN=y

# 将 RTT 控制块放到普通 SRAM（而非 DTCM），以便 J-Link RTT Viewer 自动检测；
# 否则 Viewer 能连接但看不到数据。
CONFIG_SEGGER_RTT_SECTION_DTCM=n
CONFIG_SEGGER_RTT_SECTION_NONE=y
# --- END TEMP DIAGNOSTIC ---
```

### 各配置项含义

| 配置项 | 作用 |
| --- | --- |
| `CONFIG_USE_SEGGER_RTT` | 启用 SEGGER RTT 支持 |
| `CONFIG_LOG` | 启用 Zephyr LOG 子系统 |
| `CONFIG_LOG_BACKEND_RTT` | 将 LOG 后端接到 RTT |
| `CONFIG_LOG_BACKEND_UART=n` | 关闭 UART 后端（串口被 BTP 占用） |
| `CONFIG_LOG_MODE_IMMEDIATE` | 即时输出，避免缓冲丢日志 |
| `CONFIG_LOG_BACKEND_RTT_BUFFER=1` | 使用命名 up-buffer "Logger"，供 auto-pts 采集 |
| `CONFIG_LOG_BACKEND_RTT_BUFFER_SIZE=4096` | RTT 日志缓冲大小 |
| `CONFIG_BT_GOEP_LOG_LEVEL_WRN` | 编入 GOEP/OBEX 的 WRN trace |
| `CONFIG_SEGGER_RTT_SECTION_NONE=y` | 让 RTT 控制块落在普通 SRAM |

> 注意：即便设置了 `SECTION_NONE`，在 NXP MIMXRT1170 上 `_SEGGER_RTT`
> 控制块仍可能被链接到 DTCM（`0x2000_0000` 段），见下文 auto-pts 端的搜索范围修复。

---

## 二、auto-pts 端改动

### 2.1 RTT 搜索范围（关键修复）

文件：`auto-pts/autopts/rtt.py`

问题：MIMXRT1170（M7）的普通 `.bss` 会落在 DTCM（`0x2000_0000`）。
经 `build/zephyr/zephyr.map` 确认，`_SEGGER_RTT` 控制块被链接到 `0x20004890`（DTCM 内）。
而 J-Link 默认的 RTT 自动搜索**不扫描 DTCM**，导致 Viewer/采集器连上后拿到空日志（0 字节）。

修复：在 `init_jlink()` 中，`RTT.jlink.connect(device_core)` 之后、
`rtt_get_status`/`rtt_start` 之前，允许通过环境变量扩大 RTT 搜索范围：

```python
search_ranges = os.environ.get("AUTOPTS_RTT_SEARCH_RANGES")
if search_ranges:
    ranges = " ".join(part.strip() for part in search_ranges.split(","))
    try:
        RTT.jlink.exec_command(f"SetRTTSearchRanges {ranges}")
    except pylink.errors.JLinkException as err:
        log(f'Failed to set RTT search ranges, err: {err}')
```

对应通过环境变量传入 DTCM 范围：

```
AUTOPTS_RTT_SEARCH_RANGES="0x20000000 0x20000"
```

（起始地址 `0x2000_0000`，长度 `0x2_0000` = 128 KB，覆盖 DTCM 起始区域。）

> 该改动是通用增强，建议保留；即使不再调试 BIP 也无副作用。

### 2.2 调试启动配置（launch.json）

文件：`auto-pts/.vscode/launch.json`

在 "Python Debugger" 配置中新增 `env` 块传入搜索范围（args 不变）：

```json
"env": {
    "AUTOPTS_RTT_SEARCH_RANGES": "0x20000000 0x20000"
}
```

参考的运行参数：
```
-t COM57 --nb -b nxp_evkbmimxrt1170 \
  --device_core MIMXRT1176XXXA_M7 -j 1068735430 \
  --rtt-log -c BIP/AIPI/ADP/BV-01-C
```

### 2.3 bot 配置（config.py）

文件：`auto-pts/autopts/bot/config.py`，在 `z['auto_pts']` 中：

```python
'rtt_log': True,
'debugger_snr': '1068735430',
'device_core': 'MIMXRT1176XXXA_M7',
```

---

## 三、板卡 / J-Link 信息

| 项 | 值 |
| --- | --- |
| 板卡 | NXP MIMXRT1170-EVKB（MIMXRT1176 M7 核） |
| device_core | `MIMXRT1176XXXA_M7` |
| J-Link 序列号 | `1068735430` |
| RT1170 内存映射 | ITCM=`0x0`，DTCM=`0x2000_0000`，OCRAM=`0x2020_0000` |
| RTT 命名 buffer | "Logger"（`CONFIG_LOG_BACKEND_RTT_BUFFER=1` 时创建） |

---

## 四、使用步骤

1. 按第一节修改 `prj.conf`，重新编译并烧录 tester 固件。
2. 确认 `build/zephyr/zephyr.map` 中 `_SEGGER_RTT` 的地址；若落在 DTCM，
   将其范围经 `AUTOPTS_RTT_SEARCH_RANGES` 传入（如 `0x20000000 0x20000`）。
3. 通过 VS Code 调试（已在 launch.json 配 env）或命令行运行 auto-pts，
   附带 `--rtt-log`。
4. 日志将随用例输出，可在对应 `*_iutctl.log` / RTT 采集文件中看到
   `BIPDBG`/`OBEXDBG` 等 trace。
5. 也可用 `JLinkRTTViewer` / `JLinkRTTLogger` 实时查看（需要同样设置搜索范围）。

---

## 五、调试完成后的清理清单

- [ ] 移除 `prj.conf` 中的 “TEMP DIAGNOSTIC” 配置块。
- [ ] 移除 `btp_bip.c` 中的 `BIPDBG` LOG 语句与 `LOG_MODULE_REGISTER`。
- [ ] 移除 `obex.c` 中的 `OBEXDBG` 行。
- [ ] `rtt.py` 的 `SetRTTSearchRanges` 支持为通用增强，可保留。
- [ ] `launch.json` / `config.py` 的诊断项按需回退。
