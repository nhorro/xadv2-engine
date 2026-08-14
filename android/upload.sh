#!/usr/bin/env bash
set -euo pipefail

android_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
workspace_dir=$(cd -- "$android_dir/../.." && pwd)
sdk_dir=${ANDROID_SDK_ROOT:-$workspace_dir/android-sdk}
adb="$sdk_dir/platform-tools/adb"

if [[ ! -x "$adb" ]]; then
    echo "adb not found at $adb" >&2
    echo "Set ANDROID_SDK_ROOT or follow ANDROID.md." >&2
    exit 1
fi

export ANDROID_HOME="$sdk_dir"
export ANDROID_SDK_ROOT="$sdk_dir"
"$adb" start-server >/dev/null

if [[ -n ${ANDROID_SERIAL:-} ]]; then
    device_serial=$ANDROID_SERIAL
    if [[ $device_serial == emulator-* ]]; then
        echo "ANDROID_SERIAL points to an emulator ($device_serial)." >&2
        echo "Use ./android/run.sh for emulator testing." >&2
        exit 1
    fi
    if [[ $("$adb" -s "$device_serial" get-state 2>/dev/null || true) != device ]]; then
        echo "ANDROID_SERIAL '$device_serial' is not an authorized, connected device." >&2
        "$adb" devices -l >&2
        exit 1
    fi
else
    device_list=$("$adb" devices -l)
    mapfile -t physical_devices < <(
        printf '%s\n' "$device_list" |
            awk '$1 !~ /^emulator-/ && $2 == "device" { print $1 }'
    )
    if (( ${#physical_devices[@]} == 0 )); then
        if [[ $device_list == *"no permissions"* ]]; then
            echo "ADB can see the phone, but Linux has not granted USB access." >&2
            echo >&2
            echo "On Ubuntu/Debian, install the Android udev rules:" >&2
            echo "  sudo apt install android-sdk-platform-tools-common" >&2
            echo >&2
            echo "Then unplug/reconnect the unlocked phone, accept its trust prompt, and retry." >&2
        else
            echo "No authorized Android phone is connected." >&2
            echo "Unlock the phone, enable USB debugging, accept its trust prompt, then retry." >&2
        fi
        echo >&2
        printf '%s\n' "$device_list" >&2
        echo >&2
        echo "See ANDROID.md for setup and troubleshooting." >&2
        exit 1
    fi
    if (( ${#physical_devices[@]} > 1 )); then
        echo "More than one physical Android device is connected:" >&2
        printf '  %s\n' "${physical_devices[@]}" >&2
        echo "Choose one with: ANDROID_SERIAL=<serial> ./android/upload.sh" >&2
        exit 1
    fi
    device_serial=${physical_devices[0]}
fi
export ANDROID_SERIAL=$device_serial

device_abi=$("$adb" -s "$device_serial" shell getprop ro.product.cpu.abi | tr -d '\r')
device_api=$("$adb" -s "$device_serial" shell getprop ro.build.version.sdk | tr -d '\r')
device_name=$(
    "$adb" -s "$device_serial" shell 'printf "%s %s" "$(getprop ro.product.manufacturer)" "$(getprop ro.product.model)"' |
        tr -d '\r'
)

if [[ $device_abi != arm64-v8a && $device_abi != x86_64 ]]; then
    echo "$device_name uses unsupported ABI '$device_abi'." >&2
    echo "The current APK contains arm64-v8a and x86_64." >&2
    exit 1
fi
if [[ ! $device_api =~ ^[0-9]+$ || $device_api -lt 23 ]]; then
    echo "$device_name uses unsupported Android API '$device_api' (minimum: 23)." >&2
    exit 1
fi

echo "Target: $device_name ($device_serial, $device_abi, API $device_api)"
"$android_dir/build-android.sh"
"$android_dir/install-and-run.sh"

echo
echo "Uploaded and launched on $device_name. The app should rotate to landscape."
