#!/usr/bin/env bash
set -euo pipefail

android_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
workspace_dir=$(cd -- "$android_dir/../.." && pwd)
sdk_dir=${ANDROID_SDK_ROOT:-$workspace_dir/android-sdk}
adb="$sdk_dir/platform-tools/adb"
apk="$android_dir/app/build/outputs/apk/debug/app-debug.apk"

if [[ ! -x "$adb" ]]; then
    echo "adb not found at $adb" >&2
    exit 1
fi
if [[ ! -f "$apk" ]]; then
    "$android_dir/build-android.sh"
fi
if ! "$adb" get-state >/dev/null 2>&1; then
    echo "No Android device or emulator is connected." >&2
    echo "See android/README.md for the emulator command." >&2
    exit 1
fi

"$adb" install -r "$apk"
"$adb" shell am force-stop com.nhorro.xadv2.empty
"$adb" logcat -c
"$adb" shell am start -n com.nhorro.xadv2.empty/android.app.NativeActivity

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
    sed -n '/com\.nhorro\.xadv2\.empty/,+8p' >&2
exit 1
