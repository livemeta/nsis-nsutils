@echo off
REM | syntax: *.bat <nsis_src_root_dir>

if "%~1" equ ""					echo SYNTAX: %~nx0 ^<nsis_src_root_dir^> && pause && exit /B 57
if not exist "%~1\SConstruct"	echo ERROR: "%~1" is not an NSIS directory && pause && exit /B 57
if not exist "%~1\Contrib"		echo ERROR: "%~1" is not an NSIS directory && pause && exit /B 57

echo.
echo ***********************
echo ** NSutils
echo ***********************

mkdir							"%~1\Contrib\NSutils" 2> NUL
xcopy "%~dp0\*.h"				"%~1\Contrib\NSutils" /DYF
xcopy "%~dp0\*.c"				"%~1\Contrib\NSutils" /DYF
xcopy "%~dp0\*.cpp"				"%~1\Contrib\NSutils" /DYF
xcopy "%~dp0\*.rc"				"%~1\Contrib\NSutils" /DYF
xcopy "%~dp0\*Readme.txt"		"%~1\Contrib\NSutils" /DYF
xcopy "%~dp0\SConscript"		"%~1\Contrib\NSutils" /DYF

REM pause
exit /B 0