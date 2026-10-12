@echo off
REM ============================================================================
REM  build_gui.bat  -  build the GUI (single exe, static CRT, admin manifest)
REM
REM  Requires MSVC x64 + Windows SDK. vcvars64.bat is located automatically via
REM  vswhere; if detection fails, set VSWHERE_PATH or VCINSTALLDIR beforehand.
REM ============================================================================
setlocal
call "%~dp0find_vcvars.bat"
if errorlevel 1 exit /b 1

if not exist "%~dp0build" mkdir "%~dp0build"
pushd "%~dp0src"

cl /nologo /std:c++17 /EHsc /O2 /MT /W3 /permissive- /utf-8 /DUNICODE /D_UNICODE ^
   /Fe:..\build\gcc_lite.exe ^
   /Fo:..\build\ ^
   main.cpp MiBridge.cpp PowerInfo.cpp Sensors.cpp EnergyMeter.cpp ^
   user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib wbemuuid.lib advapi32.lib pdh.lib ^
   /link /SUBSYSTEM:WINDOWS ^
   /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'"

if errorlevel 1 ( echo [X] build failed & popd & exit /b 1 )
popd
echo [OK] build\gcc_lite.exe
endlocal
