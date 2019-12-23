@echo off
if exist "W:\GIT\NSIS\NSISbin"      set NSIS_PATH=W:\GIT\NSIS\NSISbin& goto :build
if exist "%PROGRAMFILES%\NSIS"      set NSIS_PATH=%PROGRAMFILES%\NSIS& goto :build
if exist "%PROGRAMFILES(X86)%\NSIS" set NSIS_PATH=%PROGRAMFILES(X86)%\NSIS& goto :build
echo ERROR: NSIS not found & pause & exit /B 2

:build
"%NSIS_PATH%\makensis.exe" /V4 "%~dp0\NSutils-Test.nsi"
if %errorlevel% neq 0 pause & exit /B %errorlevel%

REM echo ----------------------------------------------------------
REM set exe=NSutils-Test-x86-unicode.exe
REM set /P answer=Execute %exe% (y/N)? 
REM if /I "%answer%" equ "y" "%~dp0\%exe%"
