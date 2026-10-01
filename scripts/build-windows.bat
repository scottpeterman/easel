@echo off
REM scripts\build-windows.bat
REM
REM Build, test and package Easel on Windows (MSVC x64, Ninja).
REM
REM   scripts\build-windows.bat                 configure (if needed), build, test
REM   scripts\build-windows.bat --run           ...then launch the app
REM   scripts\build-windows.bat --zip           ...then make dist\Easel-windows-x64.zip
REM   scripts\build-windows.bat --clean         wipe the build directory first
REM   scripts\build-windows.bat --qt DIR        Qt prefix, e.g. C:\Qt\6.10.3\msvc2022_64
REM   scripts\build-windows.bat --debug         Debug build in build-debug\
REM   scripts\build-windows.bat --no-tests      skip ctest
REM
REM Qt is taken from --qt, then %QT_ROOT_DIR% (set by CI), then
REM %CMAKE_PREFIX_PATH% if it is a Qt prefix, then C:\Qt\6.10.3\msvc2022_64,
REM then the newest C:\Qt\6.*\msvc*_64. It must be an MSVC build of Qt with the
REM Shader Tools module.
REM
REM Runs from a plain cmd prompt: if the x64 MSVC environment is not already
REM loaded it is loaded through vswhere + vcvars64.bat. Ninja and CMake come from
REM PATH, then from Visual Studio, then from C:\Qt\Tools.
REM
REM A build directory configured from another source tree, another Qt or another
REM generator is wiped automatically.

setlocal enabledelayedexpansion

set "QT_VERSION_DEFAULT=6.10.3"

cd /d "%~dp0.."
set "ROOT=%CD%"

set "QT="
set "BUILD_TYPE=Release"
set "CLEAN=0"
set "RUN_TESTS=1"
set "MAKE_ZIP=0"
set "RUN_APP=0"

REM --- options ----------------------------------------------------------------

:parse
if "%~1"=="" goto parsed
if /i "%~1"=="--qt" (
    if "%~2"=="" (echo error: --qt needs a path 1>&2 & exit /b 2)
    set "QT=%~f2" & shift & shift & goto parse
)
if /i "%~1"=="--clean" (set "CLEAN=1" & shift & goto parse)
if /i "%~1"=="--debug" (set "BUILD_TYPE=Debug" & shift & goto parse)
if /i "%~1"=="--no-tests" (set "RUN_TESTS=0" & shift & goto parse)
if /i "%~1"=="--zip" (set "MAKE_ZIP=1" & shift & goto parse)
if /i "%~1"=="--run" (set "RUN_APP=1" & shift & goto parse)
if /i "%~1"=="--help" goto usage
if /i "%~1"=="-h" goto usage
echo error: unknown option: %~1 1>&2
exit /b 2

:usage
for /f "usebackq skip=3 tokens=* delims=" %%L in ("%~f0") do (
    set "LINE=%%L"
    if not "!LINE:~0,3!"=="REM" exit /b 0
    echo(!LINE:~4!
)
exit /b 0

:parsed

REM --- Qt ---------------------------------------------------------------------

if not "%QT%"=="" goto qt_found
if not "%QT_ROOT_DIR%"=="" (set "QT=!QT_ROOT_DIR!" & goto qt_found)
if not "%CMAKE_PREFIX_PATH%"=="" if exist "%CMAKE_PREFIX_PATH%\lib\cmake\Qt6\Qt6Config.cmake" (
    set "QT=!CMAKE_PREFIX_PATH!" & goto qt_found
)
if exist "C:\Qt\%QT_VERSION_DEFAULT%\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake" (
    set "QT=C:\Qt\%QT_VERSION_DEFAULT%\msvc2022_64" & goto qt_found
)
REM Newest C:\Qt\6.*: dir /o-n sorts names descending, which is wrong for 6.9
REM against 6.10, so compare version components instead.
set "BEST_VER=0"
for /d %%V in ("C:\Qt\6.*") do (
    for /f "tokens=1-3 delims=." %%a in ("%%~nxV") do (
        set "VER=0"
        set /a "VER=%%a*1000000 + %%b*1000 + %%c" 2>nul
        if !VER! gtr !BEST_VER! (
            for /d %%A in ("%%~fV\msvc*_64") do (
                if exist "%%~fA\lib\cmake\Qt6\Qt6Config.cmake" (
                    set "BEST_VER=!VER!"
                    set "QT=%%~fA"
                )
            )
        )
    )
)
if "%QT%"=="" (
    echo error: no MSVC Qt found under C:\Qt. Pass --qt DIR. 1>&2
    exit /b 1
)

:qt_found
REM Normalize: absolute, no trailing slash.
for %%Q in ("%QT%\.") do set "QT=%%~fQ"

if not exist "%QT%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo error: %QT% is not a Qt 6 prefix ^(no lib\cmake\Qt6^). 1>&2
    exit /b 1
)
if not exist "%QT%\lib\Qt6Core.lib" (
    echo error: %QT% is not an MSVC build of Qt ^(no lib\Qt6Core.lib^). 1>&2
    echo   Use the msvc2022_64 kit, not mingw_64. 1>&2
    exit /b 1
)
if not exist "%QT%\lib\cmake\Qt6ShaderTools" (
    echo error: Qt at %QT% has no Shader Tools module. 1>&2
    echo   Qt Maintenance Tool: Qt %QT_VERSION_DEFAULT% ^> Additional Libraries ^> Qt Shader Tools 1>&2
    echo   or: aqt install-qt windows desktop %QT_VERSION_DEFAULT% win64_msvc2022_64 --noarchives -m qtshadertools -O C:\Qt 1>&2
    exit /b 1
)

REM Qt DLLs for the tests and --run; Qt finds its plugins relative to Qt6Core.dll.
set "PATH=%QT%\bin;%PATH%"

REM --- MSVC x64 -----------------------------------------------------------------

REM The plain "Developer Command Prompt" targets x86, which cannot link against
REM x64 Qt. Load vcvars64 unless an x64 environment is already in place.
if /i "%VSCMD_ARG_TGT_ARCH%"=="x64" goto msvc_ready
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo error: vswhere.exe not found. Install Visual Studio 2022 or its Build Tools 1>&2
    echo   with "Desktop development with C++". 1>&2
    exit /b 1
)
set "VSDIR="
for /f "usebackq tokens=* delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%I"
if "%VSDIR%"=="" (
    echo error: no Visual Studio install with the x64 C++ tools found. 1>&2
    exit /b 1
)
echo ==^> Loading MSVC x64 environment from %VSDIR%
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:msvc_ready
where cl >nul 2>&1 || (echo error: cl.exe not on PATH after loading MSVC. 1>&2 & exit /b 1)

REM --- CMake and Ninja ------------------------------------------------------------

for %%Q in ("%QT%\..\..") do set "QT_ROOT=%%~fQ"
where cmake >nul 2>&1 || if exist "%QT_ROOT%\Tools\CMake_64\bin\cmake.exe" set "PATH=%QT_ROOT%\Tools\CMake_64\bin;%PATH%"
where ninja >nul 2>&1 || if exist "%QT_ROOT%\Tools\Ninja\ninja.exe" set "PATH=%QT_ROOT%\Tools\Ninja;%PATH%"
where cmake >nul 2>&1 || (echo error: cmake not found ^(PATH, Visual Studio, %QT_ROOT%\Tools\CMake_64^). 1>&2 & exit /b 1)
where ninja >nul 2>&1 || (echo error: ninja not found ^(PATH, Visual Studio, %QT_ROOT%\Tools\Ninja^). 1>&2 & exit /b 1)

REM --- Build directory --------------------------------------------------------------

if "%BUILD_TYPE%"=="Debug" (set "BUILD_DIR=!ROOT!\build-debug") else (set "BUILD_DIR=!ROOT!\build")
set "CACHE=%BUILD_DIR%\CMakeCache.txt"

if "%CLEAN%"=="1" goto do_clean
if not exist "%CACHE%" goto configure

REM CMake caches paths with forward slashes.
set "WANT_SRC=%ROOT:\=/%"
set "WANT_QT=%QT:\=/%"
set "CACHED_SRC="
set "CACHED_QT="
set "CACHED_GEN="
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"CMAKE_HOME_DIRECTORY:" "%CACHE%"') do set "CACHED_SRC=%%B"
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"CMAKE_PREFIX_PATH:" "%CACHE%"') do set "CACHED_QT=%%B"
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"CMAKE_GENERATOR:" "%CACHE%"') do set "CACHED_GEN=%%B"
if defined CACHED_SRC set "CACHED_SRC=!CACHED_SRC:\=/!"
if defined CACHED_QT set "CACHED_QT=!CACHED_QT:\=/!"

set "REASON="
if /i not "!CACHED_SRC!"=="%WANT_SRC%" (
    set "REASON=it was configured from !CACHED_SRC!"
) else if /i not "!CACHED_QT!"=="%WANT_QT%" (
    set "REASON=it was configured for Qt at !CACHED_QT!"
) else if not "!CACHED_GEN!"=="Ninja" (
    set "REASON=it uses the !CACHED_GEN! generator"
)
if "!REASON!"=="" goto configure
echo.
echo ==^> Wiping %BUILD_DIR%: !REASON!

:do_clean
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"

REM --- Configure, build, test --------------------------------------------------------

:configure
echo.
echo ==^> Configuring ^(%BUILD_TYPE%, Qt %QT%^)
cmake -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_PREFIX_PATH="%QT%" || exit /b 1

echo.
echo ==^> Building
cmake --build "%BUILD_DIR%" --parallel || exit /b 1

if "%RUN_TESTS%"=="0" goto package
echo.
echo ==^> Testing
ctest --test-dir "%BUILD_DIR%" --output-on-failure || exit /b 1

REM --- Package -------------------------------------------------------------------------

:package
if "%MAKE_ZIP%"=="0" goto run
echo.
echo ==^> Packaging zip
set "STAGE=%ROOT%\dist\windows"
set "OUTPUT=%ROOT%\dist\Easel-windows-x64.zip"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
if exist "%OUTPUT%" del /q "%OUTPUT%"
if not exist "%ROOT%\dist" mkdir "%ROOT%\dist"
REM Qt's deploy script (windeployqt) needs an absolute install prefix.
cmake --install "%BUILD_DIR%" --prefix "%STAGE%" || exit /b 1
powershell -NoProfile -Command "Compress-Archive -Path '%STAGE%\*' -DestinationPath '%OUTPUT%' -Force" || exit /b 1
echo Wrote %OUTPUT%

REM --- Run -------------------------------------------------------------------------------

:run
if "%RUN_APP%"=="0" goto done
echo.
echo ==^> Launching
start "" "%BUILD_DIR%\src\app\easel.exe"

:done
endlocal
exit /b 0
