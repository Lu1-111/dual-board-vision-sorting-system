# 资料导航与来源层级

资料根记为 REF，最初位置 D:/PRJ/referrence；工程经验根记为 PRJ，最初为 D:/PRJ。迁移后由实际位置解析，不把历史绝对路径作为安装要求。

| 类别 | REF相对位置 | 用法 |
| --- | --- | --- |
| 总目录 | F3P_DR1M90G资料大纲.txt | 查原有课程分类；文中旧目录名可能与磁盘不同 |
| TD入门/仿真/逻辑分析 | 01_start/01_start_fpga/00_for_starter | TD软件快速入门PDF，按PL技能列出页码核对 |
| 板卡测试/外设/子卡 | 01_start/01_start_fpga/01_board_tset、02_peripheral_demo、03_mlk_card_demo | 先确定连接与板卡版本，再选择对应demo |
| 硬件与DR1手册 | 02_hardware/hardware_MLK_F3P_CZ02_DR1_20250526/hardware_MLK_F3P_CZ02_DR1_20250526 | 01硬件手册、02原理图、06芯片手册/01_DR1；电压/管脚以同版硬件为准 |
| RTL/SDK课程 | 03_demo/3-0_book_all | Verilog、FPGA基础、UDP、ISP、SDK入门/高级 |
| Linux | 03_demo/3-4_ex_soc_linux/ebook | 基础入门与驱动开发；已有txt为检索辅助 |
| Buildroot与启动 | 01_start/02_start_Linux/03_restore_factory、04_secondary_developmemt | 恢复教程可能覆盖介质，先辨识目标和当前授权 |
| NPU | 01_start/02_start_Linux/05_application_demo/05-5_NPU | NPU开发PDF/txt、demo.zip、soc_prj.zip |
| 当前MCP实现 | PRJ/tools/anlogic_mcp | 接口能力来源，不是器件手册 |
| 本地部署经验 | PRJ/sdk | 只代表所列模型/runtime/板卡的测量，不外推为厂商保证 |

来源分三层：厂商/板卡资料、配套源码与当前工具实现、局部项目实测。遇到冲突保留两边来源并设计最小验证，不以旧教程或一条日志替代实测。PDF表格/原理图的行列和连线不能只凭抽取txt判断。

套件顶层 docs/reference-catalog.json 记录原参考文档清单、大小和SHA256。原PDF、demo和完整工程没有重复搬入此技能包；专题references已保存摘要及章节定位。需要修改具体引脚、IP或部署模型时仍需相应版本原件。
