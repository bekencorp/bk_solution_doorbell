# Media Preview Example

Unified PC preview server for H264E stream and ISP frame capture over WiFi.

- One `network_transfer` instance; PC selects H264E or ISP by JSON-RPC method.
- CTRL channel: TCP 7100, JSON-RPC.
- VIDEO channel: TCP 7150, H264 ES or ISP NV12 frames.

Build:

```bash
cd projects/media_preview_example
./dbuild.sh make bk7259
```

Boot flow:

- Auto connect STA WiFi on boot.
- Auto start `media_preview` server (1080p 25fps default).

CLI:

```text
ap_cmd media_preview start 1080p 25
ap_cmd media_preview stop
ap_cmd h264e_stream server start 1080p 25
ap_cmd isp_frame server start 1080p 25
```

H264E JSON-RPC examples:

```json
{"jsonrpc":"2.0","method":"h264EScream.getRateControl","params":{},"id":1}
```

```json
{"jsonrpc":"2.0","method":"h264EScream.setRateControl","params":{"rateCtrl":{"bitrate":1200000,"qpMinI":18,"qpMaxI":40,"qpMinP":22,"qpMaxP":44}},"id":2}
```

ISP JSON-RPC examples:

```json
{"jsonrpc":"2.0","method":"ispFrame.start","params":{},"id":1}
```

```json
{"jsonrpc":"2.0","method":"ispFrame.stop","params":{},"id":2}
```
