# 安路 TD / FD MCP

两个本地 stdio MCP 服务：`anlogic-td` 和 `anlogic-fd`。直接调用已安装的安路命令行工具，无需 API Key、网络服务或点击 GUI。

默认工作区为仓库根目录，TD 为 `D:\FPGA\TD`，FD 为 `D:\FPGA\FD`。适配本机 TD Release_2026.1_SP2、FD 2026.1 和 DR1M90 工程。每个服务提供 11 个工具。

## 安装与接入

需要 Windows、Python 3.10+、TD、FD。首次安装会从 PyPI 下载 Python 依赖，运行时使用独立 `.venv`。

```powershell
& D:\PRJ\tools\anlogic_mcp\install.ps1 -Register
```

此命令创建虚拟环境、安装依赖，并通过 `codex mcp add` 注册两个服务。已有同名且指向其他程序的配置会报告冲突。仅安装、不注册时省略 `-Register`。

参数支持 `-Python`、`-Workspace`、`-TdHome`、`-FdHome`。迁移目录后重新安装并更新注册路径。配置变量也可以通过环境设置：

| 变量 | 用途 |
| --- | --- |
| `ANLOGIC_WORKSPACE` | 工程访问根目录 |
| `ANLOGIC_TD_HOME` | TD 安装根目录 |
| `ANLOGIC_FD_HOME` | FD 安装根目录 |
| `ANLOGIC_MCP_STATE` | 任务及日志目录，默认此目录下 `.state` |

按 [OpenAI 官方 MCP 接入文档](https://developers.openai.com/codex/mcp/)使用本地 stdio 注册。查看配置：

```powershell
codex -c 'service_tier="fast"' mcp get anlogic-td
codex -c 'service_tier="fast"' mcp get anlogic-fd
```

当前聊天未刷新工具时，重启 Codex 后新建聊天，再调用 `environment_info` 确认。配置已注册不等于当前聊天的工具目录已刷新。

本机 npm Codex CLI 比桌面端旧，不能解析桌面配置中的 `service_tier="default"`。安装脚本和上述命令使用单次参数覆盖来读取配置；不会调用模型，也不会修改已保存的服务等级。

其他支持 stdio 的 MCP 客户端也可使用：

```json
{
  "mcpServers": {
    "anlogic-td": {
      "command": "D:\\PRJ\\tools\\anlogic_mcp\\.venv\\Scripts\\python.exe",
      "args": ["D:\\PRJ\\tools\\anlogic_mcp\\server.py", "--tool", "td"]
    },
    "anlogic-fd": {
      "command": "D:\\PRJ\\tools\\anlogic_mcp\\.venv\\Scripts\\python.exe",
      "args": ["D:\\PRJ\\tools\\anlogic_mcp\\server.py", "--tool", "fd"]
    }
  }
}
```

## 使用方式

可直接对助手说：

- “用 TD MCP 检查 soc_prj 的器件、顶层和缺失源文件。”
- “用 TD MCP 展开 soc_prj/soc_prj.al，然后查看错误日志。”
- “用 FD MCP 构建 soc_sdk/soc_fsbl，查看是否成功。”
- “列出正在运行的构建任务，取消指定任务。”

两边都有这些工具：

| 工具 | 功能 |
| --- | --- |
| `environment_info` | 检查安装路径、可执行文件、工作区 |
| `list_projects` | 发现 TD `.al` 或 FD `.project` 工程 |
| `project_info` | 读取器件、顶层、源文件、运行配置或 BSP/Make 设置 |
| `list_reports` | 查找报告、日志和 bit/ELF/bin/map 产物 |
| `read_text_file` | 分段读取工程文本，字节偏移分页 |
| `list_jobs` / `job_status` | 查询历史任务与状态 |
| `read_job_log` | 读取后台日志，`offset=-1` 查看末尾 |
| `cancel_job` | 取消本 MCP 启动的指定任务及其子进程 |

TD 专用工具：

- `build_project`：`stage` 支持 `elaborate`、`synthesis`、`implementation`、`bitstream`。默认仅展开；从 `.al` 读取器件和顶层。其他器件系列需指定匹配的 `device_database`。`rebuild=true` 会重置所选运行结果。
- `run_tcl`：运行已有可信 `.tcl` 文件。相对路径以工作区为基准，`working_directory` 控制脚本工作目录。适合已有自动化流程及更多 TD 命令。

FD 专用工具：

- `toolchain_version`：检查 AArch64、AArch32、AArch64 Linux 或 RISC-V 编译器版本。
- `build_project`：运行 FD 自带 Make 和交叉编译器，自动从 `.cproject` 推导 BSP。支持 `all`、`clean`、`dry_run`、并行度及指定构建变量。`clean` 删除构建产物。`dry_run` 是 Make 的 `-n`，Makefile 的变量展开和递归 Make 仍可能执行命令。

例如 FD 构建参数：

```json
{"project":"soc_sdk/soc_fsbl","sdk_root":"soc_sdk/soc_base","architecture":"aarch64","jobs":4}
```

构建立即返回 `job_id`，用 `job_status` 轮询，再用 `read_job_log` 读取结果。终态包括 `succeeded`、`failed`、`cancelled`、`timed_out`、`interrupted`。任务和日志在客户端重连后仍可查询；后台工作进程可继续运行。同一 TD 工作目录或同一 FD BSP 的并发构建会被锁拒绝。

## 结果含义和范围

TD 启动器可能错误地返回退出码 0，因此服务同时检查 Tcl 成功标记；脚本明确 `exit 0` 视为成功。`succeeded` 表示命令流程结束，不证明时序收敛、位流正确或开发板运行正常，仍应检查报告。自定义 Tcl 若自行吞掉错误并返回成功，服务无法推断真实设计质量。

服务封装现有工程的 TD 构建与 FD Make 流程。没有实现 GUI 操作、从硬件描述重新生成 BSP、在线单步调试或专门的烧录接口。FD 原厂 Makefile 对路径敏感，构建目录、BSP 和工具链路径需避免空格及 shell 特殊字符。

只读接口限制在配置工作区内。Tcl 和 Makefile 属于可执行代码，以当前 Windows 用户权限运行；路径限制不是代码沙箱，仅运行可信工程。构建接口可能改写工程或产物，已经声明为写入型 MCP 工具。

`.state` 保存日志、Tcl 包装脚本和任务状态，不进入 Git。任务请求只保存工具所需的环境覆盖，不保存宿主环境中的令牌。客户端断开不会自动取消任务；请通过 `cancel_job` 取消。Windows Job Object 用于工作进程异常退出时清理后代进程。

## 验证

```powershell
Set-Location D:\PRJ\tools\anlogic_mcp
.\.venv\Scripts\python.exe -m unittest test_server -v
.\.venv\Scripts\python.exe smoke_test.py
```

单元测试覆盖路径越界、非标准 TD 元数据、退出码/成功标记、取消子进程、超时、并发锁和日志分页。冒烟测试通过 [MCP 官方 Python SDK](https://github.com/modelcontextprotocol/python-sdk)完成真实 stdio 握手与工具调用，并运行 TD 成功/失败 Tcl、FD GCC 编译和客户端重连。临时测试产物留在 `.state`，不替换工程位流或现有 ELF。

2026-10-12 本机验证：7 项单元测试通过；两个已注册服务均完成握手并返回各 11 个工具；TD 成功/报错脚本识别通过；FD 小型 C 文件编译为 ELF 目标文件通过；实际 `soc_prj/soc_prj.al` 展开通过；实际 `soc_sdk/soc_fsbl` Make 预演通过。完整布局布线、位流生成和完整 SDK 重建未在本次集成测试中运行。

移除 Codex 注册：

```powershell
codex -c 'service_tier="fast"' mcp remove anlogic-td
codex -c 'service_tier="fast"' mcp remove anlogic-fd
```
