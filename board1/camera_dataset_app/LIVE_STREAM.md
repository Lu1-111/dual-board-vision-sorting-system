# 相机只读直播接口 v1

相机应用新增 `--stream`，默认关闭。启用时继续使用原有 CaptureWorker 持有的 IMX415 帧；不另开 `/dev/mipicam`，不改变 DMA、单张拍照、UART 分块传输、SHA256 ACK 或引导倒计时。原始分辨率为 1920×1080，JPEG 来自没有屏幕辅助线的原始 QImage。模拟模式明确标为 `simulation`。

板 1 启动参数（先完成当前照片接收和 ACK，再由操作者安全重启应用）：

```sh
./run_pc_camera.sh --stream --stream-address 172.22.203.1 --stream-port 47003 --stream-fps 3 --stream-quality 95
```

默认直播编码 3 FPS，支持 1–5 FPS，质量默认 95。既有 PC-only 照片质量默认 80，与直播设置独立。直播不会自动拍照、保存照片、确认 ACK、控制电机或修改相机曝光。

## GET /v1/frame.jpg

成功响应为 `200 image/jpeg`，包含 `Content-Length`、`Connection: close`、`Cache-Control: no-store`，没有 multipart 流和 keep-alive。相同缓存可能返回同一帧，客户端按 `(session, sequence)` 去重，不能把每次 GET 都当作新帧。

| Header | 含义 |
| --- | --- |
| `X-Camera-Protocol` | `1` |
| `X-Camera-Session` | 每次打开相机重新生成的 UUID，无花括号；进程重启也改变 |
| `X-Frame-Sequence` | 原采集帧号的十进制字符串，可能跳号，避免 JSON 浮点整数截断 |
| `X-Frame-Age-Ms` | 从 GUI 接收完整、已拥有像素的原始帧到建立 HTTP 响应的单调时间差 |
| `X-Captured-Utc` | 原帧 UTC ISO8601 时间，未校时板上可能为 1970，不用于两板时差计算 |
| `X-Frame-Width` / `X-Frame-Height` | 原始像素宽高，当前 1920 / 1080 |
| `X-Camera-Source` | 真机 `imx415`，显式模拟为 `simulation` |

最大 JPEG 为 8 MiB。无首帧或缓存年龄超过 1500 ms 时，最多等待 1200 ms 新编码结果，仍无新鲜帧则 `503 application/json`，`error` 为 `frame_not_ready` 或 `frame_stale`。相机关闭或错误立即返回 503（`camera_stopped`、`camera_error` 等）。失败不返回旧 JPEG。客户端应把传输耗时加入年龄，自己设置结果失效期限；网络断线、超时或 503 应显示离线/过期状态，不能把旧推理显示为实时。

推荐每秒请求 2–5 次、同时最多一个请求、总超时 2500 ms；客户端必须限制实际接收字节数，核对 Content-Length / JPEG 解码尺寸，并拒绝非 `imx415` 来源。图像与推理结果使用同一个 session/sequence，不跨帧拼接。实时分类仅展示模型结果，不能因为置信度高就隐藏已有实拍验证误判事实。

## GET /v1/status.json

只读状态包括 `camera_active`、`source`、`session`、`frame_sequence`、`frame_age_ms`、`frame_ready`、编码计数/耗时、`pending_frames`、`connected_clients` 等。状态请求不会激活编码。`frame_ready=false` 不代表摄像头一定停了：无直播订阅时允许缓存自然过期以节省 CPU。

## 线程和资源边界

`receiveFrame` 仅短暂加锁并持有原 QImage 的共享引用。原 CaptureWorker 已在 DMA 复用前 `.copy()`，直播编码不修改该图像。单独 QThread 进行 JPEG 编码，最多一个正在编码帧、一个可被新帧替换的待编码帧和一个最新 JPEG 缓存，没有逐帧排队的 queued signal。最后一次图片 GET 的编码需求租约为 1200 ms，无请求后停止编码；状态 GET 不续约。关闭/重新打开相机使旧会话的在途编码失效。

HTTP 使用 POSIX 非阻塞 socket 与 Qt 事件通知，无新增 QtNetwork 运行库依赖。最多 4 个客户端，超限连接立即关闭；请求头最多 4096 字节，读取限时 2000 ms，等待新帧 1200 ms，发送限时 3000 ms。每次可写回调最多发送 64 KiB，内核发送缓冲也有上限。每个慢客户端只持有一个共享 JPEG，不积压多帧。只支持 GET 两个固定路径；没有上传、命令执行或相机控制路由。默认只绑定两板直连网口地址；该只读局域网接口没有身份认证。

## 离线回归

`tests/live_stream_fixture.pro` 构建硬件无关发布器，`tests/live_stream_integration.py <fixture>` 检查新鲜帧、去重标识、停止/过期 503、会话变更、无订阅停编码、单槽替换、请求长度/连接数/慢客户端超时。`tests/live_photo_concurrent.py` 在单独的模拟 GUI 上同时请求直播并运行既有引导、两张照片完整性/重传/ACK 测试。模拟测试不能当作真实硬件负载或网络实测；真机验证由主任务在拍照结束后执行。
