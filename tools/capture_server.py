#!/usr/bin/env python3
"""
Capture receiver for ohmyclawd native screen recording.
Listens for BMP frames POSTed by the ESP32 and saves them sequentially.

Usage:
    python3 capture_server.py [output_dir]

Then trigger recording from the ESP32:
    curl -X POST "http://<cyd-ip>:8789/capture/start?sink=http://<this-machine-ip>:8788"
    curl -X POST http://<cyd-ip>:8789/capture/stop

Check status:
    curl http://<cyd-ip>:8789/capture/status

Stitch frames into GIF:
    ffmpeg -framerate 10 -i output/frame_%06d.bmp -vf scale=480:640:flags=neighbor -loop 0 out.gif
"""

import os
import sys
from http.server import HTTPServer, BaseHTTPRequestHandler

output_dir = sys.argv[1] if len(sys.argv) > 1 else "frames"
os.makedirs(output_dir, exist_ok=True)


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path == "/frame":
            length = int(self.headers.get("Content-Length", 0))
            frame_num = self.headers.get("X-Frame", "0")
            data = self.rfile.read(length)
            path = os.path.join(output_dir, f"frame_{int(frame_num):06d}.bmp")
            with open(path, "wb") as f:
                f.write(data)
            print(f"  frame {frame_num} ({len(data)} bytes)", flush=True)
            self.send_response(200)
            self.end_headers()
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        pass


class ReusableServer(HTTPServer):
    allow_reuse_address = True


if __name__ == "__main__":
    server = ReusableServer(("0.0.0.0", 8788), Handler)
    print(f"Capture receiver listening on :8788, saving to {output_dir}/")
    print("Waiting for frames...")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        n = len([f for f in os.listdir(output_dir) if f.endswith(".bmp")])
        print(f"\n{n} frames captured.")
        print(f"Stitch: ffmpeg -framerate 10 -i {output_dir}/frame_%06d.bmp -vf scale=480:640:flags=neighbor -loop 0 out.gif")
