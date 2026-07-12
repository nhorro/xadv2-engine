@echo off
setlocal

set "REPO_DIR=%~dp0"
if "%REPO_DIR:~-1%"=="\" set "REPO_DIR=%REPO_DIR:~0,-1%"

set "CONFIG=Debug"
if not "%~1"=="" set "CONFIG=%~1"

if not defined VCPKG_ROOT (
  if exist "%REPO_DIR%\..\vcpkg\scripts\buildsystems\vcpkg.cmake" (
    set "VCPKG_ROOT=%REPO_DIR%\..\vcpkg"
  )
)

if not defined VCPKG_ROOT (
  echo VCPKG_ROOT is not set.
  echo Install vcpkg or set VCPKG_ROOT to your vcpkg checkout, then run this script again.
  exit /b 1
)

if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo Could not find vcpkg at "%VCPKG_ROOT%".
  echo Expected "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake".
  exit /b 1
)

rem vcpkg.json deliberately ships without a builtin-baseline (no fixed SHA pinned),
rem so the sfml 2.6.1 / lua 5.4.7 overrides cannot resolve until one is stamped in.
rem Add the local vcpkg baseline once (mirrors CI's x-update-baseline step); skip if
rem the manifest already carries one so re-runs stay idempotent.
findstr /C:"builtin-baseline" "%REPO_DIR%\vcpkg.json" >nul
if not errorlevel 1 goto baseline_done
echo Stamping the local vcpkg baseline into vcpkg.json ...
pushd "%REPO_DIR%"
"%VCPKG_ROOT%\vcpkg.exe" x-update-baseline --add-initial-baseline
if errorlevel 1 (
  popd
  echo Failed to stamp the vcpkg baseline into vcpkg.json.
  exit /b 1
)
popd
:baseline_done

set "BUILD_DIR=%REPO_DIR%\build-windows"
set "TRIPLET=x64-windows"

if exist "%BUILD_DIR%\CMakeCache.txt" (
  findstr /C:"VCPKG_INSTALLED_DIR:" "%BUILD_DIR%\CMakeCache.txt" >nul
  if errorlevel 1 (
    echo Existing build-windows cache was not configured through vcpkg; recreating it.
    rmdir /s /q "%BUILD_DIR%"
  )
)

set "USE_TOOLCHAIN=1"
if exist "%BUILD_DIR%\CMakeCache.txt" (
  findstr /C:"VCPKG_INSTALLED_DIR:" "%BUILD_DIR%\CMakeCache.txt" >nul
  if not errorlevel 1 (
    set "USE_TOOLCHAIN="
  )
)

if defined USE_TOOLCHAIN (
  cmake -U SFML_DIR -U Lua_DIR -U LUA_* -U yaml-cpp_DIR ^
    -S "%REPO_DIR%" -B "%BUILD_DIR%" ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=%TRIPLET%
) else (
  cmake -U SFML_DIR -U Lua_DIR -U LUA_* -U yaml-cpp_DIR ^
    -S "%REPO_DIR%" -B "%BUILD_DIR%" ^
    -DVCPKG_TARGET_TRIPLET=%TRIPLET%
)
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --config "%CONFIG%" --parallel
if errorlevel 1 exit /b 1

echo.
echo Built %CONFIG%:
echo   "%BUILD_DIR%\examples\01_hello_room\%CONFIG%\pac_example_01_hello_room.exe"
echo.
echo Run it from the repository root, pointing at the sample's manifest:
echo   "%BUILD_DIR%\examples\01_hello_room\%CONFIG%\pac_example_01_hello_room.exe" examples\01_hello_room\data\game.yaml
