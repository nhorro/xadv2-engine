# Release-only variant of the built-in x64-windows triplet, for CI.
#
# The Windows CI job only ever builds and tests the Release configuration
# (windows-msvc-release), so building debug copies of every dependency — openal-soft
# alone is ~4 min of that — is pure waste. VCPKG_BUILD_TYPE=release roughly halves
# the cold dependency build. Dynamic linkage matches x64-windows so the applocal DLL
# deploy keeps working. Devs who want a debug build locally still use plain
# x64-windows via the windows-msvc preset.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
