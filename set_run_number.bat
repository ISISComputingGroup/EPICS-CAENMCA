@echo off
setlocal
call %~dp0..\..\..\config_env.bat >NUL
set /p VAL=Enter run number to assign:
CHOICE /C YN /M "Press Y to assign run number of '%VAL%' or N to cancel"
if %errorlevel% neq 1 exit /b 1
caput %MYPVPREFIX%HEX0:_IRUNNUMBER %VAL%
caput %MYPVPREFIX%HEX1:_IRUNNUMBER %VAL%
pause
