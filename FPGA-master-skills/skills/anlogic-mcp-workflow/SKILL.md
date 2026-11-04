---
name: anlogic-mcp-workflow
description: 使用已连接的 anlogic-td / anlogic-fd MCP 检查安路工程、运行 TD 或 FD 构建、读取报告与管理后台任务。用于已有工程的自动化；不替代 BSP 生成、GUI 单步调试或独立烧录工具。
---

# TD / FD MCP 工作流

以当前服务暴露的工具schema为准。此说明对应套件 mcp/anlogic_mcp/server.py 的11个TD工具和11个FD工具；参数与限制来自该实现。

## 发现与核对

1. 调用对应 environment_info 检查工作区和可执行文件。TD/FD历史位置是 D:/FPGA/TD、D:/FPGA/FD；缺失时报告实际路径。
2. 在目标子目录调用 list_projects；返回truncated时缩小目录范围再查。TD传 .al 文件，FD传包含 .project/.cproject/Makefile 的目录。用 project_info 核对器件、速度、top、源文件缺失、运行名称或BSP引用。
3. 构建前读目标Tcl/Makefile与配置。它们按当前用户权限执行代码；工作区路径限制不是代码沙箱。用户要求构建本身即可授权正常生成产物，不额外发明审批。

## TD构建

build_project 的 stage 为 elaborate、synthesis、implementation、bitstream。默认仅elaborate，默认数据库dr1_90.db、运行syn_1/phy_1；从project_info确定真实run并核对 TD/arch 中的器件数据库，其他系列不可照搬DR1。

示例参数（路径为本机示例，须替换为已发现工程）：

```json
{"project":"soc_prj/soc_prj.al","stage":"elaborate"}
```

rebuild=true 会重置所选run结果，只有需要重新构建时使用。bitstream阶段会运行实现并save_best_bits，保留其他工具版本差异。已有可信Tcl可用run_tcl，script相对工作区，working_directory单独指定；不要凭空编造TD命令。

## FD构建

核对architecture：aarch64 / aarch32 / aarch64_linux / riscv；先用toolchain_version确认编译器。build_project从.cproject推导BSP，也可明确sdk_root。CHIP、ARMv8_STATE、ARMv8_EL、DOWNLOAD、TARGET需与实际工程匹配；允许的variables仅TARGET/CHIP/ARMv8_STATE/ARMv8_EL/DOWNLOAD/V/DEBUG。

```json
{"project":"soc_sdk/soc_fsbl","sdk_root":"soc_sdk/soc_base","architecture":"aarch64","jobs":4}
```

这只是已生成Makefile的构建，不会从HPF生成BSP。FD构建目录/BSP/编译器路径不能带空格或被实现拒绝的shell字符。dry_run=true是make -n，变量展开和递归make仍可能执行代码；target=clean会删除构建输出。

## 后台任务与验收

构建返回job_id后保留ID，用job_status查看终态，read_job_log按next_offset续读或offset=-1读尾部。max_bytes最大65536；文本按字节偏移，不是行号。终态succeeded/failed/cancelled/timed_out/interrupted；客户端断开后任务仍可能继续，用list_jobs恢复，不重复发起同一构建。

同一TD工作目录、同一FD BSP的并发构建会被锁拒绝。需要终止时cancel_job只作用于MCP启动的指定任务树，收到cancellation_requested后继续核对终态。

succeeded表示执行流程结束；TD退出码0不足以判断成功，服务还核对成功标记，但这仍不证明时序收敛或板端功能。用list_reports找到本次时间对应的时序/资源/错误报告和产物，必要时在文件系统中计算SHA256，关联源码和运行配置。

安装与迁移见 [接入说明](references/setup.md)。
