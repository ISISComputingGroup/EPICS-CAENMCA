TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

CAENMCALIB = $(ICPBINARYDIR)/CAENMCA/Windows

LIBRARY_IOC += CAENMCASup

# xxxRecord.h will be created from xxxRecord.dbd
#DBDINC += xxxRecord
# install CAENMCA.dbd into <top>/dbd
DBD += CAENMCA.dbd

# specify all source files to be compiled and added to the library
CAENMCASup_SRCS += CAENMCADriver.cpp

CAENMCASup_LIBS += asyn CAENMCA
CAENMCASup_LIBS += $(EPICS_BASE_IOC_LIBS)

ifneq ($(findstring windows,$(EPICS_HOST_ARCH)),)
CAENARCH = x64
endif
ifneq ($(findstring win32,$(EPICS_HOST_ARCH)),)
CAENARCH = Win32
endif

USR_CFLAGS += -I$(CAENMCALIB)/include
USR_CPPFLAGS += -I$(CAENMCALIB)/include

CAENMCA_DIR = $(CAENMCALIB)/lib/$(CAENARCH)

BIN_INSTALLS_WIN32 += $(CAENMCALIB)/lib/$(CAENARCH)/CAENMCA.dll $(CAENMCALIB)/lib/$(CAENARCH)/CAENUtility.dll $(CAENMCALIB)/lib/$(CAENARCH)/libxml2.dll
LIB_INSTALLS_WIN32 += $(CAENMCALIB)/lib/$(CAENARCH)/CAENMCA.lib

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
