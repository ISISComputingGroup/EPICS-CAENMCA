TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

### NOTE: there should only be one build.mak for a given IOC family and this should be located in the ###-IOC-01 directory

#=============================
# Build the IOC application CAENMCATest
# We actually use $(APPNAME) below so this file can be included by multiple IOCs

PROD_IOC = $(APPNAME)
# CAENMCATest.dbd will be created and installed
DBD += $(APPNAME).dbd

PROD_NAME = $(APPNAME)
include $(ADCORE)/ADApp/commonDriverMakefile

# CAENMCATest.dbd will be made up from these files:
$(APPNAME)_DBD += base.dbd
$(APPNAME)_DBD += asyn.dbd
$(APPNAME)_DBD += CAENMCA.dbd

# Add all the support libraries needed by this IOC
$(APPNAME)_LIBS += CAENMCASup
$(APPNAME)_LIBS += asyn

# CAENMCATest_registerRecordDeviceDriver.cpp derives from CAENMCATest.dbd
$(APPNAME)_SRCS += $(APPNAME)_registerRecordDeviceDriver.cpp

# Build the main IOC entry point on workstation OSs.
$(APPNAME)_SRCS_DEFAULT += $(APPNAME)Main.cpp
$(APPNAME)_SRCS_vxWorks += -nil-

# Add support from base/src/vxWorks if needed
#$(APPNAME)_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Finally link to the EPICS Base libraries
$(APPNAME)_LIBS += $(EPICS_BASE_IOC_LIBS)

# under linux we make CAENMCA a sys lib, this is so we 
# get passed a -Wl,dynamic flag in all builds and avoids us
# needing to provide a static library for CAENMCA. 
$(APPNAME)_LIBS_WIN32 += CAENMCA
$(APPNAME)_SYS_LIBS_Linux += CAENMCA CAENUtility
$(APPNAME)_SYS_LIBS_Linux += xml2

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

