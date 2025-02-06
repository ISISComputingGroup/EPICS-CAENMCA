TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

ifneq ($(findstring linux,$(EPICS_HOST_ARCH)),)
CAENMCALIB = $(ICPBINARYDIR)/CAENMCA/Linux
CAENARCH = x64
else
CAENMCALIB = $(ICPBINARYDIR)/CAENMCA/Windows
endif
LIBRARY_IOC += CAENMCASup

PROD_HOST += filereader fileconverter
filereader_SRC += filereader.cpp
filereader_LIBS += $(EPICS_BASE_HOST_LIBS)

fileconverter_SRC += fileconverter.cpp
#fileconverter_LIBS += hdf5_hl hdf5 szip zlib jpeg
fileconverter_LIBS += $(LIB_LIBS)
fileconverter_SYS_LIBS += $(LIB_SYS_LIBS)
fileconverter_LIBS += $(EPICS_BASE_HOST_LIBS)

ifeq ($(STATIC_BUILD), NO)
    USR_CXXFLAGS_WIN32    += -DH5_BUILT_AS_DYNAMIC_LIB
    USR_CFLAGS_WIN32      += -DH5_BUILT_AS_DYNAMIC_LIB
else
    USR_CXXFLAGS_WIN32    += -DH5_BUILT_AS_STATIC_LIB
    USR_CFLAGS_WIN32      += -DH5_BUILT_AS_STATIC_LIB
endif

# xxxRecord.h will be created from xxxRecord.dbd
#DBDINC += xxxRecord
# install CAENMCA.dbd into <top>/dbd
DBD += CAENMCA.dbd

# specify all source files to be compiled and added to the library
CAENMCASup_SRCS += CAENMCADriver.cpp

CAENMCASup_LIBS += asyn
CAENMCASup_LIBS += $(EPICS_BASE_IOC_LIBS)

ifneq ($(findstring windows,$(EPICS_HOST_ARCH)),)
CAENARCH = x64
endif
ifneq ($(findstring win32,$(EPICS_HOST_ARCH)),)
CAENARCH = Win32
endif

USR_CFLAGS += -I$(CAENMCALIB)/include
USR_CPPFLAGS += -I$(CAENMCALIB)/include
USR_INCLUDES += -I"$(AREA_DETECTOR)"
USR_CXXFLAGS += -DNOMINMAX

# under linux we make CAENMCA a sys lib, this is so we 
# get passed a -Wl,dynamic flag in all builds and avoids us
# needing to provide a static library for CAENMCA. 
CAENMCA_DIR = $(CAENMCALIB)/lib/$(CAENARCH)
CAENMCASup_LIBS_WIN32 += CAENMCA
CAENMCASup_SYS_LIBS_Linux += CAENMCA

CAENMCASup_SYS_LIBS_Linux += xml2

BIN_INSTALLS_WIN32 += $(CAENMCALIB)/lib/$(CAENARCH)/CAENMCA.dll $(CAENMCALIB)/lib/$(CAENARCH)/CAENUtility.dll $(CAENMCALIB)/lib/$(CAENARCH)/libxml2.dll
LIB_INSTALLS_WIN32 += $(CAENMCALIB)/lib/$(CAENARCH)/CAENMCA.lib

LIB_INSTALLS_Linux += $(CAENMCALIB)/lib/$(CAENARCH)/libCAENMCA.so $(CAENMCALIB)/lib/$(CAENARCH)/libCAENUtility.so

#===========================

include $(ADCORE)/ADApp/commonLibraryMakefile

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
