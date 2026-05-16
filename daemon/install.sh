#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  echo "install.sh must run as root (sudo)" >&2
  exit 1
fi

TARGET_USER="${OHMYCC_USER:-${SUDO_USER:-}}"
if [[ -z "${TARGET_USER}" ]]; then
  echo "Set OHMYCC_USER=<user who runs claude code> or invoke via sudo." >&2
  exit 1
fi

if ! id -u "${TARGET_USER}" >/dev/null 2>&1; then
  echo "user '${TARGET_USER}' does not exist" >&2
  exit 1
fi

cd "$(dirname "$0")"
go build -trimpath -ldflags='-s -w' -o ohmyclawd-daemon .
install -m 0755 ohmyclawd-daemon /usr/local/bin/ohmyclawd-daemon

sed "s|__OHMYCC_USER__|${TARGET_USER}|g" systemd/ohmyclawd-daemon.service \
  > /etc/systemd/system/ohmyclawd-daemon.service

systemctl daemon-reload
systemctl enable ohmyclawd-daemon.service
systemctl restart ohmyclawd-daemon.service
systemctl --no-pager status ohmyclawd-daemon.service | head -10
