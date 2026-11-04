# 板间异常图片与串口桥接

程序：`/opt/evidence-bridge/evidence_bridge`，两块板均已部署；板2网络接收服务开机启动。配置脚本：`/etc/init.d/S41monitor-link`。板1为`eth0/172.22.203.1/30`，板2为`eth1/172.22.203.2/30`；板2MAC设为`02:78:a3:01:02:04`，解决两板出厂MAC重复。

仅将当前直连PS网口从DHCP管理中排除，不配置互联网默认路由。接口TCP47002仅监听板2专用IP；两板共享`/etc/monitor-link.token`用于鉴权。该通道用于两板专线直连，没有TLS，不应直接暴露到公共网络。

## 第一块板的视觉程序如何提交

视觉程序确认异常时，保存一张JPEG，并取出**对应CAN告警**的会话ID、事件ID和类型，然后执行：

```sh
/opt/evidence-bridge/evidence_bridge \
  --send /实际路径/alarm.jpg \
  --session 123456 --event 8 --type 2 \
  --source camera --captured-at '实际拍摄时间'
```

`--type`：1颜色不符、2物料滞留、3缺料。成功返回`ok:true`与SHA256。非0退出码表示失败。

**以上数字只是接口示例，不能直接用于真实告警。** 必须与CAN发送程序的session/event/type一致。尚未完成视觉训练，目前没有自动判异常/自动抓拍这一环；需要在后续视觉程序报警分支调用本接口。正常状态不调用。

CAN事件可能晚于网线图片到达，服务会返回`unknown_event_retry_after_CAN`，发送端应保留原JPEG和事件ID，短暂等待后重试，不要为重试创建新事件。同一事件只接受一张不可变图片，相同SHA256重发可确认，不同图片覆盖会拒绝。

联调时可运行 `evidence_bridge --events` 查看板2当前已登记的演示告警，并使用`--source test`发送已有照片。界面会明确标记测试图。**这不是板1通过CAN发来的真实告警**；当前CAN仍为板2内部回环。真实系统不能拿“最新告警”随意匹配照片。

限制：JPEG≤512KiB，宽高各≤4096，总像素≤800万；建议640×360或960×540、100KB左右。最多保留100条UI告警及仍在分析的图片。超过限额会拒绝提交，不会截断图片。

## 协议

TCP连接为单请求/单响应的UTF-8 JSON行，15秒超时，最大720000字节。身份字段：`session_id`/`event_id`为非零32位数，`alarm_type`为1..3。

提交示意（不含实际密钥/图片）：

```json
{
  "op": "put_image",
  "token": "读取本机/etc/monitor-link.token",
  "session_id": 123456,
  "event_id": 8,
  "alarm_type": 2,
  "is_anomaly": true,
  "source": "camera",
  "captured_at": "实际拍摄时间",
  "sha256": "JPEG字节的SHA256小写十六进制",
  "image_b64": "JPEG的base64"
}
```

板2通过原子文件与UI交换数据，路径`/var/lib/monitor-ui/evidence/`。正常帧、未知告警、错误会话、错误类型、哈希不符、破损图片均拒绝。网络端不能创建AI结果；电脑串口端不能随意执行shell或访问任意文件。

PC以串口启动`serial_bridge.sh`进入RPC模式。响应前缀`@MONITOR@`，通过`rpc_id`匹配；`poll`取得分析队列，`image_chunk`每次1536字节，`complete`按请求ID返回结果。`QUIT`恢复原串口设置。运行期间不能用其他终端同时占用串口。

## 构建和维护

WSL运行`board/build_app.sh`和`board/build_evidence.sh`，使用既有ARM64工具链/Qt5.15.8；JPEG插件随接收程序部署，不改HDMI时钟、触摸驱动或电机代码。

板2重启UI：`/etc/init.d/S99monitor-ui restart`。重启图片接收：先`/etc/init.d/S41monitor-link stop`再`start`。这不会给机械臂发送控制命令。

本次部署前备份目录：板1`/root/before-evidence-4611`，板2`/root/before-evidence-3348`。恢复旧UI需停止UI服务，再复制板2备份的`monitor_ui`回`/opt/monitor-ui/monitor_ui`并启动服务。

本次部署包含两板专用鉴权文件，属于本机部署产物，不应公开提交；分享源码时重新生成本地token。历史显示驱动/启动镜像没有修改。
