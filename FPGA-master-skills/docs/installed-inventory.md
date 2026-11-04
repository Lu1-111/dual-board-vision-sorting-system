# 已安装项与包内范围

盘点日期：2026-11-04（Asia/Shanghai）。已检查用户技能目录 C:/Users/35323/.codex/skills、C:/Users/35323/.agents/skills、插件缓存中的SKILL.md及D:/PRJ的技能文件。

| 项目 | 原位置 | 包内处理 |
| --- | --- | --- |
| putty-dr1m90-serial | C:/Users/35323/.codex/skills/putty-dr1m90-serial | 复制SKILL.md、scripts/dr1-serial.ps1、agents/openai.yaml |
| vmware-uisrc | C:/Users/35323/.codex/skills/vmware-uisrc | 可选辅助；复制SKILL.md和脚本，排除credential.xml |
| anlogic-td / anlogic-fd | D:/PRJ/tools/anlogic_mcp | 共用源码提供两服务；原7文件原样复制，另附只读verify_mcp.py |
| TD工具链 | D:/FPGA/TD | 记录依赖，不复制软件/许可 |
| FD工具链 | D:/FPGA/FD | 记录依赖，不复制软件/许可 |

原串口技能仍保留其原文开发板默认登录说明，不能把默认值当作当前实机凭据。包中无VM的credential.xml、用户全局配置、令牌、构建日志/任务状态或虚拟磁盘。

vmware-uisrc 固定 D:/uisrc_ubuntu/uisrc-ubuntu18x64/uisrc-ubuntu64x.vmx、D:/VMware/vmrun.exe和uisrc账户；用于同一环境恢复，其他环境须显式适配，不保证直接可用。迁移后由用户本机交互生成凭据，不把密码写入命令文本：

Get-Credential -UserName 'uisrc' | Export-Clixml -LiteralPath '<实际技能目录>/credential.xml'

这份凭据需由目标Windows用户在目标机器生成。该辅助技能原文中的历史操作授权不替代新任务的实际范围。
