#!/usr/bin/env bash
set -euo pipefail

candidate="${1:?candidate path is required}"
installed=/opt/basilisk/bin/BasiliskServer
rollback=/opt/basilisk/bin/BasiliskServer.rollback
staged=/opt/basilisk/bin/BasiliskServer.new
had_installed=false

test -x "${candidate}"
if [[ -f "${installed}" ]]; then
    cp --preserve=mode,ownership,timestamps "${installed}" "${rollback}"
    had_installed=true
fi

restore_previous() {
    if [[ "${had_installed}" == true && -f "${rollback}" ]]; then
        cp --preserve=mode,ownership,timestamps "${rollback}" "${installed}"
        systemctl restart basilisk-server.service || true
    else
        rm -f "${installed}"
        systemctl stop basilisk-server.service || true
    fi
    rm -f "${staged}" "${candidate}"
}
trap restore_previous ERR

install -o root -g root -m 0755 "${candidate}" "${staged}"
mv -f "${staged}" "${installed}"
systemctl restart basilisk-server.service
systemctl is-active --quiet basilisk-server.service
ss -H -ltn | grep -Eq '(^|[[:space:]])127\.0\.0\.1:8765([[:space:]]|$)'

rm -f "${candidate}"
trap - ERR
