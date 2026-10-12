@echo off
REM ============================================================================
REM  find_vcvars.bat  -  locate MSVC x64 and import its environment
REM
REM  IMPORTANT: this script must NOT use setlocal/endlocal around the vcvars
REM  call, otherwise the environment changes are discarded when it returns.
REM
REM  Order of resolution:
REM    1. VCINSTALLDIR (already in environment)
REM    2. vswhere (standard VS installer query)
REM    3. common install paths
REM
REM  Exports on success: VCVARS_FOUND=<path to vcvars64.bat>
REM  Returns 0 on success, 1 on failure.
REM ============================================================================

set "VCVARS_FOUND="

REM --- 1. already inside a VS environment? -----------------------------------
if defined VCINSTALLDIR (
    if exist "%VCINSTALLDIR%Auxiliary\Build\vcvars64.bat" (
        call "%VCINSTALLDIR%Auxiliary\Build\vcvars64.bat" >nul 2>&1
        if not errorlevel 1 (
            set "VCVARS_FOUND=%VCINSTALLDIR%Auxiliary\Build\vcvars64.bat"
            exit /b 0
        )
    )
)

REM --- 2. ask vswhere --------------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if defined VSWHERE_PATH set "VSWHERE=%VSWHERE_PATH%"

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
            call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
            if not errorlevel 1 (
                set "VCVARS_FOUND=%%i\VC\Auxiliary\Build\vcvars64.bat"
                exit /b 0
            )
        )
    )
)

REM --- 3. fall back to well-known locations ----------------------------------
for %%p in (
    "%ProgramFiles%\Microsoft Visual Studio\2022\Community"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
    "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
    "%ProgramFiles%\Microsoft Visual Studio\18\Community"
    "%ProgramFiles%\Microsoft Visual Studio\18\BuildTools"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools"
) do (
    if exist "%%~p\VC\Auxiliary\Build\vcvars64.bat" (
        call "%%~p\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
        if not errorlevel 1 (
            set "VCVARS_FOUND=%%~p\VC\Auxiliary\Build\vcvars64.bat"
            exit /b 0
        )
    )
)

echo [X] Could not locate vcvars64.bat.
echo     Install "Visual Studio Build Tools" with the
echo     "Desktop development with C++" workload, or set VSWHERE_PATH.
exit /b 1
