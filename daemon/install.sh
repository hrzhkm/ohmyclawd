#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  echo "install.sh must run as root (sudo)" >&2
  exit 1
fi

TARGET_USER="${OHMYCLAWD_USER:-${SUDO_USER:-}}"
if [[ -z "${TARGET_USER}" ]]; then
  echo "Set OHMYCLAWD_USER=<user who runs claude code> or invoke via sudo." >&2
  exit 1
fi

if ! id -u "${TARGET_USER}" >/dev/null 2>&1; then
  echo "user '${TARGET_USER}' does not exist" >&2
  exit 1
fi

SERVICE_NAME="ohmyclawd-daemon"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"

cd "$(dirname "$0")"

# Stop before replacing binary (avoids "Text file busy")
if systemctl is-active --quiet "${SERVICE_NAME}" 2>/dev/null; then
  echo "==> stopping ${SERVICE_NAME}..."
  systemctl stop "${SERVICE_NAME}"
fi

go build -trimpath -ldflags='-s -w' -o ohmyclawd-daemon .
install -m 0755 ohmyclawd-daemon /usr/local/bin/ohmyclawd-daemon

# Preserve existing env vars on upgrade
EXISTING_LISTEN=$(grep -oP '(?<=Environment=OHMYCLAWD_LISTEN=).+' "${SERVICE_FILE}" 2>/dev/null || true)
EXISTING_TOKEN=$(grep -oP '(?<=Environment=OHMYCLAWD_TOKEN=).+' "${SERVICE_FILE}" 2>/dev/null || true)

sed "s|__USER__|${TARGET_USER}|g" systemd/ohmyclawd-daemon.service \
  > "${SERVICE_FILE}"

# Re-apply preserved values
if [[ -n "${EXISTING_LISTEN}" ]]; then
  sed -i "s|Environment=OHMYCLAWD_LISTEN=.*|Environment=OHMYCLAWD_LISTEN=${EXISTING_LISTEN}|" "${SERVICE_FILE}"
fi
if [[ -n "${EXISTING_TOKEN}" ]]; then
  sed -i "s|# Environment=OHMYCLAWD_TOKEN=|Environment=OHMYCLAWD_TOKEN=${EXISTING_TOKEN}|" "${SERVICE_FILE}"
fi

systemctl daemon-reload
systemctl enable "${SERVICE_NAME}.service"
systemctl restart "${SERVICE_NAME}.service"
systemctl --no-pager status "${SERVICE_NAME}.service" | head -10
