@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "PROJECT_DIR=%%~fI"

set "PRESET=windows-x64-msvc"
set "PRESET_KIND=windows-x64"
set "PRESET_SET=0"
set "RUN_TESTS=0"

:parse
if "%~1"=="" goto validate
if /i "%~1"=="--help" goto usage
if /i "%~1"=="-h" goto usage
if /i "%~1"=="--test" (
  set "RUN_TESTS=1"
  shift
  goto parse
)

if /i "%~1"=="windows-x64-msvc" goto preset_windows_x64
if /i "%~1"=="windows-x86-msvc" goto preset_windows_x86
if /i "%~1"=="android-arm64" goto preset_android_arm64
if /i "%~1"=="android-armeabi" goto preset_android_armeabi
if /i "%~1"=="linux-x64-cross" goto preset_linux
if /i "%~1"=="macos-x64-cross" goto preset_macos_x64
if /i "%~1"=="macos-arm64-cross" goto preset_macos_arm64

echo [build] ERROR: Unknown argument or preset: %~1
echo [build] Run "%~nx0 --help" for usage.
exit /b 2

:preset_windows_x64
set "SELECTED_PRESET=windows-x64-msvc"
set "PRESET_KIND=windows-x64"
goto set_preset
:preset_windows_x86
set "SELECTED_PRESET=windows-x86-msvc"
set "PRESET_KIND=windows-x86"
goto set_preset
:preset_android_arm64
set "SELECTED_PRESET=android-arm64"
set "PRESET_KIND=android"
goto set_preset
:preset_android_armeabi
set "SELECTED_PRESET=android-armeabi"
set "PRESET_KIND=android"
goto set_preset
:preset_linux
set "SELECTED_PRESET=linux-x64-cross"
set "PRESET_KIND=linux"
goto set_preset
:preset_macos_x64
set "SELECTED_PRESET=macos-x64-cross"
set "PRESET_KIND=macos"
goto set_preset
:preset_macos_arm64
set "SELECTED_PRESET=macos-arm64-cross"
set "PRESET_KIND=macos"
goto set_preset

:set_preset
if "%PRESET_SET%"=="1" (
  echo [build] ERROR: Specify only one build preset.
  exit /b 2
)
set "PRESET=%SELECTED_PRESET%"
set "PRESET_SET=1"
shift
goto parse

:validate
if not defined VCPKG_ROOT (
  echo [build] ERROR: VCPKG_ROOT is not set.
  exit /b 1
)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo [build] ERROR: vcpkg toolchain not found under "%VCPKG_ROOT%".
  exit /b 1
)
where cmake >nul 2>nul
if errorlevel 1 (
  echo [build] ERROR: CMake is not available in PATH.
  exit /b 1
)
where ninja >nul 2>nul
if errorlevel 1 (
  echo [build] ERROR: Ninja is not available in PATH.
  exit /b 1
)

if /i "%PRESET_KIND%"=="windows-x64" goto validate_windows_x64
if /i "%PRESET_KIND%"=="windows-x86" goto validate_windows_x86
if /i "%PRESET_KIND%"=="android" goto validate_android
if /i "%PRESET_KIND%"=="linux" goto validate_linux
if /i "%PRESET_KIND%"=="macos" goto validate_macos
goto run_build

:validate_windows_x64
call :check_msvc x64
if errorlevel 1 exit /b 1
goto run_build
:validate_windows_x86
call :check_msvc x86
if errorlevel 1 exit /b 1
goto run_build

:check_msvc
where cl >nul 2>nul
if errorlevel 1 (
  echo [build] ERROR: MSVC compiler not found. Run from a Visual Studio developer environment.
  echo [build] Initialize it with vcvarsall.bat %~1 before running this script.
  exit /b 1
)
if /i not "%VSCMD_ARG_TGT_ARCH%"=="%~1" (
  echo [build] ERROR: MSVC target is "%VSCMD_ARG_TGT_ARCH%", expected "%~1".
  echo [build] Initialize the matching environment with vcvarsall.bat %~1.
  exit /b 1
)
exit /b 0

:validate_android
if not defined ANDROID_NDK_HOME (
  echo [build] ERROR: ANDROID_NDK_HOME is not set.
  exit /b 1
)
if not exist "%ANDROID_NDK_HOME%\build\cmake\android.toolchain.cmake" (
  echo [build] ERROR: Android NDK CMake toolchain not found under "%ANDROID_NDK_HOME%".
  exit /b 1
)
if "%RUN_TESTS%"=="1" goto unsupported_test
goto run_build

:validate_linux
if not defined LINUX_SYSROOT (
  echo [build] ERROR: LINUX_SYSROOT is not set.
  exit /b 1
)
if not exist "%LINUX_SYSROOT%" (
  echo [build] ERROR: Linux sysroot does not exist: "%LINUX_SYSROOT%".
  exit /b 1
)
where clang >nul 2>nul
if errorlevel 1 goto missing_llvm
where clang++ >nul 2>nul
if errorlevel 1 goto missing_llvm
where llvm-ar >nul 2>nul
if errorlevel 1 goto missing_llvm
where llvm-ranlib >nul 2>nul
if errorlevel 1 goto missing_llvm
if "%RUN_TESTS%"=="1" goto unsupported_test
goto run_build

:validate_macos
if not defined MACOS_SDK_PATH (
  echo [build] ERROR: MACOS_SDK_PATH is not set.
  exit /b 1
)
if not exist "%MACOS_SDK_PATH%" (
  echo [build] ERROR: macOS SDK does not exist: "%MACOS_SDK_PATH%".
  exit /b 1
)
where clang >nul 2>nul
if errorlevel 1 goto missing_llvm
where clang++ >nul 2>nul
if errorlevel 1 goto missing_llvm
where llvm-ar >nul 2>nul
if errorlevel 1 goto missing_llvm
where llvm-ranlib >nul 2>nul
if errorlevel 1 goto missing_llvm
if "%RUN_TESTS%"=="1" goto unsupported_test
goto run_build

:missing_llvm
echo [build] ERROR: LLVM tools clang, clang++, and llvm-ar must be available in PATH.
exit /b 1

:unsupported_test
echo [build] ERROR: Preset "%PRESET%" has no checked-in CTest preset.
exit /b 2

:run_build
if not "%RUN_TESTS%"=="1" goto run_commands
if /i not "%PRESET_KIND%"=="windows-x64" if /i not "%PRESET_KIND%"=="windows-x86" goto unsupported_test

:run_commands
echo [build] Configure: %PRESET%
pushd "%PROJECT_DIR%"
cmake --preset "%PRESET%"
if errorlevel 1 goto configure_failed

echo [build] Build: %PRESET%
cmake --build --preset "%PRESET%" --parallel
if errorlevel 1 goto build_failed

if not "%RUN_TESTS%"=="1" goto build_success
echo [build] Test: %PRESET%
ctest --preset "%PRESET%"
if errorlevel 1 goto test_failed

:build_success
popd
echo [build] Done: %PRESET%
exit /b 0

:configure_failed
set "BUILD_RESULT=%ERRORLEVEL%"
popd
exit /b %BUILD_RESULT%
:build_failed
set "BUILD_RESULT=%ERRORLEVEL%"
popd
exit /b %BUILD_RESULT%
:test_failed
set "BUILD_RESULT=%ERRORLEVEL%"
popd
exit /b %BUILD_RESULT%

:usage
echo Usage: %~nx0 [PRESET] [--test]
echo.
echo Presets:
echo   windows-x64-msvc     Windows x64 (default)
echo   windows-x86-msvc     Windows x86
echo   android-arm64        Android arm64-v8a
echo   android-armeabi      Android armeabi-v7a
echo   linux-x64-cross      Linux x64 cross-build from Windows
echo   macos-x64-cross      macOS x64 cross-build from Windows
echo   macos-arm64-cross    macOS arm64 cross-build from Windows
echo.
echo Options:
echo   --test               Run CTest (Windows presets only)
echo   -h, --help           Show this help
echo.
echo Requirements:
echo   All presets: VCPKG_ROOT, CMake, Ninja
echo   Windows: matching MSVC developer environment (vcvarsall.bat x64 or x86)
echo   Android: ANDROID_NDK_HOME
echo   Linux cross: LINUX_SYSROOT, LLVM/Clang tools
echo   macOS cross: MACOS_SDK_PATH, LLVM/Clang tools
echo.
echo Examples:
echo   %~nx0
echo   %~nx0 windows-x86-msvc --test
echo   %~nx0 android-arm64
echo   %~nx0 macos-arm64-cross
exit /b 0
