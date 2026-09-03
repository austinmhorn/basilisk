#!/usr/bin/env bash
set -euo pipefail

candidate="${1:?candidate path is required}"
model_candidate="${2:?model candidate path is required}"
installed=/opt/basilisk/bin/BasiliskServer
rollback=/opt/basilisk/bin/BasiliskServer.rollback
staged=/opt/basilisk/bin/BasiliskServer.new
model_installed=/opt/basilisk/models/heuristic-imitation-v3.model
model_staged=/opt/basilisk/models/heuristic-imitation-v3.model.new
had_installed=false

test -s "${candidate}"
test -s "${model_candidate}"
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
    rm -f "${staged}" "${model_staged}" "${candidate}" "${model_candidate}"
}
trap restore_previous ERR

install -o root -g root -m 0755 "${candidate}" "${staged}"
install -D -o root -g root -m 0644 "${model_candidate}" "${model_staged}"
mv -f "${model_staged}" "${model_installed}"
mv -f "${staged}" "${installed}"
systemctl restart basilisk-server.service
listener_ready=false
for _ in {1..100}; do
    systemctl is-active --quiet basilisk-server.service
    if ss -H -ltn |
            grep -Eq '(^|[[:space:]])127\.0\.0\.1:8765([[:space:]]|$)'; then
        listener_ready=true
        break
    fi
    sleep 0.1
done
test "${listener_ready}" = true

rm -f "${candidate}" "${model_candidate}"
trap - ERR
