#!/usr/bin/env bash
set -euo pipefail

# OhMyClawd daemon installer
# Usage: curl -fsSL https://raw.githubusercontent.com/opariffazman/ohmyclawd/master/install.sh | sudo bash
#
# Environment variables (optional):
#   OHMYCLAWD_USER    — user who runs claude code (defaults to SUDO_USER)
#   OHMYCLAWD_LISTEN  — bind address (default: 127.0.0.1:8787, use :8787 for LAN)
#   OHMYCLAWD_TOKEN   — bearer token for /usage and /metrics (optional)

REPO="opariffazman/ohmyclawd"
BINARY="ohmyclawd-daemon-linux-amd64"
INSTALL_DIR="/usr/local/bin"
SERVICE_NAME="ohmyclawd-daemon"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"

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

# --- Preserve existing config on upgrade ---
EXISTING_LISTEN=""
EXISTING_TOKEN=""
EXISTING_PROBE=""
if [[ -f "${SERVICE_FILE}" ]]; then
  EXISTING_LISTEN=$(grep -oP '(?<=Environment=OHMYCLAWD_LISTEN=).+' "${SERVICE_FILE}" 2>/dev/null || true)
  EXISTING_TOKEN=$(grep -oP '(?<=Environment=OHMYCLAWD_TOKEN=).+' "${SERVICE_FILE}" 2>/dev/null || true)
  EXISTING_PROBE=$(grep -oP '(?<=Environment=OHMYCLAWD_PROBE_INTERVAL=).+' "${SERVICE_FILE}" 2>/dev/null || true)
fi

LISTEN="${OHMYCLAWD_LISTEN:-${EXISTING_LISTEN:-127.0.0.1:8787}}"
TOKEN="${OHMYCLAWD_TOKEN:-${EXISTING_TOKEN:-}}"
PROBE="${EXISTING_PROBE:-60s}"

echo "==> fetching latest release..."
DOWNLOAD_URL=$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" \
  | grep "browser_download_url.*${BINARY}" \
  | cut -d '"' -f 4)

if [[ -z "${DOWNLOAD_URL}" ]]; then
  echo "error: could not find ${BINARY} in latest release" >&2
  exit 1
fi

# --- Stop service before replacing binary (avoids "Text file busy") ---
if systemctl is-active --quiet "${SERVICE_NAME}" 2>/dev/null; then
  echo "==> stopping ${SERVICE_NAME}..."
  systemctl stop "${SERVICE_NAME}"
fi

echo "==> downloading ${DOWNLOAD_URL}..."
curl -fsSL -o "/tmp/${BINARY}" "${DOWNLOAD_URL}"
install -m 0755 "/tmp/${BINARY}" "${INSTALL_DIR}/ohmyclawd-daemon"
rm -f "/tmp/${BINARY}"

# --- Build token line (only if set) ---
TOKEN_LINE=""
if [[ -n "${TOKEN}" ]]; then
  TOKEN_LINE="Environment=OHMYCLAWD_TOKEN=${TOKEN}"
fi

echo "==> installing systemd service for user '${TARGET_USER}'..."
cat > "${SERVICE_FILE}" << EOF
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
# Bind address: 127.0.0.1:8787 (localhost) or :8787 (LAN)
Environment=OHMYCLAWD_LISTEN=${LISTEN}
Environment=OHMYCLAWD_PROBE_INTERVAL=${PROBE}
${TOKEN_LINE}
NoNewPrivileges=true
PrivateTmp=false
ProtectSystem=strict
ProtectHome=read-only

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable "${SERVICE_NAME}.service"
systemctl restart "${SERVICE_NAME}.service"

echo "==> done! ohmyclawd-daemon is running"
systemctl --no-pager status "${SERVICE_NAME}.service" | head -5
echo ""
echo "config:"
echo "  listen: ${LISTEN}"
if [[ -n "${TOKEN}" ]]; then
  echo "  token:  set (${#TOKEN} chars)"
  echo "  usage:  curl -H 'Authorization: Bearer <token>' http://$(hostname).local:${LISTEN##*:}/usage"
else
  echo "  token:  none (open access)"
  echo "  usage:  curl http://$(hostname).local:${LISTEN##*:}/usage"
fi
