# MCP接入与可移植性

本包包含现有服务的server.py、worker.py、install.ps1、requirements.txt、单元测试、原冒烟测试和README；未带.venv、.state、日志、运行中的任务、全局Codex配置或TD/FD安装程序。

先将整个包解压到固定位置，例如 D:/AnlogicKit。需要Windows、Python 3.10+、TD/FD及所需设备支持。原环境为TD Release_2026.1_SP2、FD 2026.1（来自现有MCP README的环境记录）；以目标机实际版本为准。

- 在线安装并注册：在包内 mcp/anlogic_mcp 目录运行 ./install.ps1 -Python python -Workspace D:/PRJ -TdHome D:/FPGA/TD -FdHome D:/FPGA/FD -Register。
- 只准备Python环境：省略-Register。
- 注册后重启/刷新客户端工具目录，通过environment_info核对。若同名服务已指向其他路径，安装器会拒绝替换；本机现有服务可继续用，不必为归档副本另注册。
- requirements.txt是原始直接依赖；requirements-resolved.txt记录打包时环境版本，供复现参考，未内附Python wheels，因此首次安装需可用Python包源。
- 本包verify_mcp.py仅握手并查询环境/工具，不构建工程。原smoke_test.py会运行TD Tcl和FD小型C编译，还硬编码soc_prj/soc_prj.al、soc_sdk/soc_fsbl等本机工程；它不是通用安装验收脚本。
- 保留安装目录；注册路径指向该处，移动目录后需显式调整对应两项MCP配置。

当前源码依赖pywin32用于Windows Job Object，它由MCP的Windows依赖传递提供；若目标安装缺失，先检查该解释器的依赖安装与平台，不能只复制server.py然后宣称已完成安装。

外部客户端可参考 config/mcp-stdio.example.json；Codex可参考 config/codex-mcp.example.toml。模板只列这两个服务与必要环境值，没有复制其他插件或用户配置。__KIT_DIR__、__WORKSPACE__、__TD_HOME__、__FD_HOME__是必须替换的路径占位符，路径推荐使用正斜杠。

## 来源

- 原服务 D:/PRJ/tools/anlogic_mcp/README.md 与install.ps1。
- server.py:22–36 默认路径/架构、212–393 工具定义、worker.py:45–129 后台生命周期。
- 2026-11-04 environment_info 实查：D:/FPGA/TD/bin/td_commands_prompt.exe、D:/FPGA/TD/bin/td.exe、FD make、fd.exe以及4类GCC均存在。存在不等于工程构建已成功。
