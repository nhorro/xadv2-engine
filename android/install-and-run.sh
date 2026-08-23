#!/usr/bin/env bash
set -euo pipefail

android_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk_dir=${ANDROID_SDK_ROOT:-$android_dir/sdk}
adb="$sdk_dir/platform-tools/adb"
build_variant=${PAC_ANDROID_VARIANT:-debug}
apk="$android_dir/app/build/outputs/apk/$build_variant/app-$build_variant.apk"
aapt="$sdk_dir/build-tools/35.0.0/aapt"

if [[ ! -x "$adb" ]]; then
    echo "adb not found at $adb" >&2
    exit 1
fi
if [[ ! -f "$apk" ]]; then
    "$android_dir/build.sh" ${PAC_ANDROID_GAME_DIR:+"$PAC_ANDROID_GAME_DIR"}
fi
if [[ ! -x "$aapt" ]]; then
    echo "aapt not found at $aapt" >&2
    exit 1
fi
package_name=$("$aapt" dump badging "$apk" |
    sed -nE "s/^package: name='([^']+)'.*/\1/p" | head -n 1)
if [[ -z "$package_name" ]]; then
    echo "Could not determine the application id in $apk" >&2
    exit 1
fi
if ! "$adb" get-state >/dev/null 2>&1; then
    echo "No Android device or emulator is connected." >&2
    echo "See android/README.md for the emulator command." >&2
    exit 1
fi

"$adb" install -r "$apk"
"$adb" shell am force-stop "$package_name"
"$adb" logcat -c
"$adb" shell am start -n "$package_name/android.app.NativeActivity"

for _ in {1..80}; do
    marker=$("$adb" logcat -d -s xadv2-android:I xadv2-engine:I '*:S' | tail -20)
    if [[ "$marker" == *"starting normal game.yaml bootstrap"* &&
          "$marker" == *"loaded manifest resource 'game.yaml'"* &&
          "$marker" == *"rendered first frame"* ]]; then
        echo "Started the normal game.yaml path and rendered its entry scene. Runtime markers:"
        printf '%s\n' "$marker"
        exit 0
    fi
    sleep 0.25
done

echo "The activity started, but its normal game bootstrap markers were not observed." >&2
"$adb" logcat -d -s \
    xadv2-android:V xadv2-engine:V sfml-activity:V AndroidRuntime:E libc:F DEBUG:F '*:S' >&2
"$adb" shell dumpsys activity activities |
    grep -F -A 8 "$package_name" >&2 || true
exit 1
