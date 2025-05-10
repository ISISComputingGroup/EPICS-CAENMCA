REM @echo off
setlocal
set "PATH=C:\Windows\System32;C:\Windows;C:\Windows\System32\wbem"

set "SRCDIR=%1"
set "FILEPREFIX=%2"
set "RUNNUMBER=%3"
set "FILENAME=%4"
set "SCALEA=%5"
set "SCALEB=%6"
REM assumes TOP env var set by IOC calling this

REM Update any files if needed from central area (cycle number for example)
xcopy /d /y /i o:\setcycle.cmd "%TMP%"
REM now use the cycle number
CALL "%TMP%\setcycle.cmd"

set "DSTDIR=d:\data\%CYCLE%\autoreduced"
set "JOURNALDIR=d:\logs\journal"
set "DSTDIR=\\olympic\babylon5\scratch\freddie"
set "JOURNALDIR=\\olympic\babylon5\scratch\freddie"

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
NXS_FILE="%FILENAME:.bin=.nxs%"

call %~dp0run_converter.bat %SRCDIR%\%FILENAME% %TOP%\%NXS_FILE% %SCALEA% %SCALEB% 
@echo Moving to %DSTDIR%

REM update journal file
robocopy "%TOP%\iocBoot\%IOC%" "%JOURNALDIR%" "journal_*.txt" /NJH /NJS /NP /R:2 /copy:DT

REM move data files
robocopy "%TOP%\iocBoot\%IOC%" "%DSTDIR%" "%FILEPREFIX%%RUNNUMBER%_*.*" /MOV /NJH /NJS /NP /copy:DT
robocopy "%SRCDIR%" "%DSTDIR%" "%FILENAME%" /MOV /NJH /NJS /NP  /copy:DT
robocopy "%TOP%" "%DSTDIR%" "%NXS_FILE%" /MOV /NJH /NJS /NP  /copy:DT
