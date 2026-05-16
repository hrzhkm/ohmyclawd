# ohmyclawd daemon

Runs inside the Proxmox VM where Claude Code is used. Probes the Anthropic
`/v1/messages` endpoint every 60 s with a 1-Haiku-token call and parses the
`anthropic-ratelimit-unified-*` response headers into a compact JSON that the
ESP32 firmware polls over LAN.

## Install (systemd, recommended)

```bash
sudo OHMYCC_USER=youruser ./install.sh
```

This builds the static binary, copies it to `/usr/local/bin/ohmyclawd-daemon`,
installs the systemd unit substituting `OHMYCC_USER=youruser`, and starts the
service. The daemon runs as `youruser` so it can read `~/.claude/.credentials.json`.

## Configuration

All knobs are environment variables on the systemd unit:

| Var                     | Default                                  |
|-------------------------|------------------------------------------|
| `OHMYCC_LISTEN`         | `:8787`                                  |
| `OHMYCC_PROBE_INTERVAL` | `60s`                                    |
| `OHMYCC_CREDS_PATH`     | `~/.claude/.credentials.json`            |
| `OHMYCC_ANTHROPIC_URL`  | `https://api.anthropic.com/v1/messages`  |
| `OHMYCC_LOG_LEVEL`      | `info`                                   |

Edit `/etc/systemd/system/ohmyclawd-daemon.service` and run
`systemctl daemon-reload && systemctl restart ohmyclawd-daemon`.

## Endpoints

- `GET /usage` — JSON `{s, sr, w, wr, st, ok, ts}`; supports `If-None-Match`.
- `GET /healthz` — `200 OK` with body `ok`.
- `GET /metrics` — Prometheus exposition.

## Fake mode

```bash
ohmyclawd-daemon --fake
```

Skips Anthropic calls and serves a scripted utilization curve. Useful for
exercising the firmware in isolation.

## Tests

```bash
go test -race -count=1 ./...
```
