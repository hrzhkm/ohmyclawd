# Native Screen Recording

Pixel-perfect GIFs from ESP32 framebuffer. No camera. CYD streams BMP frames over WiFi to receiver on your machine.

## Usage

```bash
# start receiver
python3 tools/capture_server.py frames/

# start recording (replace IPs)
curl -X POST "http://<cyd-ip>:8789/capture/start?sink=http://<your-ip>:8788"

# stop
curl -X POST http://<cyd-ip>:8789/capture/stop

# stitch GIF
ffmpeg -framerate 10 -i frames/frame_%06d.bmp -vf scale=480:640:flags=neighbor -loop 0 out.gif
```

## API

| Endpoint | Method | What |
|---|---|---|
| `/capture/start?sink=http://host:8788` | POST | Start. Sink defaults to daemon host. |
| `/capture/stop` | POST | Stop. |
| `/capture/status` | GET | JSON: state, frames, heap, sink. |

## Notes

- Zero overhead idle — no sprite allocated, no perf hit normal mode
- ~1 fps throughput (230KB BMP over WiFi per frame)
- Virtual clock = smooth animation output regardless of transfer speed
- Same network required. No firewall block port 8788
- Allocates 8bpp sprite (75KB) heap on start, freed on stop
