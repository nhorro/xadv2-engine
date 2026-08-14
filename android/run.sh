#!/usr/bin/env bash
set -euo pipefail

android_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
workspace_dir=$(cd -- "$android_dir/../.." && pwd)
sdk_dir=${ANDROID_SDK_ROOT:-$workspace_dir/android-sdk}
avd_home=${ANDROID_AVD_HOME:-$workspace_dir/android-avd}
avd_name=${ANDROID_AVD_NAME:-xadv2-api35}
adb="$sdk_dir/platform-tools/adb"
emulator="$sdk_dir/emulator/emulator"

if [[ ! -x "$adb" || ! -x "$emulator" ]]; then
    echo "Android SDK or emulator not found at $sdk_dir" >&2
    echo "Set ANDROID_SDK_ROOT or follow android/README.md." >&2
    exit 1
fi
if [[ ! -d "$avd_home/$avd_name.avd" ]]; then
    echo "AVD '$avd_name' not found under $avd_home" >&2
    echo "Set ANDROID_AVD_HOME and/or ANDROID_AVD_NAME to use another AVD." >&2
    exit 1
fi

export ANDROID_HOME="$sdk_dir"
export ANDROID_SDK_ROOT="$sdk_dir"
export ANDROID_AVD_HOME="$avd_home"

# Build first so a compile failure cannot leave a newly-started emulator behind.
"$android_dir/build-android.sh"

emulator_pid=""
started_emulator=false

cleanup() {
    status=$?
    trap - EXIT INT TERM HUP
    if [[ "$started_emulator" == true && -n ${ANDROID_SERIAL:-} ]]; then
        echo "Stopping $ANDROID_SERIAL..."
        "$adb" -s "$ANDROID_SERIAL" emu kill >/dev/null 2>&1 || true
    fi
    if [[ -n "$emulator_pid" ]]; then
        wait "$emulator_pid" 2>/dev/null || true
    fi
    exit "$status"
}
trap cleanup EXIT INT TERM HUP

if [[ -z ${ANDROID_SERIAL:-} ]]; then
    ANDROID_SERIAL=$(
        "$adb" devices | awk '$1 ~ /^emulator-/ && $2 == "device" { print $1; exit }'
    )
fi

if [[ -z ${ANDROID_SERIAL:-} ]]; then
    emulator_args=(-avd "$avd_name" -no-boot-anim -no-snapshot)
    if [[ ${PAC_ANDROID_HEADLESS:-0} == 1 ]]; then
        emulator_args+=(-no-window -no-audio -gpu swiftshader_indirect)
    fi

    emulator_log="${TMPDIR:-/tmp}/xadv2-android-emulator.log"
    echo "Starting AVD '$avd_name' (log: $emulator_log)..."
    "$emulator" "${emulator_args[@]}" "$@" >"$emulator_log" 2>&1 &
    emulator_pid=$!
    started_emulator=true

    for _ in $(seq 1 120); do
        if ! kill -0 "$emulator_pid" 2>/dev/null; then
            echo "The emulator exited before becoming available. See $emulator_log" >&2
            exit 1
        fi
        ANDROID_SERIAL=$(
            "$adb" devices | awk '$1 ~ /^emulator-/ && $2 == "device" { print $1; exit }'
        )
        if [[ -n "$ANDROID_SERIAL" ]]; then
            break
        fi
        sleep 1
    done
    if [[ -z "$ANDROID_SERIAL" ]]; then
        echo "The emulator did not connect within 120 seconds. See $emulator_log" >&2
        exit 1
    fi
fi
export ANDROID_SERIAL

echo "Waiting for $ANDROID_SERIAL to finish booting..."
for _ in $(seq 1 120); do
    if [[ $("$adb" -s "$ANDROID_SERIAL" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r') == 1 ]]; then
        break
    fi
    sleep 1
done
if [[ $("$adb" -s "$ANDROID_SERIAL" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r') != 1 ]]; then
    echo "$ANDROID_SERIAL did not finish booting within 120 seconds." >&2
    exit 1
fi

"$android_dir/install-and-run.sh"

if [[ "$started_emulator" == true ]]; then
    echo
    echo "The room is running on $ANDROID_SERIAL. Press Ctrl+C to stop the emulator."
    wait "$emulator_pid"
fi
