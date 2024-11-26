@echo off
setlocal
set "PATH=C:\Windows\System32;C:\Windows;C:\Windows\System32\wbem"

set "SRCDIR=%1"
set "FILEPREFIX=%2"
set "RUNNUMBER=%3"

set "DSTDIR=\\olympic\babylon5\scratch\freddie"

REM wait for files to close
REM if we try to MOVE too early file is still in use
ping 127.0.0.1 -n 30 > nul

@echo Copying to %DSTDIR%

REM move data files
robocopy "%SRCDIR%" "%DSTDIR%" "%FILEPREFIX%%RUNNUMBER%_*.*" /MOV /NJH /NJS /NP /R:2
robocopy "%TOP%\iocBoot\%IOC%" "%DSTDIR%" "%FILEPREFIX%%RUNNUMBER%_*.*" /MOV /NJH /NJS /NP /R:2

REM update journal file
robocopy "%TOP%\iocBoot\%IOC%" "%DSTDIR%" "journal_*.txt" /NJH /NJS /NP /R:2
