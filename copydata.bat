REM @echo off
setlocal
set "PATH=C:\Windows\System32;C:\Windows;C:\Windows\System32\wbem"

set "SRCDIR=%1"
set "FILEPREFIX=%2"
set "RUNNUMBER=%3"
set "CHANNEL=%4"
set "FILENAME=%5"
set "SCALEA=%6"
set "SCALEB=%7"

REM assumes TOP env var set by IOC calling this
set "WINTOP=%TOP:/=\%"

REM Update any files if needed from central area (cycle number for example)
xcopy /d /y /i o:\setcycle.cmd "%TMP%"
REM now use the cycle number
CALL "%TMP%\setcycle.cmd"

set "DSTDIR=d:\data\%CYCLE%\autoreduced"
set "JOURNALDIR=d:\logs\journal"
set "NXS_FILE=%FILENAME:.bin=.nxs%"

REM for testing
REM set "DSTDIR=\\olympic\babylon5\scratch\freddie"
REM set "JOURNALDIR=\\olympic\babylon5\scratch\freddie"
REM set "SRCDIR=%WINTOP%"
REM set "FILENAME=freddie_ch%CHANNEL%.bin"

REM copy journal files across to archive
if not exist %JOURNALDIR% (
	md %JOURNALDIR%
)

REM now send to archiver (creating the cycle directory if needed)
if not exist %DSTDIR% (
	md %DSTDIR%
)

REM wait for files to close
"%~dp0CheckFileAccess.exe" "%SRCDIR%\%FILENAME%" "R" ""

call %~dp0run_converter.bat %SRCDIR%\%FILENAME% %WINTOP%\%NXS_FILE% %SCALEA% %SCALEB% 
@echo Moving files to %DSTDIR%

REM update journal files - only do for CHANNEL 0 as we get called for channel 0 and 1
REM and don't want to do this twice
if "%CHANNEL%" == "0" (
    robocopy "%WINTOP%\iocBoot\%IOC%" "%JOURNALDIR%" "journal_*.txt" /NJH /NJS /NP /R:2 /copy:DT
    robocopy "%WINTOP%\iocBoot\%IOC%" "%DSTDIR%" "%FILEPREFIX%%RUNNUMBER%_*.*" /MOV /NJH /NJS /NP /copy:DT
)

REM move data files
robocopy "%SRCDIR%" "%DSTDIR%" "%FILENAME%" /MOV /NJH /NJS /NP /copy:DT
robocopy "%WINTOP%" "%DSTDIR%" "%NXS_FILE%" /MOV /NJH /NJS /NP /copy:DT
