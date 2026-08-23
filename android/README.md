# Optional Android environment and runner

Android support is an optional, Linux-hosted engine backend. Normal Linux and
Windows builds do not configure this directory. Android cross-compiles the same
`pac::engine` and `pac::game` targets used on desktop; games do not provide an
Android bootstrap, alternate manifest, source list, or gameplay implementation.

```text
game C++/Lua/YAML -> pac::game + pac::engine -> Android resources/lifecycle/input
```

The only game-owned Android files are optional packaging metadata and resources
under `<game>/android/` (application id, label, icon/manifest resources), plus
accepted `.gles.frag` shader variants beside their ordinary shaders.

## Local directory layout

All machine-local Android files stay under the engine's ignored `android/`
directory:

```text
android/
├── sdk/            # SDK, platform tools, NDK, CMake, emulator and system image
├── avd/            # writable emulator device
├── gradle-home/    # wrapper distribution and Gradle caches
├── app/build/      # generated APK/resources
└── app/.cxx/       # generated ABI build trees
```

These directories are not installed with the engine and must never be committed.
The checked-in Gradle wrapper downloads pinned Gradle 8.13 itself, so do not
download or unpack a separate Gradle distribution.

## Install on Linux

Host requirements:

```bash
sudo apt install openjdk-21-jdk unzip curl
```

Java 17 or newer works; Java 21 is the tested version.

1. Download **Command line tools only for Linux** from the official Android
   Studio download page:
   <https://developer.android.com/studio#command-tools>
2. Extract it so this executable exists:

   ```text
   xadv2-engine/android/sdk/cmdline-tools/latest/bin/sdkmanager
   ```

   A typical extraction starts with a directory named `cmdline-tools`; move its
   contents below the `latest/` directory shown above.
3. Install the pinned SDK/NDK/emulator packages and accept licenses:

   ```bash
   ./android/setup.sh
   ```

The setup installs:

- SDK Platform 36 and Build Tools 35.0.0;
- NDK `27.0.12077973` and CMake 3.31.6;
- platform tools and emulator;
- API 35 Google APIs x86_64 system image;
- an AVD named `xadv2-api35` under `android/avd`.

`ANDROID_SDK_ROOT`, `ANDROID_AVD_HOME`, `ANDROID_AVD_NAME`, and
`GRADLE_USER_HOME` may override the local defaults.

For USB devices on Ubuntu/Debian, also install the udev rules:

```bash
sudo apt install android-sdk-platform-tools-common
```

## Standard game contract

A compiled game exposes one platform-independent CMake target and factory:

```cmake
add_library(my_game STATIC game.cpp ...)
add_library(pac::game ALIAS my_game)
target_link_libraries(my_game PUBLIC pac::engine)
```

```cpp
#include "engine/core/game.hpp"

namespace pac::game {
std::unique_ptr<pac::core::Game> create();
}
```

The returned `Game` owns the scene factory, application hooks, and any modules
captured by those hooks. Desktop supplies filesystem/packed resources; Android
supplies APK assets. Both launch the same object. Newly scaffolded games already
implement this contract.

## Build

Build the default engine example:

```bash
./android/build.sh
```

Build another engine example:

```bash
PAC_ANDROID_EXAMPLE=06_cpp_scene ./android/build.sh
```

Build any game checkout implementing `pac::game`:

```bash
./android/build.sh ../games/fuera-de-cuadro
```

The output contains `arm64-v8a` for physical devices and `x86_64` for the
emulator:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

Use `PAC_ANDROID_VARIANT=release` for the sideloadable QA release configuration.
It is debug-signed and is not a store artifact.

## Run in the emulator

```bash
PAC_ANDROID_GAME_DIR="$PWD/../games/fuera-de-cuadro" ./android/run.sh
```

Without `PAC_ANDROID_GAME_DIR`, `run.sh` builds the selected engine example.
`PAC_ANDROID_HEADLESS=1` starts a no-window CI-style emulator. Additional
arguments are forwarded to the emulator.

## Upload to a phone or tablet

```bash
PAC_ANDROID_GAME_DIR="$PWD/../games/fuera-de-cuadro" ./android/upload.sh
```

The script rejects emulators, validates the device API/ABI, installs the APK,
launches it, and waits for manifest-load and first-frame log markers. Select a
specific authorized device with `ANDROID_SERIAL=<serial>`.

For wireless debugging, use the local adb directly:

```bash
./android/sdk/platform-tools/adb pair PHONE_IP:PAIRING_PORT
./android/sdk/platform-tools/adb connect PHONE_IP:DEBUG_PORT
```

## Game packaging conventions

Given `./android/build.sh /path/to/game`, the runner reads:

- `/path/to/game/data/` — packaged unchanged as APK assets;
- `/path/to/game/CMakeLists.txt` — must expose `pac::game`;
- `/path/to/game/android/res/` — optional Android resources;
- `/path/to/game/android/app-label.txt` — optional launcher label;
- `/path/to/game/android/application-id.txt` — optional package id;
- the CMake `project(... VERSION x.y.z)` — Android version name/code.

Overrides remain available as `PAC_ANDROID_DATA_DIR`,
`PAC_ANDROID_GAME_CMAKE_DIR`, `PAC_ANDROID_GAME_RES_DIR`,
`PAC_ANDROID_APP_LABEL`, `PAC_ANDROID_APPLICATION_ID`,
`PAC_ANDROID_VERSION_NAME`, and `PAC_ANDROID_VERSION_CODE`.

Android platform corrections live in the engine and its pinned modified SFML
dependency. Games must not add Android C++, conditional scenes, alternate YAML
composition, or platform-specific Lua.
