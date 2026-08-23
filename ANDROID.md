# Test the Android build on a phone

The current debug APK supports Android 6.0/API 23 or newer on 64-bit ARM phones.
It is signed with the local Android debug key and can be installed directly with
ADB; Android Studio is not required.

## USB setup

1. On the phone, open **Settings → About phone** and tap **Build number** seven
   times to enable Developer options. The exact menu name varies by manufacturer.
2. Open **Developer options** and enable **USB debugging**.
3. Connect the unlocked phone with a data-capable USB cable. If Android asks
   whether to allow debugging from this computer, approve the RSA fingerprint.
4. From the engine repository, check the connection:

   ```bash
   ../android-sdk/platform-tools/adb devices -l
   ```

   The phone's row must end in `device`. If it says `unauthorized`, unlock the
   phone and accept the debugging prompt.

## Build, upload, and launch

Run:

```bash
./android/upload.sh
```

This packages example 01 by default. To upload another example instead:

```bash
PAC_ANDROID_EXAMPLE=02_scumm_inventory ./android/upload.sh
PAC_ANDROID_EXAMPLE=03_dialog_npc ./android/upload.sh
PAC_ANDROID_EXAMPLE=04_cutscene ./android/upload.sh
PAC_ANDROID_EXAMPLE=05_closeup ./android/upload.sh
PAC_ANDROID_EXAMPLE=06_cpp_scene ./android/upload.sh
```

`PAC_ANDROID_EXAMPLE` is the example directory name under `examples/`; the same
setting works with `android/build-android.sh` and `android/run.sh`.

To package an external game's data directory instead:

```bash
PAC_ANDROID_DATA_DIR=../fuera-de-cuadro/data ./android/upload.sh
```

If the external game's parent directory contains a `CMakeLists.txt` and
`android/bootstrap.cpp`, the build adds the normal game project and links its
`pac::game` target. Fuera de Cuadro therefore uses exactly the same map
and notebook composition library on desktop and Android; Example 06 is the
smaller custom-C++ compatibility case.

The script:

- selects one authorized physical device, never an emulator;
- checks that its Android version and CPU ABI are supported;
- builds the selected example into the debug APK for both configured ABIs;
- updates the installed app with `adb install -r` and launches it;
- waits for the normal manifest load and first rendered frame markers.

The activity rotates to landscape. Tap the room floor to walk and hotspots to
run their default Lua action. Example 02 adds selectable verbs and inventory;
example 03 adds touch-selectable NPC dialog options; example 04 adds cutscenes
advanced by touch; and example 05 adds a close-up overlay. The phone's system
Back button performs the existing Escape action, including closing a close-up.
The APK contains the selected example's unchanged data tree and starts it through
the same manifest, scenes, and Lua APIs used on desktop. Re-running the script
rebuilds and replaces the existing app.

Shaders, grading, and dynamic lighting use the GLES2 version of the desktop
multi-pass pipeline. Authored shaders may provide a `.gles.frag` sidecar for
syntax or precision differences. The Android SFML compatibility patch also
keeps render-texture alpha correct for translucent sprites and shadows, and
adds the MP3 reader required by Fuera de Cuadro's score.

Save-game thumbnail images are temporarily unavailable on Android. SFML 2.6's
GLES1 framebuffer readback is not portable across the test devices; the engine
leaves the optional thumbnail empty while normal save data continues to work.

If multiple phones are connected, select one explicitly:

```bash
ANDROID_SERIAL=<serial-from-adb-devices> ./android/upload.sh
```

`ANDROID_SDK_ROOT` may point to another Android SDK. Without it, the scripts use
the workspace-local SDK at `../android-sdk`.

## Wireless debugging (optional)

Android 11 and newer can connect through Developer options → Wireless debugging.
The phone and computer must be on the same network.

```bash
../android-sdk/platform-tools/adb pair PHONE_IP:PAIRING_PORT
../android-sdk/platform-tools/adb connect PHONE_IP:DEBUG_PORT
ANDROID_SERIAL=PHONE_IP:DEBUG_PORT ./android/upload.sh
```

The pairing and debugging ports shown by Android may be different. USB is simpler
for the first test.

## Logs and stopping the app

Use the serial shown by `adb devices -l`:

```bash
../android-sdk/platform-tools/adb -s SERIAL logcat \
  -s 'xadv2-android:I' 'xadv2-engine:I' '*:S'
../android-sdk/platform-tools/adb -s SERIAL shell am force-stop com.nhorro.xadv2.empty
```

## Troubleshooting

- **No device appears:** unlock the phone, try a different data-capable cable or
  USB port, and choose a data/file-transfer USB mode if the phone offers one.
- **`unauthorized`:** revoke USB debugging authorizations in Developer options,
  reconnect, and approve the new fingerprint prompt.
- **Linux `no permissions`:** this is a host USB permission issue, not an SDK
  issue. On Debian/Ubuntu, run:

  ```bash
  sudo apt install android-sdk-platform-tools-common
  sudo udevadm control --reload-rules
  ```

  Then unplug and reconnect the unlocked phone. Your account must also belong to
  `plugdev` (`id` shows the current groups); log out and back in if you just added
  it.
- **The phone blocks installation:** some manufacturers have a separate
  **Install via USB** developer option or show an on-device confirmation prompt.
- **More than one device:** set `ANDROID_SERIAL` as shown above.

Disable USB/Wireless debugging when you no longer need development access.
