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
game_res_dir=${PAC_ANDROID_GAME_RES_DIR:-}
app_label=${PAC_ANDROID_APP_LABEL:-}
application_id=${PAC_ANDROID_APPLICATION_ID:-}
version_name=${PAC_ANDROID_VERSION_NAME:-}
version_code=${PAC_ANDROID_VERSION_CODE:-}
build_variant=${PAC_ANDROID_VARIANT:-debug}

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
    if [[ -z "$game_res_dir" && -d "$game_dir/android/res" ]]; then
        game_res_dir="$game_dir/android/res"
    fi
    if [[ -z "$app_label" && -f "$game_dir/android/app-label.txt" ]]; then
        IFS= read -r app_label < "$game_dir/android/app-label.txt"
    fi
    if [[ -z "$application_id" && -f "$game_dir/android/application-id.txt" ]]; then
        IFS= read -r application_id < "$game_dir/android/application-id.txt"
    fi
    if [[ -z "$version_name" && -f "$game_dir/CMakeLists.txt" ]]; then
        version_name=$(sed -nE \
            's/^[[:space:]]*project\([^)]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*$/\1/p' \
            "$game_dir/CMakeLists.txt" | head -n 1)
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

app_label=${app_label:-xadv2 room scene}
application_id=${application_id:-com.nhorro.xadv2.empty}
version_name=${version_name:-0.1.0}
if [[ ! "$application_id" =~ ^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$ ]]; then
    echo "Invalid Android application id: $application_id" >&2
    exit 1
fi
if [[ ! "$version_name" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    echo "Android version must be major.minor.patch: $version_name" >&2
    exit 1
fi
if [[ -z "$version_code" ]]; then
    version_code=$((10#${BASH_REMATCH[1]} * 1000000 +
                    10#${BASH_REMATCH[2]} * 1000 +
                    10#${BASH_REMATCH[3]}))
fi
if [[ ! "$version_code" =~ ^[1-9][0-9]*$ ]]; then
    echo "Android version code must be a positive integer: $version_code" >&2
    exit 1
fi
if [[ "$build_variant" != debug && "$build_variant" != release ]]; then
    echo "PAC_ANDROID_VARIANT must be 'debug' or 'release'" >&2
    exit 1
fi
gradle_variant=${build_variant^}

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
    -Pxadv2GameResDir="$game_res_dir" \
    -Pxadv2AppLabel="$app_label" \
    -Pxadv2ApplicationId="$application_id" \
    -Pxadv2VersionName="$version_name" \
    -Pxadv2VersionCode="$version_code" \
    :app:assemble"$gradle_variant"

echo
echo "APK: $android_dir/app/build/outputs/apk/$build_variant/app-$build_variant.apk"
