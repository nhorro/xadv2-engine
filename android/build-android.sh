#!/usr/bin/env bash
set -euo pipefail

android_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
workspace_dir=$(cd -- "$android_dir/../.." && pwd)
sdk_dir=${ANDROID_SDK_ROOT:-$workspace_dir/android-sdk}
example_name=${PAC_ANDROID_EXAMPLE:-01_hello_room}
data_dir=${PAC_ANDROID_DATA_DIR:-}
game_bootstrap=${PAC_ANDROID_GAME_BOOTSTRAP:-}
game_sources=${PAC_ANDROID_GAME_SOURCES:-}
game_include_dirs=${PAC_ANDROID_GAME_INCLUDE_DIRS:-}

if [[ ! -x "$sdk_dir/platform-tools/adb" ]]; then
    echo "Android SDK not found at $sdk_dir" >&2
    echo "Set ANDROID_SDK_ROOT or follow android/README.md." >&2
    exit 1
fi
if [[ -n "$data_dir" ]]; then
    if [[ ! -d "$data_dir" ]]; then
        echo "PAC_ANDROID_DATA_DIR is not a directory: $data_dir" >&2
        exit 1
    fi
    data_dir=$(cd -- "$data_dir" && pwd)
    if [[ ! -f "$data_dir/game.yaml" ]]; then
        echo "PAC_ANDROID_DATA_DIR has no game.yaml: $data_dir" >&2
        exit 1
    fi
    package_label="external data: $data_dir"

    # Optional convention for an external game with native scene modules. The
    # game owns the bootstrap and a reviewable source inventory; callers keep
    # using the same PAC_ANDROID_DATA_DIR command as a data-only game.
    game_dir=$(cd -- "$data_dir/.." && pwd)
    external_bootstrap="$game_dir/android/bootstrap.cpp"
    external_sources="$game_dir/android/sources.list"
    if [[ -z "$game_bootstrap" && -f "$external_bootstrap" ]]; then
        game_bootstrap="$external_bootstrap"
    fi
    if [[ -z "$game_sources" && -f "$external_sources" ]]; then
        while IFS= read -r relative_source || [[ -n "$relative_source" ]]; do
            [[ -z "$relative_source" || "$relative_source" == \#* ]] && continue
            source_path="$game_dir/$relative_source"
            if [[ ! -f "$source_path" ]]; then
                echo "Android game source does not exist: $source_path" >&2
                exit 1
            fi
            game_sources+="${game_sources:+;}$source_path"
        done < "$external_sources"
    fi
    if [[ -z "$game_include_dirs" && -d "$game_dir/src" ]]; then
        game_include_dirs="$game_dir/src"
    fi
else
    example_dir="$android_dir/../examples/$example_name"
    data_dir="$example_dir/data"
    if [[ ! $example_name =~ ^[0-9][0-9]_[a-z0-9_]+$ || ! -f "$data_dir/game.yaml" ]]; then
        echo "Android example '$example_name' was not found under examples/." >&2
        echo "Set PAC_ANDROID_EXAMPLE to an example directory such as 02_scumm_inventory." >&2
        exit 1
    fi
    data_dir=$(cd -- "$data_dir" && pwd)
    package_label="example: $example_name"

    if [[ "$example_name" == 06_cpp_scene ]]; then
        example_dir=$(cd -- "$example_dir" && pwd)
        game_bootstrap=${game_bootstrap:-$example_dir/android/bootstrap.cpp}
        game_sources=${game_sources:-$example_dir/src/game.cpp;$example_dir/src/field_notes.cpp}
        game_include_dirs=${game_include_dirs:-$example_dir/src}
    fi
fi

if [[ -n "$game_bootstrap" && ! -f "$game_bootstrap" ]]; then
    echo "PAC_ANDROID_GAME_BOOTSTRAP does not exist: $game_bootstrap" >&2
    exit 1
fi

export ANDROID_HOME="$sdk_dir"
export ANDROID_SDK_ROOT="$sdk_dir"

# local.properties is machine-specific and intentionally ignored by Git.
printf 'sdk.dir=%s\n' "$sdk_dir" > "$android_dir/local.properties"

echo "Packaging $package_label"
"$android_dir/gradlew" --project-dir "$android_dir" \
    -Pxadv2Example="$example_name" \
    -Pxadv2DataDir="$data_dir" \
    -Pxadv2GameBootstrap="$game_bootstrap" \
    -Pxadv2GameSources="$game_sources" \
    -Pxadv2GameIncludeDirs="$game_include_dirs" \
    :app:assembleDebug

echo
echo "APK: $android_dir/app/build/outputs/apk/debug/app-debug.apk"
