REM :: Marius Negrutiu (marius.negrutiu@protonmail.com)

@echo off
echo.
SetLocal

:: This script builds the project by directly calling cl.exe
:: The sln/vcxproj files are ignored

cd /d "%~dp0"

set OUTNAME=NSutils
set RCNAME=NSutils

if not exist "%PF%" set PF=%PROGRAMFILES(X86)%
if not exist "%PF%" set PF=%PROGRAMFILES%

set VSWHERE=%PF%\Microsoft Visual Studio\Installer\vswhere.exe
if exist "%VSWHERE%" for /f "usebackq tokens=1* delims=: " %%i in (`"%VSWHERE%" -version 15 -requires Microsoft.Component.MSBuild`) do if /i "%%i"=="installationPath" set VCVARSALL=%%j\VC\Auxiliary\Build\VCVarsAll.bat
if exist "%VCVARSALL%" goto :BUILD

set VCVARSALL=%PF%\Microsoft Visual Studio 14.0\VC\VcVarsAll.bat
if exist "%VCVARSALL%" goto :BUILD

set VCVARSALL=%PF%\Microsoft Visual Studio 12.0\VC\VcVarsAll.bat
if exist "%VCVARSALL%" goto :BUILD

set VCVARSALL=%PF%\Microsoft Visual Studio 11.0\VC\VcVarsAll.bat
if exist "%VCVARSALL%" goto :BUILD

set VCVARSALL=%PF%\Microsoft Visual Studio 10.0\VC\VcVarsAll.bat
if exist "%VCVARSALL%" goto :BUILD

set VCVARSALL=%PF%\Microsoft Visual Studio 9.0\VC\VcVarsAll.bat
if exist "%VCVARSALL%" goto :BUILD

echo ERROR: Can't find Visual Studio 2008/2010/2012/2013/2015/2017
pause
exit /B 2

:BUILD
pushd "%CD%"
call "%VCVARSALL%" x86
popd

echo -----------------------------------
echo x86-ansi
echo -----------------------------------
set OUTDIR=ReleaseA-nocrt
set BUILD_MACHINE=X86
call :BUILD_PARAMS
set CL=/D "_MBCS" /arch:SSE %CL%
set LINK=/MACHINE:X86 /SAFESEH %LINK%
call :BUILD_CL
if %ERRORLEVEL% neq 0 pause && exit /B %ERRORLEVEL%

echo -----------------------------------
echo x86-unicode
echo -----------------------------------
set OUTDIR=ReleaseW-nocrt
call :BUILD_PARAMS
set CL=/D "_UNICODE" /D "UNICODE" /arch:SSE %CL%
set LINK=/MACHINE:X86 /SAFESEH %LINK%
call :BUILD_CL
if %ERRORLEVEL% neq 0 pause && exit /B %ERRORLEVEL%

:BUILD64
pushd "%CD%"
call "%VCVARSALL%" amd64
popd

echo -----------------------------------
echo amd64-unicode
echo -----------------------------------
set OUTDIR=ReleaseW-nocrt-amd64
call :BUILD_PARAMS
set CL=/D "_UNICODE" /D "UNICODE" %CL%
set LINK=/MACHINE:AMD64 %LINK%
call :BUILD_CL
if %ERRORLEVEL% neq 0 pause && exit /B %ERRORLEVEL%

:: Finish
exit /B 0


:BUILD_PARAMS
set CL=^
	/nologo ^
	/Zi ^
	/W3 /WX- ^
	/O2 /Os /Oy- ^
	/D WIN32 /D NDEBUG /D _WINDOWS /D _USRDLL /D _WINDLL ^
	/Gm- /EHsc /MT /GS- /Gd /TC /GF /FD /LD ^
	/Fo".\%OUTDIR%\temp\\" ^
	/Fd".\%OUTDIR%\temp\\" ^
	/Fe".\%OUTDIR%\%OUTNAME%"

set LINK=^
	/NOLOGO ^
	/NODEFAULTLIB ^
	/DYNAMICBASE /NXCOMPAT ^
	/DEBUG ^
	/OPT:REF ^
	/OPT:ICF ^
	/INCREMENTAL:NO ^
	/MANIFEST:NO ^
	/ENTRY:"DllMain" ^
	kernel32.lib user32.lib version.lib advapi32.lib shlwapi.lib gdi32.lib ole32.lib uuid.lib oleaut32.lib msimg32.lib ^
	".\%OUTDIR%\temp\%RCNAME%.res"

set FILES=^
	"main.c" ^
	"verinfo.c" ^
	"registry.c" ^
	"utils.c" ^
	"strblock.c" ^
	"gdi.c" ^
	"handles.c" ^
	"nsiswapi\pluginapi.c"

exit /B 0


:BUILD_CL
echo.
if not exist "%~dp0\%OUTDIR%"      mkdir "%~dp0\%OUTDIR%"
if not exist "%~dp0\%OUTDIR%\temp" mkdir "%~dp0\%OUTDIR%\temp"

echo %RCNAME%.rc
rc.exe /l"0x0409" /Fo".\%OUTDIR%\temp\%RCNAME%.res" "%RCNAME%.rc"
if %errorlevel% neq 0 exit /B %errorlevel%

cl.exe %FILES%
if %errorlevel% neq 0 exit /B %errorlevel%

exit /B 0
