# Android experiment: unchanged engine examples

The Android target now cross-compiles the complete engine and starts the normal
game composition root:

```cpp
run_game_from_resources(android_assets, "game.yaml");
```

Gradle packages one selected example's complete, unchanged `data` directory.
At runtime the regular manifest loader creates the regular scenes and loads
YAML, images, fonts, audio declarations, and Lua behavior through the same
`ResourceSource` interface used on desktop. There is no Android-specific room
implementation or script probe.

Examples 01 through 06 have been exercised end to end. Example 02 covers verbs,
inventory, two-operand Lua dispatch, and room changes. Example 03 covers a
script-spawned NPC, an NPC-bound hotspot, multi-line speech, conditional and
one-shot dialog options, inventory side effects, and returning control to the
room. Example 04 covers the title screen, manual and automatic cutscenes,
per-slide presentation overrides, and returning to a room. Example 05 covers a
Lua-driven close-up overlay, hotspot dialogue and persistent state, and maps the
Android system Back button to the existing Escape-to-close behavior.
Example 06 additionally links a game-owned C++ scene module, installs its custom
Lua API, and renders the resulting Field Notes scene.

Primary touch input is translated in the engine into pointer movement followed
by a left-button press/release. Existing hover/click handlers therefore see the
same event sequence they expect on desktop, and a floor tap uses the normal
room pathfinding and avatar movement code.

## Portability contract

The target is one game definition and one gameplay API on every platform:

```text
game.yaml -> run_game -> engine scenes and Lua
                   ^
                   |
        platform services/resources/input
```

Android-specific code belongs below the engine's platform seams: packaged
resource access, writable user-data locations, lifecycle/window integration,
and raw input translation. Rooms, manifests, Lua scripts, and scene behavior
must not branch on Android. The examples are incremental compatibility checks;
running Fuera de Cuadro is the final acceptance milestone. If that game needs
temporary experimental changes, they live on its `android-porting-experiment`
branch.

The normal engine code is linked, but compatibility has not yet been exercised
across every subsystem or example. Shader effects and dynamic lighting remain
deferred: GLES-only SFML reports shaders unavailable, so rooms take the normal
unshaded rendering path instead of entering the off-screen effects pipeline.
Save-game thumbnail capture is currently disabled on Android because SFML 2.6's GLES1
texture readback depends on optional framebuffer functions; saves themselves
remain available. The Android build also applies a configure-time SFML font
atlas workaround for the same GLES1 limitation.

## Pinned toolchain

The local test setup uses:

- Android command-line tools 22.0 (`15859902`)
- Android SDK Platform 36 and Build Tools 35.0.0
- Android Gradle Plugin 8.13.2 with Gradle 8.13
- Android NDK `27.0.12077973`
- CMake 3.31.6
- yaml-cpp 0.8.0 (fetched and cross-compiled by CMake)
- Lua 5.4.8 (fetched and cross-compiled by CMake)
- Android Emulator 37.1.11 with the API 35 Google APIs x86_64 image
- OpenJDK 21 (the Android Gradle Plugin requires JDK 17 or newer)

The SDK was installed outside the Git checkout at:

```text
../android-sdk
```

Gradle 8.13 was also unpacked at `../gradle-8.13` to generate the checked-in
wrapper. Normal builds use `android/gradlew` and do not depend on that unpacked
copy. The emulator's writable AVD files live at `../android-avd`.

`ANDROID_SDK_ROOT` can point somewhere else. The build script writes that path
to ignored `local.properties`, so no machine-specific path is committed.

## Build

From the engine repository:

```bash
./android/build-android.sh
```

Example 01 is the default. Select another example directory with
`PAC_ANDROID_EXAMPLE`:

```bash
PAC_ANDROID_EXAMPLE=02_scumm_inventory ./android/build-android.sh
# For example:
PAC_ANDROID_EXAMPLE=05_closeup ./android/build-android.sh
```

Example 06 automatically selects and links its game-owned C++ composition:

```bash
PAC_ANDROID_EXAMPLE=06_cpp_scene ./android/run.sh
```

An external game's data directory can use the same scene composition on desktop
and Android:

```bash
PAC_ANDROID_DATA_DIR=../fuera-de-cuadro/data ./android/run.sh
```

For a game with native scene modules, the build automatically uses
`android/bootstrap.cpp`, reads additional sources from `android/sources.list`,
and adds the game's `src` directory as an include root. Fuera de Cuadro uses this
convention for its production map/notebook composition, while the command above
remains identical to a data-only game. The bootstrap implements
`pac::android::run_game`; each non-comment line in `sources.list` is a source path
relative to the game root.

The convention can be overridden with `PAC_ANDROID_GAME_BOOTSTRAP`,
`PAC_ANDROID_GAME_SOURCES`, and `PAC_ANDROID_GAME_INCLUDE_DIRS`. Example 06 is
the smaller checked compatibility case for this seam and sets those values
automatically.

`PAC_ANDROID_EXAMPLE` must name a directory directly under `examples/` containing
`data/game.yaml`. The selected data tree replaces the previous generated assets,
so an APK never mixes resources from two examples.

The debug APK contains `arm64-v8a` for physical devices and `x86_64` for the
emulator. It is written to:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

## Run on a connected device

For the complete phone setup and troubleshooting guide, see
[`ANDROID.md`](../ANDROID.md). Once USB or wireless debugging is connected, run:

```bash
./android/upload.sh
# Or:
PAC_ANDROID_EXAMPLE=02_scumm_inventory ./android/upload.sh
```

The upload script selects a physical device, builds, installs, launches, and
checks that the unchanged manifest loads and renders its entry scene. Bootstrap
messages use `xadv2-android`; engine diagnostics use `xadv2-engine`.

## Run on the local emulator

The local SDK has an AVD named `xadv2-api35`. Build, boot that AVD, install the
APK, and launch the room with one command:

```bash
./android/run.sh
# Or:
PAC_ANDROID_EXAMPLE=02_scumm_inventory ./android/run.sh
```

The script keeps the emulator attached to the terminal. Press Ctrl+C to stop an
emulator it started. If an emulator is already connected, the script reuses it
and leaves it running. It honors `ANDROID_SDK_ROOT`, `ANDROID_AVD_HOME`,
`ANDROID_AVD_NAME`, `ANDROID_SERIAL`, and `PAC_ANDROID_EXAMPLE`.

For a headless CI-style launch:

```bash
PAC_ANDROID_HEADLESS=1 ./android/run.sh
```

Additional arguments are forwarded to the Android emulator when the script
starts one.

The AVD's home screen is portrait, but the game activity declares a fixed
landscape orientation. Once the game starts, SFML renders a landscape surface.
Interactions behave as on desktop: tap the floor to walk, tap a hotspot to use
its default verb, or use the verb and inventory panels to build a command.

## Near-term path to the examples

The next slices are deliberately ordered so each remains runnable:

1. Examples 01 through 06 now run through the same data, Lua, and game-owned C++
   composition APIs as desktop.
2. A reduced Fuera de Cuadro room renders and accepts touch movement from its
   `android-porting-experiment` branch. Its production global Lua, inventory,
   fact state, notebook, and map now also run through the same APIs as desktop.
3. Restore room transitions and the remaining functional game graph one slice at
   a time, keeping platform fixes below the game API and leaving advanced effects
   deferred until their own slices.

Shaders, save thumbnails, and other difficult effects can remain disabled until
the basic examples run end to end.
