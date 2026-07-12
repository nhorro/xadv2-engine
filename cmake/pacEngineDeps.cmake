# Dependency discovery for the engine, shared by two callers:
#
#   * the engine's own build     (lib/CMakeLists.txt)
#   * the INSTALLED package      (pac_engineConfig.cmake, at find_package time)
#
# Both must produce the *same* imported targets, because the exported engine
# target names them. The one that would otherwise bite: `PkgConfig::LUA` is
# created by pkg_check_modules and cannot be exported, so a consumer importing
# pac::engine would hit "target PkgConfig::LUA not found" unless the config
# re-creates it first — which is exactly what including this file does.
include_guard(GLOBAL)

find_package(SFML 2.6 COMPONENTS graphics window audio system REQUIRED)
find_package(yaml-cpp REQUIRED)

# yaml-cpp 0.8 exports the namespaced target; older packages use the bare name.
if(TARGET yaml-cpp::yaml-cpp)
    set(PAC_YAMLCPP yaml-cpp::yaml-cpp)
else()
    set(PAC_YAMLCPP yaml-cpp)
endif()

# Lua 5.4. Two discovery paths, in preference order:
#
#  - pkg-config (Unix package managers). Distros disagree on the module name
#    (Debian/Ubuntu `lua5.4`, Arch `lua-5.4`, Fedora/RHEL plain `lua` at 5.4.x),
#    so probe in order. GLOBAL on the IMPORTED_TARGET so any directory can link it.
#  - CMake's FindLua (vcpkg / Windows-MSVC, where there is no pkg-config). It
#    yields LUA_INCLUDE_DIR / LUA_LIBRARIES variables and no imported target.
#
# Result, either way: PAC_LUA_INCLUDE_DIRS + PAC_LUA_LIBRARIES, which the caller
# wraps in the `pac_lua` INTERFACE seam.
if(NOT DEFINED PAC_LUA_LIBRARIES)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LUA IMPORTED_TARGET GLOBAL lua5.4)
        if(NOT LUA_FOUND)
            pkg_check_modules(LUA IMPORTED_TARGET GLOBAL lua-5.4)
        endif()
        if(NOT LUA_FOUND)
            pkg_check_modules(LUA IMPORTED_TARGET GLOBAL lua>=5.4)
        endif()
    endif()

    if(TARGET PkgConfig::LUA)
        set(PAC_LUA_INCLUDE_DIRS "")
        set(PAC_LUA_LIBRARIES PkgConfig::LUA)
        message(STATUS "pac_engine: Lua via pkg-config (${LUA_LIBRARIES})")
    else()
        find_package(Lua 5.4 REQUIRED)
        set(PAC_LUA_INCLUDE_DIRS ${LUA_INCLUDE_DIR})
        set(PAC_LUA_LIBRARIES ${LUA_LIBRARIES})
        message(STATUS "pac_engine: Lua via FindLua (${LUA_LIBRARIES})")
    endif()
endif()
