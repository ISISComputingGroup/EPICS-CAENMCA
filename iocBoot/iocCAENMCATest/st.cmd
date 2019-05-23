#!../../bin/win32-x86/CAENMCATest

## You may have to change CAENMCATest to something else
## everywhere it appears in this file

< envPaths

## Register all support components
dbLoadDatabase("../../dbd/CAENMCATest.dbd",0,0)
CAENMCATest_registerRecordDeviceDriver(pdbbase) 

< st-common.cmd
