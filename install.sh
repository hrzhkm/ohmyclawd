#!/usr/bin/env bash
set -euo pipefail

# OhMyClawd daemon installer
# Usage: curl -fsSL https://raw.githubusercontent.com/opariffazman/ohmyclawd/master/install.sh | sudo bash

REPO="opariffazman/ohmyclawd"
BINARY="ohmyclawd-daemon-linux-amd64"
INSTALL_DIR="/usr/local/bin"
SERVICE_NAME="ohmyclawd-daemon"

if [[ "${EUID}" -ne 0 ]]; then
  echo "error: must run as root (sudo)" >&2
  exit 1
fi

TARGET_USER="${OHMYCLAWD_USER:-${SUDO_USER:-}}"
if [[ -z "${TARGET_USER}" ]]; then
  echo "error: set OHMYCLAWD_USER=<user who runs claude code> or invoke via sudo" >&2
  exit 1
fi

if ! id -u "${TARGET_USER}" >/dev/null 2>&1; then
  echo "error: user '${TARGET_USER}' does not exist" >&2
  exit 1
fi

echo "==> fetching latest release..."
DOWNLOAD_URL=$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" \
  | grep "browser_download_url.*${BINARY}" \
  | cut -d '"' -f 4)

if [[ -z "${DOWNLOAD_URL}" ]]; then
  echo "error: could not find ${BINARY} in latest release" >&2
  exit 1
fi

echo "==> downloading ${DOWNLOAD_URL}..."
curl -fsSL -o "/tmp/${BINARY}" "${DOWNLOAD_URL}"
install -m 0755 "/tmp/${BINARY}" "${INSTALL_DIR}/ohmyclawd-daemon"
rm -f "/tmp/${BINARY}"

echo "==> installing systemd service for user '${TARGET_USER}'..."
cat > /etc/systemd/system/${SERVICE_NAME}.service << EOF
[Unit]
Description=ohmyclawd daemon — probes Anthropic for Claude Code utilization
Wants=network-online.target
After=network-online.target

[Service]
Type=simple
ExecStart=${INSTALL_DIR}/ohmyclawd-daemon
Restart=always
RestartSec=5
User=${TARGET_USER}
Group=${TARGET_USER}
Environment=OHMYCLAWD_LISTEN=:8787
Environment=OHMYCLAWD_PROBE_INTERVAL=60s
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=read-only

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable ${SERVICE_NAME}.service
systemctl restart ${SERVICE_NAME}.service

echo "==> done! ohmyclawd-daemon is running"
systemctl --no-pager status ${SERVICE_NAME}.service | head -5
echo ""
echo "usage: curl http://$(hostname).local:8787/usage"
