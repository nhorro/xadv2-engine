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
echo   "%BUILD_DIR%\%CONFIG%\themummy_demo.exe"
echo.
echo Run from the repository root so games\themummy\config.yml resolves by default:
echo   "%BUILD_DIR%\%CONFIG%\themummy_demo.exe"
