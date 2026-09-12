@echo off
setlocal EnableDelayedExpansion
set "PATH=C:\Windows\System32;C:\Windows;C:\Windows\System32\wbem"

set count=0
for %%a in (%*) do (
    set /a count+=1
    set "ARG!count!=%%a"
)

REM assumes TOP env var set by IOC calling this
set "WINTOP=%TOP:/=\%"

REM Update any files if needed from central area (cycle number for example)
xcopy /d /y /i o:\setcycle.cmd "%TMP%"
REM now use the cycle number
CALL "%TMP%\setcycle.cmd"

set "DSTDIR=d:\data\%CYCLE%"
set "DSTDIREXTRA=d:\data\%CYCLE%\autoreduced"
set "JOURNALDIR=d:\logs\journal"
REM for testing
REM set "DSTDIR=\\olympic\babylon5\scratch\freddie"
REM set "JOURNALDIR=\\olympic\babylon5\scratch\freddie"

REM copy journal files across to archive
if not exist %JOURNALDIR% (
	md %JOURNALDIR%
)

REM now send to archiver (creating the cycle directory if needed)
if not exist %DSTDIR% (
	md %DSTDIR%
)
if not exist %DSTDIREXTRA% (
	md %DSTDIREXTRA%
)

set "DATAFILE=%ARG1%"
set "FILEPREFIX=%ARG2%"
set "RUNNUMBER=%ARG3%"
set "DEV0=%ARG4%"
set "CHANNEL0=%ARG5%"
set "SRCDIR0=%ARG6%"
set "FILE0=%ARG7%"
set "SCALEA0=%ARG8%"
set "SCALEB0=%ARG9%"
set "DEV1=%ARG10%"
set "CHANNEL1=%ARG11%"
set "SRCDIR1=%ARG12%"
set "FILE1=%ARG13%"
set "SCALEA1=%ARG14%"
set "SCALEB1=%ARG15%"
set "DEV2=%ARG16%"
set "CHANNEL2=%ARG17%"
set "SRCDIR2=%ARG18%"
set "FILE2=%ARG19%"
set "SCALEA2=%ARG20%"
set "SCALEB2=%ARG21%"
set "DEV3=%ARG22%"
set "CHANNEL3=%ARG23%"
set "SRCDIR3=%ARG24%"
set "FILE3=%ARG25%"
set "SCALEA3=%ARG26%"
set "SCALEB3=%ARG27%"

REM wait for files to close
"%HIDEWINDOW%\bin\%EPICS_HOST_ARCH%\CheckFileAccess.exe" "%SRCDIR0%\%FILE0%" "R" ""
"%HIDEWINDOW%\bin\%EPICS_HOST_ARCH%\CheckFileAccess.exe" "%SRCDIR1%\%FILE1%" "R" ""
"%HIDEWINDOW%\bin\%EPICS_HOST_ARCH%\CheckFileAccess.exe" "%SRCDIR2%\%FILE2%" "R" ""
"%HIDEWINDOW%\bin\%EPICS_HOST_ARCH%\CheckFileAccess.exe" "%SRCDIR3%\%FILE3%" "R" ""

call %~dp0run_converter.bat %*

xcopy /y /v /d /c "c:\data\%FILEPREFIX%%RUNNUMBER%_hex*_info.txt" "C:\Data\Export only"
xcopy /y /v /d /c "c:\data\journal_*.txt" "C:\Data\Export only"
xcopy /y /v /d /c "%DATAFILE%" "C:\Data\Export only"

@echo Moving files to %DSTDIR%

REM update journal files
robocopy "c:\data" "%JOURNALDIR%" "journal_*.txt" /NJH /NJS /NP /copy:DT /XX /R:2
robocopy "c:\data" "%DSTDIR%" "%FILEPREFIX%%RUNNUMBER%_hex*_info.txt" /MOV /NJH /NJS /NP /copy:DT /XX /R:100

REM move hexagon original data files
robocopy "%SRCDIR0%" "%DSTDIREXTRA%" "%FILE0%" /MOV /NJH /NJS /NP /copy:DT /XX /R:100
robocopy "%SRCDIR1%" "%DSTDIREXTRA%" "%FILE1%" /MOV /NJH /NJS /NP /copy:DT /XX /R:100
robocopy "%SRCDIR2%" "%DSTDIREXTRA%" "%FILE2%" /MOV /NJH /NJS /NP /copy:DT /XX /R:100
robocopy "%SRCDIR3%" "%DSTDIREXTRA%" "%FILE3%" /MOV /NJH /NJS /NP /copy:DT /XX /R:100

REM get nexus file name parts
for %%I in ( %DATAFILE% ) do set "DATAFILEDIR=%%~dI%%~pI"
for %%I in ( %DATAFILE% ) do set "DATAFILENAME=%%~nI%%~xI"
REM need to remove "" from DATAFILEDIR in robocopy due to trailing \ in path
robocopy %DATAFILEDIR% "%DSTDIR%" "%DATAFILENAME%" /MOV /NJH /NJS /NP /copy:DT /XX /R:100
