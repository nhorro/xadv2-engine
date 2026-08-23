#!/usr/bin/env bash
# Install the pinned Android packages into this checkout's ignored android/sdk.
set -euo pipefail

android_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk_dir=${ANDROID_SDK_ROOT:-$android_dir/sdk}
sdkmanager="$sdk_dir/cmdline-tools/latest/bin/sdkmanager"
avdmanager="$sdk_dir/cmdline-tools/latest/bin/avdmanager"
avd_home=${ANDROID_AVD_HOME:-$android_dir/avd}
avd_name=${ANDROID_AVD_NAME:-xadv2-api35}
system_image="system-images;android-35;google_apis;x86_64"

if [[ ! -x "$sdkmanager" || ! -x "$avdmanager" ]]; then
    echo "Android command-line tools were not found under:" >&2
    echo "  $sdk_dir/cmdline-tools/latest" >&2
    echo "Follow android/README.md to install the small command-line tools archive," >&2
    echo "then run this script again." >&2
    exit 1
fi
if ! command -v java >/dev/null 2>&1; then
    echo "Java 17 or newer is required." >&2
    exit 1
fi

export ANDROID_HOME="$sdk_dir"
export ANDROID_SDK_ROOT="$sdk_dir"
export ANDROID_AVD_HOME="$avd_home"
export GRADLE_USER_HOME=${GRADLE_USER_HOME:-$android_dir/gradle-home}

mkdir -p "$avd_home" "$GRADLE_USER_HOME"
"$sdkmanager" --sdk_root="$sdk_dir" --licenses < <(yes) >/dev/null
"$sdkmanager" --sdk_root="$sdk_dir" \
    "platform-tools" \
    "platforms;android-36" \
    "build-tools;35.0.0" \
    "ndk;27.0.12077973" \
    "cmake;3.31.6" \
    "emulator" \
    "$system_image"

if [[ ! -d "$avd_home/$avd_name.avd" ]]; then
    printf 'no\n' | "$avdmanager" create avd \
        --name "$avd_name" \
        --package "$system_image" \
        --device "pixel_6"
fi

echo "Android environment ready under $android_dir"
