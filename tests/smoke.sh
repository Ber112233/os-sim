#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"

make

cleanup_pid=""
cleanup_log=""
cleanup() {
    if [ -n "$cleanup_pid" ] && kill -0 "$cleanup_pid" 2>/dev/null; then
        kill -TERM "$cleanup_pid" 2>/dev/null || true
        wait "$cleanup_pid" 2>/dev/null || true
    fi
    if [ -n "$cleanup_log" ]; then
        rm -f "$cleanup_log"
    fi
}
trap cleanup EXIT INT TERM

run_case() {
    algorithm=$1
    shift
    cleanup_log=$(mktemp)

    OS_SIM_MIN_MS=200 OS_SIM_MAX_MS=200 ./os_sim "$algorithm" "$@" \
        >"$cleanup_log" 2>&1 &
    cleanup_pid=$!
    sleep 0.1

    kill -USR1 "$cleanup_pid"
    sleep 0.05
    kill -USR1 "$cleanup_pid"
    sleep 0.05
    kill -USR1 "$cleanup_pid"
    sleep 1.0
    kill -TERM "$cleanup_pid"
    wait "$cleanup_pid"
    cleanup_pid=""

    if ! grep -q "Shutdown complete. Created: 3 | Finished: 3 | Interrupted: 0" \
        "$cleanup_log"; then
        echo "FALLO: $algorithm" >&2
        cat "$cleanup_log" >&2
        exit 1
    fi
    rm -f "$cleanup_log"
    cleanup_log=""
    echo "OK: $algorithm"
}

run_case fcfs
run_case sjf
run_case srtf
run_case rr 100

echo "Todas las pruebas smoke pasaron."
