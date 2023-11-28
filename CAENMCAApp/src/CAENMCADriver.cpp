/*************************************************************************\
* Copyright (c) 2013 Science and Technology Facilities Council (STFC), GB.
* All rights reverved.
* This file is distributed subject to a Software License Agreement found
* in the file LICENSE.txt that is included with this distribution.
\*************************************************************************/

/// @file CAENMCADriver.cpp Implementation of #CAENMCADriver class and CAENMCAConfigure() iocsh command

//#pragma warning(push, 0)

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <cstdint>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>
#include <tuple>
#include <map>
#include <sys/stat.h>

#include <epicsTypes.h>
#include <epicsTime.h>
#include <epicsThread.h>
#include <epicsString.h>
#include <epicsTimer.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <errlog.h>
#include <iocsh.h>
#include <epicsExit.h>

#include <asynPortDriver.h>

#include <CAENMCA.h>

#include <epicsExport.h>

#include "CAENMCADriver.h"

#ifndef _WIN32
#define _fsopen(a,b,c) fopen(a,b)
#define _ftelli64 ftell
#define _fseeki64 fseek
#endif /* ndef _WIN32 */

#define MAX_ENERGY_BINS  32768   /* need to check energy bin bits? */

static const char *driverName = "CAENMCADriver"; ///< Name of driver for use in message printing 

/// EPICS driver report function for iocsh dbior command
void CAENMCADriver::report(FILE* fp, int details)
{
    uint32_t val0 = 0, val1 = 0;
    readRegister(0x10B8, val0);
    readRegister(0x11B8, val1);
    fprintf(fp, "0x1nB8 register for setting timing is: %u %u\n", val0, val1);
	ADDriver::report(fp, details);
}

void CAENMCADriver::setADAcquire(int addr, int acquire)
{
    int adstatus;
    int acquiring;
    int imageMode;
    asynStatus status = asynSuccess;

    /* Ensure that ADStatus is set correctly before we set ADAcquire.*/
		getIntegerParam(addr, ADStatus, &adstatus);
		getIntegerParam(addr, ADAcquire, &acquiring);
		getIntegerParam(addr, ADImageMode, &imageMode);
		  if (acquire && !acquiring) {
			setStringParam(addr, ADStatusMessage, "Acquiring data");
			setIntegerParam(addr, ADStatus, ADStatusAcquire); 
			setIntegerParam(addr, ADAcquire, 1); 
            setIntegerParam(addr, ADNumImagesCounter, 0);
		  }
		  if (!acquire && acquiring) {
			setIntegerParam(addr, ADAcquire, 0); 
			setStringParam(addr, ADStatusMessage, "Acquisition stopped");
			if (imageMode == ADImageContinuous) {
			  setIntegerParam(addr, ADStatus, ADStatusIdle);
			} else {
			  setIntegerParam(addr, ADStatus, ADStatusAborted);
			}
		  }
}

void CAENMCADriver::setShutter(int addr, int open)
{
    int shutterMode;

    getIntegerParam(addr, ADShutterMode, &shutterMode);
    if (shutterMode == ADShutterModeDetector) {
        /* Simulate a shutter by just changing the status readback */
        setIntegerParam(addr, ADShutterStatus, open);
    } else {
        /* For no shutter or EPICS shutter call the base class method */
        ADDriver::setShutter(open);
    }
}

static std::string translateCAENError(int code)
{
	switch (code)
	{
	case CAEN_MCA_RetCode_Success:
		return "Success";
		break;
	case CAEN_MCA_RetCode_Generic:
		return "Generic error";
		break;
	case CAEN_MCA_RetCode_SockInit:
		return "Socket initialization error";
		break;
	case CAEN_MCA_RetCode_SockConnect:
		return "Socket connect error";
		break;
	case CAEN_MCA_RetCode_OutOfMemory:
		return "ut of memory (malloc failed)";
		break;
	case CAEN_MCA_RetCode_Handle:
		return "Invalid handle";
		break;
	case CAEN_MCA_RetCode_Argument:
		return "Invalid argument";
		break;
	case CAEN_MCA_RetCode_SocketSend:
		return "TCP/IP send error";
		break;
	case CAEN_MCA_RetCode_SocketReceive:
		return "TCP/IP receive error";
		break;
	case CAEN_MCA_RetCode_Protocol:
		return "Protocol error";
		break;
	case CAEN_MCA_RetCode_Serialize:
		return "Serialize error";
		break;
	case CAEN_MCA_RetCode_Deserialize:
		return "Deserialize error";
		break;
	case CAEN_MCA_RetCode_Parameter:
		return "Parameter error";
		break;
	case CAEN_MCA_RetCode_ParameterValue:
		return "Parameter Value error";
		break;
	case CAEN_MCA_RetCode_LibraryLoad:
		return "Library dynamic load error";
		break;
	case CAEN_MCA_RetCode_DiscoveryFunction:
		return "SSDP discovery failed";
		break;
	case CAEN_MCA_RetCode_NotConnected:
		return "Not connected";
		break;
	case CAEN_MCA_RetCode_NotSupported:
		return "Not supported";
		break;
	case CAEN_MCA_RetCode_NotYetImplemented:
		return "Not yet implemented";
		break;
	case CAEN_MCA_RetCode_CollectionFull:
		return "DHandle collection full";
		break;
	case CAEN_MCA_RetCode_Map:
		return "Error in a map";
		break;
	default:
	    {
			std::ostringstream oss;
	        oss << "Unknown error: " << code;
	        return oss.str();
		}
		break;
	}
}

class CAENMCAException : public std::runtime_error
{
	public:
	explicit CAENMCAException(const std::string& what_arg) : std::runtime_error(what_arg) { }
    explicit CAENMCAException(const std::string& message, int caen_errcode) : std::runtime_error(message + ": " + translateCAENError(caen_errcode)) { }    
};

#define ERROR_CHECK(__func, __ret) \
    if ( (__ret) != CAEN_MCA_RetCode_Success ) { \
        throw CAENMCAException(__func, __ret); \
    }

struct CAENMCA
{
    static bool simulate;

    static CAEN_MCA_HANDLE OpenDevice(const std::string& path, int32_t* index)
    {
        CAEN_MCA_HANDLE h = NULL;
        if (!simulate) {
            int32_t retcode;
            h = CAEN_MCA_OpenDevice(path.c_str(), &retcode, index);
            ERROR_CHECK("CAENMCA::OpenDevice()", retcode);
        } else {
            std::cerr << "Opening simulated device for " << path << std::endl;
        }
        return h;
    }

    static void closeDevice(CAEN_MCA_HANDLE handle)
    {
        if (!simulate) {
            CAEN_MCA_CloseDevice(handle);
        }
    }

    static void GetData(CAEN_MCA_HANDLE handle, CAEN_MCA_DataType_t dataType, uint64_t dataMask, ...)
    {
        if (!simulate) {
            va_list args;
            va_start(args, dataMask);
            int32_t retcode = CAEN_MCA_GetDataV(handle, dataType, dataMask, args);
            va_end(args);
            ERROR_CHECK("CAENMCA::GetData()", retcode);
        }
    }

    static void SetData(CAEN_MCA_HANDLE handle, CAEN_MCA_DataType_t dataType, uint64_t dataMask, ...)
    {
        if (!simulate) {
            va_list args;
            va_start(args, dataMask);
            int32_t retcode = CAEN_MCA_SetDataV(handle, dataType, dataMask, args);
            va_end(args);
            ERROR_CHECK("CAENMCA::SetData()", retcode);
        }
    }

    static void SendCommand(CAEN_MCA_HANDLE handle, CAEN_MCA_CommandType_t cmdType, uint64_t cmdMaskIn, uint64_t cmdMaskOut, ...)
    {
        if (!simulate) {
            va_list args;
            va_start(args, cmdMaskOut);
            int32_t retcode = CAEN_MCA_SendCommandV(handle, cmdType, cmdMaskIn, cmdMaskOut, args);
            va_end(args);
            ERROR_CHECK("CAENMCA::SendCommand()", retcode);
        }            
    }

    static CAEN_MCA_HANDLE GetChildHandle(CAEN_MCA_HANDLE handle, CAEN_MCA_HandleType_t handleType, int32_t index)
    {
        CAEN_MCA_HANDLE h = NULL;
        if (!simulate) {
            h = CAEN_MCA_GetChildHandle(handle, handleType, index);
            if (h == NULL)
            {
                throw CAENMCAException("GetChildHandle(): failed");
            }
        }
        return h;
    }
    
    static CAEN_MCA_HANDLE GetChildHandleByName(CAEN_MCA_HANDLE handle, CAEN_MCA_HandleType_t handleType, const std::string& name)
    {
        CAEN_MCA_HANDLE h = NULL;
        if (!simulate) {
            h = CAEN_MCA_GetChildHandleByName(handle, handleType, name.c_str());
            if (h == NULL)
            {
                throw CAENMCAException("GetChildHandleByName(): failed for name \"" + name + "\"");
            }
        }
        return h;
    }
    
    static void getHandlesFromCollection(CAEN_MCA_HANDLE parent, CAEN_MCA_HandleType_t handleType, std::vector<CAEN_MCA_HANDLE>& handles)
    {
        handles.resize(0);
        if (simulate) {
            handles.push_back(NULL);
            handles.push_back(NULL);
            return;
        }
        CAEN_MCA_HANDLE collection = CAENMCA::GetChildHandle(parent, CAEN_MCA_HANDLE_COLLECTION, handleType);
        uint32_t collection_length = 0;
        CAEN_MCA_HANDLE collection_handles[COLLECTION_MAXLEN] = { NULL };
        CAENMCA::GetData(
            collection,
            CAEN_MCA_DATA_COLLECTION,
            DATAMASK_COLLECTION_LENGTH |
            DATAMASK_COLLECTION_HANDLES,
            &collection_length,
            collection_handles
        );
		int32_t type, index;
		std::string name;
        for(int i=0; i<collection_length; ++i)
        {
			getHandleDetails(collection_handles[i], type, index, name);
			if (i != index || type != handleType)
			{
			    std::cerr << "handle collection ERROR" << std::endl;
			}
            handles.push_back(collection_handles[i]);
        }
    }   
	
    static void getHandleDetails(CAEN_MCA_HANDLE handle, int32_t& type, int32_t& index, std::string& name)
	{
        if (simulate) {
            return;
        }
		char name_c[HANDLE_NAME_MAXLEN];
		CAENMCA::GetData(
			handle,
			CAEN_MCA_DATA_HANDLE_INFO,
			DATAMASK_HANDLE_TYPE |
			DATAMASK_HANDLE_INDEX |
			DATAMASK_HANDLE_NAME,
			&type,
			&index,
			name_c
		);
		name = name_c;		
	}
	
};

bool CAENMCA::simulate = false;

/// Constructor for the webgetDriver class.
/// Calls constructor for the asynPortDriver base class and sets up driver parameters.
///
/// \param[in] portName @copydoc initArg0
CAENMCADriver::CAENMCADriver(const char *portName, const char* deviceName)
	: ADDriver(portName,
		2, /* maxAddr */
		NUM_CAEN_PARAMS,
					0, // maxBuffers
					0, // maxMemory
		asynInt32Mask | asynInt32ArrayMask | asynFloat64Mask | asynFloat64ArrayMask | asynOctetMask | asynDrvUserMask, /* Interface mask */
		asynInt32Mask | asynInt32ArrayMask | asynFloat64Mask | asynFloat64ArrayMask | asynOctetMask,  /* Interrupt mask */
		ASYN_CANBLOCK | ASYN_MULTIDEVICE, /* asynFlags.  This driver can block and is multi-device */
		1, /* Autoconnect */
		0, /* Default priority */
		0),	/* Default stack size*/
	m_famcode(CAEN_MCA_FAMILY_CODE_UNKNOWN),m_device_h(NULL),m_old_list_filename(2),m_file_fd(2, std::tuple<FILE*, FILE*>{NULL,NULL}),
    m_event_file_last_pos(2, 0),m_frame_time(2, 0),m_max_event_time(2, 0),m_pRaw(NULL)
{
	const char *functionName = "CAENMCADriver";

	createParam(P_deviceNameString, asynParamOctet, &P_deviceName);
	createParam(P_availableConfigurationsString, asynParamOctet, &P_availableConfigurations);
	createParam(P_configurationString, asynParamOctet, &P_configuration);
	createParam(P_numEnergySpecString, asynParamInt32, &P_numEnergySpec);
	createParam(P_energySpecClearString, asynParamInt32, &P_energySpecClear);
	createParam(P_energySpecString, asynParamInt32Array, &P_energySpec);
	createParam(P_energySpecEventString, asynParamInt32Array, &P_energySpecEvent);
	createParam(P_energySpecEventTMinString, asynParamFloat64, &P_energySpecEventTMin);
	createParam(P_energySpecEventTMaxString, asynParamFloat64, &P_energySpecEventTMax);
	createParam(P_energySpecEventNEventsString, asynParamInt32, &P_energySpecEventNEvents);
	createParam(P_energySpecCountsString, asynParamInt32, &P_energySpecCounts);
	createParam(P_energySpecNBinsString, asynParamInt32, &P_energySpecNBins);
    createParam(P_energySpecFilenameString, asynParamOctet, &P_energySpecFilename);
	createParam(P_energySpecRealtimeString, asynParamFloat64, &P_energySpecRealtime);
	createParam(P_energySpeclivetimeString, asynParamFloat64, &P_energySpeclivetime);
	createParam(P_energySpecdeadtimeString, asynParamFloat64, &P_energySpecdeadtime);
	createParam(P_energySpecOverflowsString, asynParamInt32, &P_energySpecOverflows);
	createParam(P_energySpecUnderflowsString, asynParamInt32, &P_energySpecUnderflows);
	createParam(P_energySpecAutosaveString, asynParamFloat64, &P_energySpecAutosave);
	createParam(P_nEventsString, asynParamInt32, &P_nEvents);
	createParam(P_nEventsProcessedString, asynParamInt32, &P_nEventsProcessed);
	createParam(P_vmonString, asynParamFloat64, &P_vmon);
	createParam(P_vsetString, asynParamFloat64, &P_vset);
	createParam(P_imonString, asynParamFloat64, &P_imon);
	createParam(P_isetString, asynParamFloat64, &P_iset);
	createParam(P_tmonString, asynParamFloat64, &P_tmon);
	createParam(P_rampupString, asynParamFloat64, &P_rampup);
	createParam(P_rampdownString, asynParamFloat64, &P_rampdown);
	createParam(P_hvPolarityString, asynParamInt32, &P_hvPolarity);
	createParam(P_hvStatusString, asynParamInt32, &P_hvStatus);
	createParam(P_hvRangeNameString, asynParamOctet, &P_hvRangeName);
	createParam(P_chanEnabledString, asynParamInt32, &P_chanEnabled);
	createParam(P_chanPolarityString, asynParamInt32, &P_chanPolarity);	
    createParam(P_chanMemFullString, asynParamInt32, &P_chanMemFull);
    createParam(P_chanMemEmptyString, asynParamInt32, &P_chanMemEmpty);
	createParam(P_listFileString, asynParamOctet, &P_listFile);
	createParam(P_listFileSizeString, asynParamFloat64, &P_listFileSize);
	createParam(P_listEnabledString, asynParamInt32, &P_listEnabled);	
	createParam(P_listSaveModeString, asynParamInt32, &P_listSaveMode);	
	createParam(P_listMaxNEventsString, asynParamInt32, &P_listMaxNEvents);	
    createParam(P_acqRunningString, asynParamInt32, &P_acqRunning);
    createParam(P_acqRunningChString, asynParamInt32, &P_acqRunningCh);
	createParam(P_hvOnString, asynParamInt32, &P_hvOn);	
    createParam(P_acqInitString, asynParamInt32, &P_acqInit);
	createParam(P_acqStartModeString, asynParamInt32, &P_acqStartMode);
	createParam(P_restartString, asynParamInt32, &P_restart);
	createParam(P_startAcquisitionString, asynParamInt32, &P_startAcquisition);
	createParam(P_stopAcquisitionString, asynParamInt32, &P_stopAcquisition);	
 	createParam(P_eventsSpecYString, asynParamFloat64Array, &P_eventsSpecY);
 	createParam(P_eventsSpecXString, asynParamFloat64Array, &P_eventsSpecX);
 	createParam(P_eventsSpecNEventsString, asynParamInt32, &P_eventsSpecNEvents);
 	createParam(P_eventsSpecNTriggersString, asynParamInt32, &P_eventsSpecNTriggers);
 	createParam(P_eventsSpecTriggerRateString, asynParamFloat64, &P_eventsSpecTriggerRate);
    createParam(P_eventsSpecNBinsString, asynParamInt32, &P_eventsSpecNBins);
    createParam(P_eventsSpecTMinString, asynParamFloat64, &P_eventsSpecTMin);
    createParam(P_eventsSpecTMaxString, asynParamFloat64, &P_eventsSpecTMax);
    createParam(P_eventsSpecTBinWidthString, asynParamFloat64, &P_eventsSpecTBinWidth);
    createParam(P_eventsSpecNTimeTagRolloverString, asynParamInt32, &P_eventsSpecNTimeTagRollover);
    createParam(P_eventsSpecNTimeTagResetString, asynParamInt32, &P_eventsSpecNTimeTagReset);
    createParam(P_eventsSpecNEventEnergySatString, asynParamInt32, &P_eventsSpecNEventEnergySat);
    createParam(P_eventsSpecMaxEventTimeString, asynParamFloat64, &P_eventsSpecMaxEventTime);
    createParam(P_nFakeEventsString, asynParamInt32, &P_nFakeEvents);
    createParam(P_nImpDynamSatEventString, asynParamInt32, &P_nImpDynamSatEvent);
    createParam(P_nPileupEventString, asynParamInt32, &P_nPileupEvent);
    createParam(P_nEventEnergyOutSCAString, asynParamInt32, &P_nEventEnergyOutSCA);
    createParam(P_nEventDurSatInhibitString, asynParamInt32, &P_nEventDurSatInhibit);
    createParam(P_nEventNotBinnedString, asynParamInt32, &P_nEventNotBinned);
    createParam(P_nEventEnergyDiscardString, asynParamInt32, &P_nEventEnergyDiscard);
    createParam(P_eventSpec2DTimeMinString, asynParamFloat64, &P_eventSpec2DTimeMin);
    createParam(P_eventSpec2DTimeMaxString, asynParamFloat64, &P_eventSpec2DTimeMax);
    createParam(P_eventSpec2DNTimeBinsString, asynParamInt32, &P_eventSpec2DNTimeBins);
    createParam(P_eventSpec2DEnergyBinGroupString, asynParamInt32, &P_eventSpec2DEnergyBinGroup);
    createParam(P_eventSpec2DTBinWidthString, asynParamFloat64, &P_eventSpec2DTBinWidth);
    createParam(P_loadDataFileString, asynParamInt32, &P_loadDataFile);
    createParam(P_loadDataFileStatusString, asynParamInt32, &P_loadDataFileStatus);
    createParam(P_loadDataFileNameString, asynParamOctet, &P_loadDataFileName);
    createParam(P_eventSpec2DTransModeString, asynParamInt32, &P_eventSpec2DTransMode);
    createParam(P_reloadLiveDataString, asynParamInt32, &P_reloadLiveData);

    NDDataType_t dataType = NDInt32; // data type for each frame
    int status = 0;
    for(int i=0; i<maxAddr; ++i)
    {
        //int maxSizeX = maxSizes[i][0];
        //int maxSizeY = maxSizes[i][1];
		status =  setStringParam (i, ADManufacturer, "CAENMCA");
		status |= setStringParam (i, ADModel, "CAENMCA");
		status |= setIntegerParam(i, ADMaxSizeX, 1);
		status |= setIntegerParam(i, ADMaxSizeY, 1);
		status |= setIntegerParam(i, ADMinX, 0);
		status |= setIntegerParam(i, ADMinY, 0);
		status |= setIntegerParam(i, ADBinX, 1);
		status |= setIntegerParam(i, ADBinY, 1);
		status |= setIntegerParam(i, ADReverseX, 0);
		status |= setIntegerParam(i, ADReverseY, 0);
		status |= setIntegerParam(i, ADSizeX, 1);
		status |= setIntegerParam(i, ADSizeY, 1);
		status |= setIntegerParam(i, NDArraySizeX, 1);
		status |= setIntegerParam(i, NDArraySizeY, 1);
		status |= setIntegerParam(i, NDArraySize, 1);
		status |= setIntegerParam(i, NDDataType, dataType);
		status |= setIntegerParam(i, ADImageMode, ADImageContinuous);
		status |= setIntegerParam(i, ADStatus, ADStatusIdle);
		status |= setIntegerParam(i, ADAcquire, 0);
		status |= setDoubleParam (i, ADAcquireTime, .001);
		status |= setDoubleParam (i, ADAcquirePeriod, .005);
		status |= setIntegerParam(i, ADNumImages, 100);
        status |= setIntegerParam(i, ADNumImagesCounter, 0);
        status |= setIntegerParam(i, P_eventSpec2DEnergyBinGroup, 64); // must be a valid value in Db
        status |= setIntegerParam(i, P_loadDataFile, 0);
        status |= setIntegerParam(i, P_reloadLiveData, 0);
        status |= setIntegerParam(i, P_loadDataFileStatus, 0);
        status |= setIntegerParam(i, P_eventSpec2DTransMode, 0);
    }

    if (status) {
        printf("%s: unable to set CAENMCA parameters\n", functionName);
        return;
    }    
    
	setStringParam(P_deviceName, deviceName);
	m_device_h = CAENMCA::OpenDevice(deviceName, NULL);
	CAENMCA::GetData(m_device_h, CAEN_MCA_DATA_BOARD_INFO, DATAMASK_BRDINFO_FAMCODE, &m_famcode);
	getBoardInfo();

    CAENMCA::getHandlesFromCollection(m_device_h, CAEN_MCA_HANDLE_CHANNEL, m_chan_h);
    CAENMCA::getHandlesFromCollection(m_device_h, CAEN_MCA_HANDLE_HVCHANNEL, m_hv_chan_h);
	
    getHVInfo(0);
    getHVInfo(1);

    getChannelInfo(0);
    getChannelInfo(1);
	
    std::cerr << "hv0 on " << isHVOn(m_hv_chan_h[0]) << std::endl;
    std::cerr << "hv1 on " << isHVOn(m_hv_chan_h[1]) << std::endl;
        
	std::cerr << isAcqRunning() << " " << isAcqRunning(m_chan_h[0]) << " " << isAcqRunning(m_chan_h[1]) << std::endl;
    
    uint32_t val0 = 0, val1 = 0;
    readRegister(0x10B8, val0);
    readRegister(0x11B8, val1);
    std::cerr << "OLD: 0x1nB8 register for setting timing was: " << val0 << " " << val1 << std::endl;
    writeRegisterMask(0x10B8, 2, 2);
    writeRegisterMask(0x11B8, 2, 2);
    readRegister(0x10B8, val0);
    readRegister(0x11B8, val1);
    std::cerr << "NEW: 0x1nB8 register for setting timing was: " << val0 << " " << val1 << std::endl;

	if (epicsThreadCreate("CAENMCADriverPoller",
		epicsThreadPriorityMedium,
		epicsThreadGetStackSize(epicsThreadStackMedium),
		(EPICSTHREADFUNC)pollerTaskC, this) == 0)
	{
		printf("%s:%s: epicsThreadCreate failure\n", driverName, functionName);
		return;
	}
}

void CAENMCADriver::getParameterInfo(CAEN_MCA_HANDLE handle, const char *name)
{
	CAEN_MCA_HANDLE parameter = CAENMCA::GetChildHandleByName(handle, CAEN_MCA_HANDLE_PARAMETER, name);
	getParameterInfo(parameter);
}
	
void CAENMCADriver::getParameterInfo(CAEN_MCA_HANDLE parameter)
{
	char parameter_name[PARAMINFO_NAME_MAXLEN] = { '\0' };
	char parameter_codename[PARAMINFO_NAME_MAXLEN] = { '\0' };
	uint32_t infobits;
	char uom_name[PARAMINFO_NAME_MAXLEN] = { '\0' };
	char uom_codename[PARAMINFO_NAME_MAXLEN] = { '\0' };
	int32_t uom_power;
	CAEN_MCA_ParameterType_t type;
	double min, max, incr;
	uint32_t nallowed_values;
	double allowed_values[PARAMINFO_LIST_MAXLEN] = { 0. };
	char *allowed_value_codenames[PARAMINFO_LIST_MAXLEN] = { NULL };
	char *allowed_value_names[PARAMINFO_LIST_MAXLEN] = { NULL };
	for (uint32_t i = 0; i < PARAMINFO_LIST_MAXLEN; i++) {
		allowed_value_codenames[i] = (char*)calloc(PARAMINFO_NAME_MAXLEN, sizeof(char));
		allowed_value_names[i] = (char*)calloc(PARAMINFO_NAME_MAXLEN, sizeof(char));
	}
	CAENMCA::GetData(
		parameter,
		CAEN_MCA_DATA_PARAMETER_INFO,
		DATAMASK_PARAMINFO_NAME |
		DATAMASK_PARAMINFO_CODENAME |
		DATAMASK_PARAMINFO_INFOMASK |
		DATAMASK_PARAMINFO_UOM_NAME |
		DATAMASK_PARAMINFO_UOM_CODENAME |
		DATAMASK_PARAMINFO_UOM_POWER |
		DATAMASK_PARAMINFO_TYPE |
		DATAMASK_PARAMINFO_MIN |
		DATAMASK_PARAMINFO_MAX |
		DATAMASK_PARAMINFO_INCR |
		DATAMASK_PARAMINFO_NALLOWED_VALUES |
		DATAMASK_PARAMINFO_ALLOWED_VALUES |
		DATAMASK_PARAMINFO_ALLOWED_VALUE_CODENAMES |
		DATAMASK_PARAMINFO_ALLOWED_VALUE_NAMES,
		parameter_name,
		parameter_codename,
		&infobits,
		uom_name,
		uom_codename,
		&uom_power,
		&type,
		&min,
		&max,
		&incr,
		&nallowed_values,
		allowed_values,
		allowed_value_codenames,
		allowed_value_names
	);
	fprintf(stdout, "Parameter: %s (%s)\n", parameter_codename, parameter_name);
	switch (type) {
	case CAEN_MCA_PARAMETER_TYPE_RANGE:
		fprintf(stdout, "\tMin: %f max: %f incr: %f\n", min, max, incr);
		break;
	case CAEN_MCA_PARAMETER_TYPE_LIST:
		for (uint32_t i = 0; i < nallowed_values; i++)
			fprintf(stdout, "\tAllowed value: %s [%s] (%f)\n", allowed_value_codenames[i], allowed_value_names[i], allowed_values[i]);
		break;
	default:
		fprintf(stderr, "\tUnknown parameter type.\n");
	}
	fprintf(stdout, "\tUnit of measurement: %s (power: %d)\n", uom_name, uom_power);
	for (uint32_t i = 0; i < PARAMINFO_LIST_MAXLEN; i++) {
		free(allowed_value_codenames[i]);
	}
}

// parameter of type range
double CAENMCADriver::getParameterValue(CAEN_MCA_HANDLE handle, const char *name)
{
	double value;
	CAEN_MCA_HANDLE parameter = CAENMCA::GetChildHandleByName(handle, CAEN_MCA_HANDLE_PARAMETER, name);
	CAENMCA::GetData(parameter, CAEN_MCA_DATA_PARAMETER_VALUE, DATAMASK_VALUE_NUMERIC, &value);
	return value;
}

// parameter of type list
std::string CAENMCADriver::getParameterValueList(CAEN_MCA_HANDLE handle, const char *name)
{
	char pvalue[PARAMINFO_NAME_MAXLEN];
	CAEN_MCA_HANDLE parameter = CAENMCA::GetChildHandleByName(handle, CAEN_MCA_HANDLE_PARAMETER, name);
    CAENMCA::GetData(parameter, CAEN_MCA_DATA_PARAMETER_VALUE, DATAMASK_VALUE_CODENAME, pvalue);
	return pvalue;
}

void CAENMCADriver::setParameterValue(CAEN_MCA_HANDLE handle, const char *name, double value)
{
	CAEN_MCA_HANDLE parameter = CAENMCA::GetChildHandleByName(handle, CAEN_MCA_HANDLE_PARAMETER, name);
	CAENMCA::SetData(parameter, CAEN_MCA_DATA_PARAMETER_VALUE, DATAMASK_VALUE_NUMERIC, value);
}

void CAENMCADriver::setParameterValueList(CAEN_MCA_HANDLE handle, const char *name, const std::string& value)
{
	CAEN_MCA_HANDLE parameter = CAENMCA::GetChildHandleByName(handle, CAEN_MCA_HANDLE_PARAMETER, name);
	CAENMCA::SetData(parameter, CAEN_MCA_DATA_PARAMETER_VALUE, DATAMASK_VALUE_NUMERIC, value.c_str());  // DATAMASK_VALUE_CODENAME ?
}

void CAENMCADriver::setHVState(CAEN_MCA_HANDLE hvchan, bool is_on)
{
	CAEN_MCA_CommandType_t cmd = is_on ? CAEN_MCA_CMD_HV_ON : CAEN_MCA_CMD_HV_OFF;
	CAENMCA::SendCommand(hvchan, cmd, DATAMASK_CMD_NONE, DATAMASK_CMD_NONE);
}

bool CAENMCADriver::isHVOn(CAEN_MCA_HANDLE hvchan)
{
	uint32_t is_on = 0;
	CAENMCA::SendCommand(hvchan, CAEN_MCA_CMD_HV_ONOFF, DATAMASK_CMD_NONE, DATAMASK_CMD_HVOUTPUT_STATUS, &is_on);
	return (is_on != 0 ? true : false);
}

bool CAENMCADriver::isAcqRunning()
{
	double value = getParameterValue(m_device_h, "PARAM_ACQRUNNING");
	return (value != 0.0 ? true : false);
}

bool CAENMCADriver::isAcqRunning(CAEN_MCA_HANDLE chan)
{
	double value = getParameterValue(chan, "PARAM_CH_ACQ_RUN");
	return (value != 0.0 ? true : false);
}

void CAENMCADriver::getHVInfo(uint32_t hv_chan_id)
{
    char hvrange_name[HVRANGEINFO_NAME_MAXLEN];
	double vset_min, vset_max, vset_incr, vmax_max, vmax, vmon, imon;
    double hvpol, hvstat, vset, iset, tmon, hv_active_range, rampup, rampdown;
    uint32_t nranges;
    int32_t polarity;
	CAEN_MCA_HANDLE hvchannel = m_hv_chan_h[hv_chan_id];

	CAENMCA::GetData(
        hvchannel,
        CAEN_MCA_DATA_HVCHANNEL_INFO,
        DATAMASK_HVCHANNELINFO_NRANGES |
        DATAMASK_HVCHANNELINFO_POLARITY,
        &nranges,
        &polarity);
        
	hv_active_range = getParameterValue(hvchannel, "PARAM_HVCH_ACTIVE_RANGE");

	CAEN_MCA_HANDLE hvrange = CAENMCA::GetChildHandle(hvchannel, CAEN_MCA_HANDLE_HVRANGE, (int)hv_active_range);
	CAENMCA::GetData(
		hvrange,
		CAEN_MCA_DATA_HVRANGE_INFO,
		DATAMASK_HVRANGEINFO_VSET_MIN |
		DATAMASK_HVRANGEINFO_VSET_MAX |
		DATAMASK_HVRANGEINFO_VSET_INCR |
		DATAMASK_HVRANGEINFO_VMAX_MAX |
        DATAMASK_HVRANGEINFO_NAME,
		&vset_min,
		&vset_max,
		&vset_incr,
		&vmax_max,
        hvrange_name);
	vset = getParameterValue(hvrange, "PARAM_HVRANGE_VSET");
	iset = getParameterValue(hvrange, "PARAM_HVRANGE_ISET");
	vmon = getParameterValue(hvrange, "PARAM_HVRANGE_VMON");
	tmon = getParameterValue(hvrange, "PARAM_HVRANGE_TMON");
	vmax = getParameterValue(hvrange, "PARAM_HVRANGE_VMAX");
	imon = getParameterValue(hvrange, "PARAM_HVRANGE_IMON");
	rampup = getParameterValue(hvrange, "PARAM_HVRANGE_RAMPUP");
	rampdown = getParameterValue(hvrange, "PARAM_HVRANGE_RAMPDOWN");
	hvpol = getParameterValue(hvchannel, "PARAM_HVCH_POLARITY");
	hvstat = getParameterValue(hvchannel, "PARAM_HVCH_STATUS");
	setDoubleParam(hv_chan_id, P_vmon, vmon);
	setDoubleParam(hv_chan_id, P_imon, imon);
	setDoubleParam(hv_chan_id, P_vset, vset);
	setDoubleParam(hv_chan_id, P_iset, iset);
	setDoubleParam(hv_chan_id, P_tmon, tmon);
	setDoubleParam(hv_chan_id, P_rampdown, rampdown);
	setDoubleParam(hv_chan_id, P_rampup, rampup);
    setStringParam(hv_chan_id, P_hvRangeName, hvrange_name); 
	setIntegerParam(hv_chan_id, P_hvPolarity, hvpol); // CAEN_MCA_POLARITY_TYPE_POSITIVE=0, CAEN_MCA_POLARITY_TYPE_NEGATIVE=1
	setIntegerParam(hv_chan_id, P_hvStatus, hvstat); 
    setIntegerParam(hv_chan_id, P_hvOn, (isHVOn(hvchannel) ? 1 : 0));
//	getParameterInfo(hvrange, "PARAM_HVRANGE_VMON");
//	getParameterInfo(hvchannel, "PARAM_HVCH_STATUS");
}


void CAENMCADriver::stopAcquisition(int addr, int value)
{
    if (value < 2) // is it a bo record sending 0 or 1, if so single channel and use asyn addr for channel
    {
        controlAcquisition(1 << addr, false);
    }
    else
    {
        controlAcquisition(value, false);
    }
}

void CAENMCADriver::startAcquisition(int addr, int value)
{
    if (value < 2) // is it a bo record sending 0 or 1, if so single channel and use asyn addr for channel
    {
        controlAcquisition(1 << addr, true);
    }
    else
    {
        controlAcquisition(value, true);
    }
}

void CAENMCADriver::clearEnergySpectrum(int channel_id)
{
    CAENMCA::SendCommand(getSpectrumHandle(channel_id, 0), CAEN_MCA_CMD_ENERGYSPECTRUM_CLEAR,
                         DATAMASK_CMD_NONE, DATAMASK_CMD_NONE);
}

void CAENMCADriver::controlAcquisition(int chan_mask, bool start)
{
    CAEN_MCA_CommandType_t cmdtype = (start ? CAEN_MCA_CMD_ACQ_START : CAEN_MCA_CMD_ACQ_STOP);

	if (m_famcode == CAEN_MCA_FAMILY_CODE_XXHEX)
	{
		if (chan_mask == 0x3) // both channels
		{
            if (start) {
                clearEnergySpectrum(0);
                clearEnergySpectrum(1);
            }
			CAENMCA::SendCommand(m_device_h, cmdtype, DATAMASK_CMD_NONE, DATAMASK_CMD_NONE);
            setADAcquire(0, (start ? 1 : 0));
            setADAcquire(1, (start ? 1 : 0));
		}
		else
		{
			for (int i = 0; i < 2; ++i)
			{
				if ((chan_mask & (1 << i)) != 0)
				{
                    if (start) {
                        clearEnergySpectrum(i);
                    }
					CAENMCA::SendCommand(m_chan_h[i], cmdtype, DATAMASK_CMD_NONE, DATAMASK_CMD_NONE);
                    setADAcquire(i, (start ? 1 : 0));
				}
			}
		}
	}
	else
	{
        if (start) {
            clearEnergySpectrum(0);
        }
		CAENMCA::SendCommand(m_device_h, cmdtype, DATAMASK_CMD_NONE, DATAMASK_CMD_NONE);
        setADAcquire(0, (start ? 1 : 0));
	}
}

void CAENMCADriver::readRegister(uint32_t address, uint32_t& value)
{
	CAENMCA::SendCommand(
		m_device_h,
		CAEN_MCA_CMD_REGISTER_READ,
		DATAMASK_CMD_REG_ADDR,
		DATAMASK_CMD_REG_DATA,
		address,
		&value);
}

void CAENMCADriver::writeRegister(uint32_t address, uint32_t value)
{
	CAENMCA::SendCommand(
		m_device_h,
		CAEN_MCA_CMD_REGISTER_WRITE,
		DATAMASK_CMD_REG_ADDR | DATAMASK_CMD_REG_DATA,
		DATAMASK_CMD_NONE,
		address,
		value);
}

void CAENMCADriver::writeRegisterMask(uint32_t address, uint32_t value, uint32_t mask)
{
	CAENMCA::SendCommand(
		m_device_h,
		CAEN_MCA_CMD_REGISTER_WRITE,
		DATAMASK_CMD_REG_ADDR | DATAMASK_CMD_REG_DATA | DATAMASK_CMD_REG_MASK,
		DATAMASK_CMD_NONE,
		address,
		value,
		mask);
}

void CAENMCADriver::getChannelInfo(int32_t channel_id)
{
	CAEN_MCA_HANDLE channel = m_chan_h[channel_id];
	uint32_t nEnergySpectra = 0;
	CAENMCA::GetData(
		channel,
		CAEN_MCA_DATA_CHANNEL_INFO,
		DATAMASK_CHANNELINFO_NENERGYSPECTRA,
		&nEnergySpectra
	);
    setIntegerParam(channel_id, P_acqRunningCh, (isAcqRunning(channel) ? 1 : 0));
    setIntegerParam(channel_id, P_chanEnabled, (getParameterValue(channel, "PARAM_CH_ENABLED") != 0.0 ? 1 : 0));
    setIntegerParam(channel_id, P_chanPolarity, (getParameterValue(channel, "PARAM_CH_POLARITY"))); // CAEN_MCA_POLARITY_TYPE_POSITIVE=0, CAEN_MCA_POLARITY_TYPE_NEGATIVE=1
    setIntegerParam(channel_id, P_numEnergySpec, nEnergySpectra);
	setIntegerParam(channel_id, P_acqInit, (getParameterValue(channel, "PARAM_CH_ACQ_INIT") != 0.0 ? 1 : 0));
	setIntegerParam(channel_id, P_acqStartMode, getParameterValue(channel, "PARAM_CH_STARTMODE"));
	setIntegerParam(channel_id, P_chanMemFull, getParameterValue(channel, "PARAM_CH_MEMORY_FULL"));
	setIntegerParam(channel_id, P_chanMemEmpty, getParameterValue(channel, "PARAM_CH_MEMORY_EMPTY"));
}


void CAENMCADriver::getBoardInfo()
{

	uint32_t channels, hvchannels, serialNum, pcbrev, nbits;
	char modelName[MODEL_NAME_MAXLEN] = { '\0' };

	CAENMCA::GetData(
		m_device_h,
		CAEN_MCA_DATA_BOARD_INFO,
		DATAMASK_BRDINFO_MODELNAME |
		DATAMASK_BRDINFO_NCHANNELS |
		DATAMASK_BRDINFO_SERIALNUM |
		DATAMASK_BRDINFO_NHVCHANNELS |
		DATAMASK_BRDINFO_PCBREV |
		DATAMASK_BRDINFO_ADC_BIT_COUNT |
		DATAMASK_BRDINFO_TSAMPLE_PS |
		DATAMASK_BRDINFO_ENERGY_BIT_COUNT,
		modelName,
		&channels,
		&serialNum,
		&hvchannels,
		&pcbrev,
		&nbits,
		&m_tsample,
		&m_nbitsEnergy
	);
 	
	fprintf(stdout, "Device name: %s\n", modelName);
	fprintf(stdout, "PCB revision: %d\n", pcbrev);
	fprintf(stdout, "Number of input channels: %d\n", channels);
	fprintf(stdout, "Number of ADC bits: %d\n", nbits);
	fprintf(stdout, "Number of Energy bits: %d\n", m_nbitsEnergy);
	fprintf(stdout, "Sample period (in picoseconds): %d\n", m_tsample);
	fprintf(stdout, "Number of HV channels: %d\n", channels);
	fprintf(stdout, "Serial number: %d\n", serialNum);
}

template <typename T>
void CAENMCADriver::setData(CAEN_MCA_HANDLE handle, CAEN_MCA_DataType_t dataType, uint64_t dataMask, T value)
{
	CAENMCA::SetData(handle, dataType, dataMask, value);
}

void CAENMCADriver::setListsData(CAEN_MCA_HANDLE channel, bool timetag, bool energy, bool extras) 
{
	uint32_t mask = 0;
	if (timetag)	mask |= LIST_FILE_DATAMASK_TIMETAG;
	if (energy)		mask |= LIST_FILE_DATAMASK_ENERGY;
	if (extras)		mask |= LIST_FILE_DATAMASK_FLAGS;
	CAENMCA::SetData(channel, CAEN_MCA_DATA_LIST_MODE, DATAMASK_LIST_FILE_DATAMASK, mask);
}

void CAENMCADriver::loadConfiguration(const char* name) 
{
	if (name == NULL)
	{
        // If no name is provided, the most recent configuration is loaded
        CAENMCA::SendCommand(m_device_h, CAEN_MCA_CMD_CONFIGURATION_LOAD, DATAMASK_CMD_NONE, DATAMASK_CMD_NONE);
	}
    else
	{
        CAENMCA::SendCommand(m_device_h, CAEN_MCA_CMD_CONFIGURATION_LOAD, DATAMASK_CMD_SAVE_NAME, DATAMASK_CMD_NONE, name);
	}
}

void CAENMCADriver::listConfigurations(std::vector<std::string>& configs)
{
    uint32_t offset = 0;
    uint32_t cnt_found = 0;
    char *savenames[CONFIGSAVE_LIST_MAXLEN];
	configs.resize(0);
    for (uint32_t i = 0; i < CONFIGSAVE_LIST_MAXLEN; i++) {
        savenames[i] = (char*)calloc(CONFIGSAVE_FULLPATH_MAXLEN, sizeof(*savenames[i]));
    }
    CAENMCA::SendCommand(
        m_device_h,
        CAEN_MCA_CMD_CONFIGURATION_LIST,
        DATAMASK_CMD_SAVE_LIST_OFFSET,
        DATAMASK_CMD_SAVE_LIST_COUNT |
        DATAMASK_CMD_SAVE_LIST_NAMES,
        offset,
        &cnt_found,
        savenames
    );
    for (uint32_t i = 0; i < cnt_found; i++) {
	    configs.push_back(savenames[i]);
	}
    for (uint32_t i = 0; i < CONFIGSAVE_LIST_MAXLEN; i++) {
        free(savenames[i]);
    }
}

void CAENMCADriver::getEnergySpectrum(int32_t channel_id, int32_t spectrum_id, std::vector<epicsInt32>& data)
{
	CAEN_MCA_HANDLE channel = m_chan_h[channel_id];
	CAEN_MCA_HANDLE spectrum = getSpectrumHandle(channel, spectrum_id);
	uint64_t realtime;
	uint64_t livetime;
	uint64_t deadtime;
	uint32_t overflows, underflows;
	uint64_t nentries;
	uint32_t nrois;
	uint32_t autosaveperiod;
	char filename[ENERGYSPECTRUM_FULLPATH_MAXLEN];

    data.resize(ENERGYSPECTRUM_MAXLEN);

	CAENMCA::GetData(
		spectrum,
		CAEN_MCA_DATA_ENERGYSPECTRUM,
		DATAMASK_ENERGY_SPECTRUM_ARRAY |
		DATAMASK_ENERGY_SPECTRUM_RTIME |
		DATAMASK_ENERGY_SPECTRUM_LTIME |
		DATAMASK_ENERGY_SPECTRUM_DTIME |
		DATAMASK_ENERGY_SPECTRUM_OVERFLOW |
		DATAMASK_ENERGY_SPECTRUM_UNDERFLOW |
		DATAMASK_ENERGY_SPECTRUM_NENTRIES |
		DATAMASK_ENERGY_SPECTRUM_NROIS |
		DATAMASK_ENERGY_SPECTRUM_FILENAME |
		DATAMASK_ENERGY_SPECTRUM_AUTOSAVE_PERIOD,
		&(data[0]),
		&realtime,
		&livetime,
		&deadtime,
		&overflows,
		&underflows,
		&nentries,
		&nrois,
		filename,
		&autosaveperiod
	);
	uint32_t nbins = getParameterValue(spectrum, "PARAM_ENERGY_SPECTRUM_NBINS");
	setIntegerParam(channel_id, P_energySpecCounts, nentries);
    setIntegerParam(channel_id, P_energySpecNBins, nbins);
    setStringParam(channel_id, P_energySpecFilename, filename);    
    setDoubleParam(channel_id, P_energySpecRealtime, realtime);
	setDoubleParam(channel_id, P_energySpeclivetime, livetime);
	setDoubleParam(channel_id, P_energySpecdeadtime, deadtime);
	setIntegerParam(channel_id, P_energySpecOverflows, overflows);
    setIntegerParam(channel_id, P_energySpecUnderflows, underflows);
    setDoubleParam(channel_id, P_energySpecAutosave, autosaveperiod / 1000.0);

	data.resize(nbins);
}
	
void CAENMCADriver::setListModeFilename(int32_t channel_id, const char* filename)
{ 
	CAENMCA::SetData(m_chan_h[channel_id], CAEN_MCA_DATA_LIST_MODE, DATAMASK_LIST_FILENAME, filename);
}

void CAENMCADriver::setListModeType(int32_t channel_id,  CAEN_MCA_ListSaveMode_t mode)
{ 
	CAENMCA::SetData(m_chan_h[channel_id], CAEN_MCA_DATA_LIST_MODE, DATAMASK_LIST_SAVEMODE, mode);
}

void CAENMCADriver::setListModeEnable(int32_t channel_id,  bool enable)
{ 
	CAENMCA::SetData(m_chan_h[channel_id], CAEN_MCA_DATA_LIST_MODE, DATAMASK_LIST_ENABLE, (uint32_t)(enable ? 1 : 0));
}

CAEN_MCA_HANDLE CAENMCADriver::getSpectrumHandle(int32_t channel_id, int32_t spectrum_id)
{
	return CAENMCA::GetChildHandle(m_chan_h[channel_id], CAEN_MCA_HANDLE_ENERGYSPECTRUM, spectrum_id);
}

CAEN_MCA_HANDLE CAENMCADriver::getSpectrumHandle(CAEN_MCA_HANDLE channel, int32_t spectrum_id)
{
	return CAENMCA::GetChildHandle(channel, CAEN_MCA_HANDLE_ENERGYSPECTRUM, spectrum_id);
}

void CAENMCADriver::setEnergySpectrumFilename(int32_t channel_id, int32_t spectrum_id, const char* filename)
{
	CAENMCA::SetData(getSpectrumHandle(channel_id, spectrum_id), CAEN_MCA_DATA_ENERGYSPECTRUM, DATAMASK_ENERGY_SPECTRUM_FILENAME, filename);
}

void CAENMCADriver::setEnergySpectrumAutosave(int32_t channel_id, int32_t spectrum_id, double period)
{
	CAENMCA::SetData(getSpectrumHandle(channel_id, spectrum_id), CAEN_MCA_DATA_ENERGYSPECTRUM, DATAMASK_ENERGY_SPECTRUM_AUTOSAVE_PERIOD, (uint32_t)(period * 1000.0 + 0.5));
}
 
void CAENMCADriver::setEnergySpectrumNumBins(int32_t channel_id, int32_t spectrum_id, int nbins)
{
	setParameterValue(getSpectrumHandle(channel_id, spectrum_id), "PARAM_ENERGY_SPECTRUM_NBINS", (double)nbins);
}

void CAENMCADriver::setEnergySpectrumParameter(CAEN_MCA_HANDLE channel, int32_t spectrum_id, const char* parname, double value)
{
	CAEN_MCA_HANDLE spectrum = CAENMCA::GetChildHandle(channel, CAEN_MCA_HANDLE_ENERGYSPECTRUM, spectrum_id);
	setParameterValue(spectrum, parname, value);
}

template <typename T>
void CAENMCADriver::energySpectrumSetProperty(CAEN_MCA_HANDLE channel, int32_t spectrum_id, int prop, T value)
{
	CAEN_MCA_HANDLE spectrum = CAENMCA::GetChildHandle(channel, CAEN_MCA_HANDLE_ENERGYSPECTRUM, spectrum_id);
	CAENMCA::SetData(spectrum, CAEN_MCA_DATA_ENERGYSPECTRUM, prop, value);
}

void CAENMCADriver::pollerTask()
{
    bool new_data;
	epicsThreadSleep(0.2); // to allow class constructror to complete
	while(true)
	{
	    lock();

        //std::cerr << "hv0 on " << isHVOn(m_hv_chan_h[0]) << std::endl;
        //std::cerr << "hv1 on " << isHVOn(m_hv_chan_h[1]) << std::endl;
	
	    //std::cerr << isAcqRunning() << " " << isAcqRunning(m_chan_h[0]) << " " << isAcqRunning(m_chan_h[1]) << std::endl;
	    for(int i=0;i<2; ++i)
		{
	        getEnergySpectrum(i, 0, m_energy_spec[i]);
		    doCallbacksInt32Array(m_energy_spec[i].data(), m_energy_spec[i].size(), P_energySpec, i);
            getHVInfo(i);
            getChannelInfo(i);
		    getLists(i);
            new_data = processListFile(i);
		    callParamCallbacks(i);
			updateAD(i, new_data);
		    doCallbacksFloat64Array(m_event_spec_x[i].data(), m_event_spec_x[i].size(), P_eventsSpecX, i);
		    doCallbacksFloat64Array(m_event_spec_y[i].data(), m_event_spec_y[i].size(), P_eventsSpecY, i);
		    doCallbacksInt32Array(m_energy_spec_event[i].data(), m_energy_spec_event[i].size(), P_energySpecEvent, i);
		    callParamCallbacks(i);
		}
        setIntegerParam(P_acqRunning, (isAcqRunning() ? 1 : 0));
		std::vector<std::string> configs_v;
        listConfigurations(configs_v);
        std::string configs;

        for(int i=0; i<configs_v.size(); ++i)
        {
            configs +=  configs_v[i];
            if (i != configs_v.size() - 1)
            {
                configs += ",";
            }
        }        
        setStringParam(P_availableConfigurations, configs.c_str());

		callParamCallbacks(0);
		unlock();
		epicsThreadSleep(3.0);
	}
}

asynStatus CAENMCADriver::readOctet(asynUser *pasynUser, char *value, size_t maxChars, size_t *nActual, int *eomReason)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *paramName = NULL;
	getParamName(function, &paramName);
	int addr = 0;
	getAddress(pasynUser, &addr);
    if (function < FIRST_CAEN_PARAM) {
        return ADDriver::readOctet(pasynUser, value, maxChars, nActual, eomReason);
    } else {
	    return asynPortDriver::readOctet(pasynUser, value, maxChars, nActual, eomReason);
    }
}

asynStatus CAENMCADriver::writeOctet(asynUser *pasynUser, const char *value, size_t maxChars, size_t *nActual)
{
    static const char* functionName = "writeOctet";
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *paramName = NULL;
	getParamName(function, &paramName);
	int addr = 0;
	getAddress(pasynUser, &addr);
	std::string value_s(value, maxChars); // in case not NULL terminated
	try
	{
	    if (function == P_listFile)
	    {		
            setListModeFilename(addr, value_s.c_str());
	    }
	    else if (function == P_configuration)
	    {
          loadConfiguration(value_s.c_str());
	    }
	    else if (function == P_energySpecFilename)
	    {
            setEnergySpectrumFilename(addr, 0, value_s.c_str()); 
	    }
		asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
			"%s:%s: function=%d, name=%s, value=%s\n",
			driverName, functionName, function, paramName, value_s.c_str());
        if (function < FIRST_CAEN_PARAM) {
            return ADDriver::writeOctet(pasynUser, value, maxChars, nActual);
        } else {
	        return asynPortDriver::writeOctet(pasynUser, value, maxChars, nActual);
        }
	}
	catch (const std::exception& ex)
	{
		epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
			"%s:%s: function=%d, name=%s, value=%s, error=%s",
			driverName, functionName, function, paramName, value_s.c_str(), ex.what());
		return asynError;
	}    
}

asynStatus CAENMCADriver::readInt32(asynUser *pasynUser, epicsInt32 *value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *paramName = NULL;
	getParamName(function, &paramName);
	int addr = 0;
	getAddress(pasynUser, &addr);
    if (function < FIRST_CAEN_PARAM) {
        return ADDriver::readInt32(pasynUser, value);
    } else {
	    return asynPortDriver::readInt32(pasynUser, value);
    }
}

asynStatus CAENMCADriver::readFloat64Array(asynUser *pasynUser, epicsFloat64 *value, size_t nElements, size_t *nIn)
{
	int function = pasynUser->reason;
	int addr = 0;
	getAddress(pasynUser, &addr);
	if (function < FIRST_CAEN_PARAM)
	{
		return ADDriver::readFloat64Array(pasynUser, value, nElements, nIn);
	}
    asynStatus stat = asynSuccess;
	callParamCallbacks();
	doCallbacksFloat64Array(value, *nIn, function, addr);
    return stat;
}

asynStatus CAENMCADriver::readInt32Array(asynUser *pasynUser, epicsInt32 *value, size_t nElements, size_t *nIn)
{
	int function = pasynUser->reason;
	int addr = 0;
	getAddress(pasynUser, &addr);
	if (function < FIRST_CAEN_PARAM)
	{
		return ADDriver::readInt32Array(pasynUser, value, nElements, nIn);
	}
    asynStatus stat = asynSuccess;
	callParamCallbacks();
	doCallbacksInt32Array(value, *nIn, function, addr);
    return stat;
}

asynStatus CAENMCADriver::readFloat64(asynUser *pasynUser, epicsFloat64 *value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *paramName = NULL;
	getParamName(function, &paramName);
	int addr = 0;
	getAddress(pasynUser, &addr);
    if (function < FIRST_CAEN_PARAM) {
        return ADDriver::readFloat64(pasynUser, value);
    } else {
	    return asynPortDriver::readFloat64(pasynUser, value);
    }
}

asynStatus CAENMCADriver::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *paramName = NULL;
	getParamName(function, &paramName);
	int addr = 0;
	getAddress(pasynUser, &addr);
    if (function < FIRST_CAEN_PARAM) {
        return ADDriver::writeFloat64(pasynUser, value);
    } else {
	    return asynPortDriver::writeFloat64(pasynUser, value);
    }
}

asynStatus CAENMCADriver::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
	static const char* functionName = "writeInt32";
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *paramName = NULL;
	getParamName(function, &paramName);
	int addr = 0;
	getAddress(pasynUser, &addr);
	try
	{
        if (function == ADAcquire)
        {            
            setADAcquire(addr, value);
            // fall through to next line to call base class
        }
		if (function == P_startAcquisition)
		{
			startAcquisition(addr, value);
		}
		else if (function == P_stopAcquisition)
		{
			stopAcquisition(addr, value);
		}
		else if (function == P_hvOn)
        {
            setHVState(m_hv_chan_h[addr], (value != 0 ? true : false));
        }
		else if (function == P_listEnabled)
        {
            setListModeEnable(addr, (value != 0 ? true : false));
        }
		else if (function == P_chanEnabled)
        {
            setParameterValue(m_chan_h[addr], "PARAM_CH_ENABLED", (value != 0 ? 1 : 0));
        }
		else if (function == P_listSaveMode)
        {
            setListModeType(addr, static_cast<CAEN_MCA_ListSaveMode_t>(value));
        }
		else if (function == P_listMaxNEvents)
        {
            CAENMCA::SetData(m_chan_h[addr], CAEN_MCA_DATA_LIST_MODE, DATAMASK_LIST_MAXNEVTS, value);
        }
		else if (function == P_energySpecNBins)
        {
            setEnergySpectrumNumBins(addr, 0, value);
        }
		else if (function == P_energySpecClear)
        {
            clearEnergySpectrum(addr);
        }
		else if (function == P_restart)
        {
            CAENMCA::SendCommand(m_device_h, CAEN_MCA_CMD_RESTART , DATAMASK_CMD_NONE, DATAMASK_CMD_NONE);
        }
		asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
			"%s:%s: function=%d, name=%s, value=%d\n",
			driverName, functionName, function, paramName, value);
        if (function < FIRST_CAEN_PARAM) {
            return ADDriver::writeInt32(pasynUser, value);
		} else {
            return asynPortDriver::writeInt32(pasynUser, value);
        }
	}
	catch (const std::exception& ex)
	{
		epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
			"%s:%s: status=%d, function=%d, name=%s, value=%d, error=%s",
			driverName, functionName, status, function, paramName, value, ex.what());
		return asynError;
	}
}

void CAENMCADriver::getLists(uint32_t channel_id) 
{
	uint32_t maxnevts;
	uint32_t nevts = 0;
	uint32_t getfake;
	CAEN_MCA_ListSaveMode_t savemode;
	uint32_t enabled;
	uint32_t datamask;
	char filename[LISTS_FULLPATH_MAXLEN];
	std::vector<uint64_t> datatimetag;
	std::vector<uint32_t> dataenergy;
	std::vector<uint16_t> dataflags;
    CAEN_MCA_HANDLE channel = m_chan_h[channel_id];
	CAENMCA::GetData(
		channel,
		CAEN_MCA_DATA_LIST_MODE,
		DATAMASK_LIST_ENABLE |
		DATAMASK_LIST_SAVEMODE |
		DATAMASK_LIST_FILENAME |
		DATAMASK_LIST_FILE_DATAMASK |
		DATAMASK_LIST_GETFAKEEVTS |
        DATAMASK_LIST_MAXNEVTS |
		DATAMASK_LIST_NEVTS,
		&enabled,
		&savemode,
		filename,
		&datamask,
		&getfake,
        &maxnevts,
        &nevts
	);
    if (savemode == CAEN_MCA_SAVEMODE_MEMORY) {  
        datatimetag.resize(LISTS_DATA_MAXLEN);
        dataenergy.resize(LISTS_DATA_MAXLEN);
        dataflags.resize(LISTS_DATA_MAXLEN);
	    CAENMCA::GetData(
		    channel,
		    CAEN_MCA_DATA_LIST_MODE,
            DATAMASK_LIST_MAXNEVTS |   
		    DATAMASK_LIST_NEVTS |
// these next values cause a reset, ony use is savemode is memory
            DATAMASK_LIST_DATA_TIMETAG |
            DATAMASK_LIST_DATA_ENERGY |
            DATAMASK_LIST_DATA_FLAGS_DATAMASK,
            &maxnevts,
            &nevts,
            &(datatimetag[0]),
            &(dataenergy[0]),
            &(dataflags[0])
        );
        datatimetag.resize(nevts);
        dataenergy.resize(nevts);
        dataflags.resize(nevts);
    }
	
	setIntegerParam(channel_id, P_nEvents, nevts);
	setIntegerParam(channel_id, P_listMaxNEvents, maxnevts);
	setStringParam(channel_id, P_listFile, filename);
	setIntegerParam(channel_id, P_listEnabled, enabled);
	setIntegerParam(channel_id, P_listSaveMode, savemode);
    // set a parameter to datamask	
}

static std::string describeFlags(unsigned flags)
{
    std::string s;
    if (flags & 0x1)
    {
        s += "First event after a dead time occurrence, ";
        flags &= ~0x1;
    }
    if (flags & 0x2)
    {
        s += "Time tag rollover, ";
        flags &= ~0x2;
    }
    if (flags & 0x4)
    {
        s += "Time tag reset, ";
        flags &= ~0x4;
    }
    if (flags & 0x8)
    {
        s += "Fake event, ";
        flags &= ~0x8;
    }
    if (flags & 0x80)
    {
        s += "Event energy saturated, ";
        flags &= ~0x80;
    }
    if (flags & 0x400)
    {
        s += "Input dynamics saturated event, ";
        flags &= ~0x400;
    }
    if (flags & 0x8000)
    {
        s += "Pile up event, ";
        flags &= ~0x8000;
    }
    if (flags & 0x10000)
    {
        s += "Deadtime calc event, ";
        flags &= ~0x10000;
    }
    if (flags & 0x20000)
    {
        s += "Event energy is outside the SCA interval, ";
        flags &= ~0x20000;
    }
    if (flags & 0x40000)
    {
        s += "Event occurred during saturation inhibit, ";
        flags &= ~0x40000;
    }
    if (flags != 0)
    {
        s += "Unknown flag";
    }
    return s;
}

bool CAENMCADriver::processListFile(int channel_id)
{
    std::string filename, deviceName, ethPrefix = "eth://";
    int enabled = 0, save_mode, load_data_file = 0, reload_live_data = 0;
	getStringParam(channel_id, P_listFile, filename);
	getIntegerParam(channel_id, P_listEnabled, &enabled);
	getIntegerParam(channel_id, P_listSaveMode, &save_mode);
    getIntegerParam(channel_id, P_loadDataFile, &load_data_file);
    getIntegerParam(channel_id, P_reloadLiveData, &reload_live_data);
    double eventSpec2d_TMin = 0.0, eventSpec2d_TMax = 0.0;
    int eventSpec2d_nTBins = 0, eventSpec2d_engBinGroup = 1;
	getStringParam(P_deviceName, deviceName);
	getDoubleParam(channel_id, P_eventSpec2DTimeMax, &eventSpec2d_TMax);
	getDoubleParam(channel_id, P_eventSpec2DTimeMin, &eventSpec2d_TMin);
	getIntegerParam(channel_id, P_eventSpec2DNTimeBins, &eventSpec2d_nTBins);
	getIntegerParam(channel_id, P_eventSpec2DEnergyBinGroup, &eventSpec2d_engBinGroup);
    uint64_t trigger_time = 0, frame_length = 0, max_event_time = 0;
    int16_t energy;
    uint32_t extras;
    const size_t EVENT_SIZE = 14;
    struct stat stat_struct;
    bool new_data = false;
    std::string filename_ascii = filename;
    for(int i=0; i<filename_ascii.size(); ++i) {
        if (filename_ascii[i] == '/' || filename_ascii[i] == '\\' || filename_ascii[i] == '.') {
            filename_ascii[i] = '_';
        }            
    }
    filename_ascii = std::string("c:/Data/") + filename_ascii + ".txt";
    std::string prefix = "\\\\127.0.0.1\\storage\\";
    if (!deviceName.compare(0, ethPrefix.size(), ethPrefix)) {
        prefix = std::string("\\\\") + deviceName.substr(ethPrefix.size()) + "\\storage\\";
    }
    FILE*& f = std::get<0>(m_file_fd[channel_id]);
    FILE*& f_ascii = std::get<1>(m_file_fd[channel_id]);
    if ( (sizeof(trigger_time) + sizeof(energy) + sizeof(extras)) != EVENT_SIZE )
    {
        std::cerr << "size error" << std::endl;
        return new_data;
    }
    if (reload_live_data != 0) {
        setIntegerParam(channel_id, P_reloadLiveData, 0);
        std::cerr << "ReLoading live data..." << std::endl;
    }        
    if (load_data_file != 0) {
        setIntegerParam(channel_id, P_loadDataFile, 0);
        setADAcquire(channel_id, 1);
    }        
    else if (!enabled || save_mode != CAEN_MCA_SAVEMODE_FILE_BINARY)
    {
        if (f != NULL)
        {
            fclose(f);
            f = NULL;
        }
        if (f_ascii != NULL)
        {
            fclose(f_ascii);
            f_ascii = NULL;
        }
        return new_data;
    }
    int64_t frame = 0, last_pos, current_pos = 0, new_bytes, nevents;
    m_energy_spec_event[channel_id].resize(MAX_ENERGY_BINS);
	if (f != NULL)
	{
		current_pos = _ftelli64(f);
	}
    int nevents_real_ev = 0, ev_nbins = 0, nevents_real_es = 0;
    int ntimerollover = 0, ntimereset = 0, neventenergysat = 0;
    int nfakeevent = 0, nimpdynamsatevent = 0, npileupevent = 0;
    int neventenergyoutsca = 0, neventdursatinhibit = 0;
    int nevent2dnotbinned = 0, neventnotbinned = 0, neventenergydiscard = 0;
    double ev_tmin = 0.0, ev_tmax = 0.0;
    FILE* save_f = NULL;
    int64_t save_event_file_last_pos = 0;
    getDoubleParam(channel_id, P_eventsSpecTMin, &ev_tmin);
    getDoubleParam(channel_id, P_eventsSpecTMax, &ev_tmax);
    getIntegerParam(channel_id, P_eventsSpecNBins, &ev_nbins);
    double ev_binw = 0.0;
    if (ev_tmax > ev_tmin && ev_nbins > 0) {
        ev_binw = (ev_tmax - ev_tmin) / ev_nbins;
    }
    setDoubleParam(channel_id, P_eventsSpecTBinWidth, ev_binw);
    m_event_spec_x[channel_id].resize(ev_nbins);
    m_event_spec_y[channel_id].resize(ev_nbins);
    int eventSpec2d_nx = MAX_ENERGY_BINS / eventSpec2d_engBinGroup;
    int eventSpec2d_ny = eventSpec2d_nTBins;
	m_event_spec_2d[channel_id].resize(eventSpec2d_nx * eventSpec2d_ny);
    double ev2d_tbinw = 0.0;
    if (eventSpec2d_TMax > eventSpec2d_TMin && eventSpec2d_nTBins > 0) {
        ev2d_tbinw = (eventSpec2d_TMax - eventSpec2d_TMin) / eventSpec2d_nTBins;
    }
    setDoubleParam(channel_id, P_eventSpec2DTBinWidth, ev2d_tbinw);
    if (f == NULL || load_data_file || reload_live_data || filename != m_old_list_filename[channel_id] ||
        current_pos == -1 || current_pos != m_event_file_last_pos[channel_id])
    {
        new_data = true;
        setIntegerParam(channel_id, P_nEventsProcessed, 0);
        setIntegerParam(channel_id, P_energySpecEventNEvents, 0);
        setIntegerParam(channel_id, P_eventsSpecNEvents, 0);
        setIntegerParam(channel_id, P_eventsSpecNTriggers, 0);
        setIntegerParam(channel_id, P_eventsSpecNTimeTagRollover, 0);
        setIntegerParam(channel_id, P_eventsSpecNTimeTagReset, 0);
        setIntegerParam(channel_id, P_eventsSpecNEventEnergySat, 0);
        setIntegerParam(channel_id, P_nFakeEvents, 0);
        setIntegerParam(channel_id, P_nPileupEvent, 0);
        setIntegerParam(channel_id, P_nEventEnergyOutSCA, 0);
        setIntegerParam(channel_id, P_nEventDurSatInhibit, 0);
        setIntegerParam(channel_id, P_nImpDynamSatEvent, 0);
        setIntegerParam(channel_id, P_nEventEnergyDiscard, 0);
        setIntegerParam(channel_id, P_nEventNotBinned, 0);
        std::fill(m_event_spec_y[channel_id].begin(), m_event_spec_y[channel_id].end(), 0.0);
        std::fill(m_energy_spec_event[channel_id].begin(), m_energy_spec_event[channel_id].end(), 0);
        std::fill(m_event_spec_2d[channel_id].begin(), m_event_spec_2d[channel_id].end(), 0);

        std::string p_filename;
        if (load_data_file) {
            getStringParam(channel_id, P_loadDataFileName, filename);
            p_filename = filename;
            std::cerr << "Loading data file \"" << p_filename << "\" ..." << std::endl;
            setIntegerParam(channel_id, P_loadDataFileStatus, 1);
            callParamCallbacks(channel_id);
            save_f = f;
            save_event_file_last_pos = m_event_file_last_pos[channel_id];
        } else {
            p_filename = prefix + filename;
        }
        std::replace(p_filename.begin(), p_filename.end(), '/', '\\'); 
        
        if (!load_data_file)
        {
            if (f != NULL) {
                fclose(f);
                f = NULL;
            }
            if (f_ascii != NULL) {
                fclose(f_ascii);
                f_ascii = NULL;
            }
            //std::cerr << "Opening " << filename_ascii << std::endl;
            //f_ascii = _fsopen(filename_ascii.c_str(), "wb", _SH_DENYWR);
            if (f_ascii != NULL) {
                fprintf(f_ascii, "TIMETAG\t\tENERGY\tFLAGS\t\n");
            }
        }
        if (stat(p_filename.c_str(), &stat_struct) != 0 || stat_struct.st_size == 0)
        {
            return new_data;
        }
        if ( (f = _fsopen(p_filename.c_str(), "rb", _SH_DENYNO)) == NULL )
        {
            return new_data;
        }
        if (!load_data_file) {
            m_old_list_filename[channel_id] = filename;
        }
        m_max_event_time[channel_id] = 0;
        m_event_file_last_pos[channel_id] = 0;
        current_pos = 0;
    }
    if (_fseeki64(f, 0, SEEK_END) != 0)
    {
        std::cerr << "fseek forward error" << std::endl;
        return new_data;
    }   
    if ( (current_pos = _ftelli64(f)) == -1)
    {
        std::cerr << "ftell curr error" << std::endl;
        return new_data;
    }
    if (_fseeki64(f, m_event_file_last_pos[channel_id], SEEK_SET) != 0)
    {
        std::cerr << "fseek back error" << std::endl;
        return new_data;
    }   
    setDoubleParam(channel_id, 	P_listFileSize, (double)current_pos / (1024.0 * 1024.0)); // convert to MBytes
    new_bytes = current_pos - m_event_file_last_pos[channel_id];
	if (new_bytes < 0)
	{
		fclose(f);
		f = NULL;
		return new_data;
	}
    nevents = new_bytes / EVENT_SIZE;
    if (nevents == 0)
    {
        return new_data;
    }
    new_data = true;
    for(int i=0; i<ev_nbins; ++i)
    {
        m_event_spec_x[channel_id][i] = ev_tmin + i * ev_binw;
    }
    double es_tmin = 0.0, es_tmax = 0.0;
    getDoubleParam(channel_id, P_energySpecEventTMin, &es_tmin);
    getDoubleParam(channel_id, P_energySpecEventTMax, &es_tmax);
    bool force_trigger, fake_trigger = false;
    for(int i=0; i<nevents; ++i)
    {
        if (fread(&trigger_time, sizeof(trigger_time), 1, f) != 1)
        {
            std::cerr << "fread time error" << std::endl;
            return new_data;
        }
        if (fread(&energy, sizeof(energy), 1, f) != 1)
        {
            std::cerr << "fread energy error" << std::endl;
            return new_data;
        }
        if (fread(&extras, sizeof(extras), 1, f) != 1)
        {
            std::cerr << "fread extras error" << std::endl;
            return new_data;
        }
        if (f_ascii != NULL) {
            fprintf(f_ascii, "%llu\t%d\t0x%08x\t\n", trigger_time, energy, extras);
        }
        trigger_time /= 1000;  // convert from ps to ns
        force_trigger = false;
        if (fake_trigger && ((trigger_time - m_frame_time[channel_id]) > 20000000))
        {
            force_trigger = true;
        }
        if (force_trigger || (extras == 0x8 && energy == 0))
        {
            ++frame;
            if (trigger_time > m_frame_time[channel_id]) {
                frame_length = trigger_time - m_frame_time[channel_id];
            }
            m_frame_time[channel_id] = trigger_time;
        }
        if (extras & 0x2) {
            ++ntimerollover;
        }
        if (extras & 0x4) {
            ++ntimereset;
        }
        if (extras & 0x8) {
            ++nfakeevent;
        }
        if (extras & 0x80) {
            ++neventenergysat;
        }
        if (extras & 0x400) {
            ++nimpdynamsatevent;
        }
        if (extras & 0x8000) {
            ++npileupevent;
        }
        if (extras & 0x20000) {
            ++neventenergyoutsca;
        }
        if (extras & 0x40000) {
            ++neventdursatinhibit;
        }
        if ( energy > 0 && (!(extras & 0x8)) )
        {
            uint64_t tdiff = trigger_time - m_frame_time[channel_id];
            if (tdiff > max_event_time) {
                max_event_time = tdiff;
            }
            if (energy != 32767) {
                int n = (ev_binw != 0.0) ? ((tdiff - ev_tmin) / ev_binw) : -1;
                if (n >= 0 && n < ev_nbins)
                {
                    m_event_spec_y[channel_id][n] += 1.0;
                    ++nevents_real_ev;
                }
                else
                {
                    ++neventnotbinned;
                }
                n = (ev2d_tbinw != 0.0) ? ((tdiff - eventSpec2d_TMin) / ev2d_tbinw) : -1;
                if (n >= 0 && n < eventSpec2d_nTBins)
                {
					m_event_spec_2d[channel_id][n * eventSpec2d_nx + energy / eventSpec2d_engBinGroup] += 1;
                }
                else
                {
                    ++nevent2dnotbinned;
                }
                if ((es_tmin >= es_tmax) || (tdiff >= es_tmin && tdiff <= es_tmax))
                {
                    ++nevents_real_es;
                    ++(m_energy_spec_event[channel_id][energy]);
                }
            } else {
                ++neventenergydiscard;
            }
        }
        //std::cout << frame << ": " << trigger_time << "  " << trigger_time - m_frame_time[channel_id] << "  " << energy << "  (" << describeFlags(extras) << ")" << std::endl;
    }
    // checking if max_event_time > m_max_event_time[channel_id] may not always be sensible
    m_max_event_time[channel_id] = max_event_time;
    m_event_file_last_pos[channel_id] = _ftelli64(f);
    incrIntParam(channel_id, P_nEventsProcessed, nevents);
    incrIntParam(channel_id, P_eventsSpecNEvents, nevents_real_ev);
    incrIntParam(channel_id, P_energySpecEventNEvents, nevents_real_es);
    incrIntParam(channel_id, P_eventsSpecNTriggers, frame);
    incrIntParam(channel_id, P_eventsSpecNTimeTagRollover, ntimerollover);
    incrIntParam(channel_id, P_eventsSpecNTimeTagReset, ntimereset);
    incrIntParam(channel_id, P_eventsSpecNEventEnergySat, neventenergysat);
    incrIntParam(channel_id, P_nFakeEvents, nfakeevent);
    incrIntParam(channel_id, P_nPileupEvent, npileupevent);
    incrIntParam(channel_id, P_nEventEnergyOutSCA, neventenergyoutsca);
    incrIntParam(channel_id, P_nEventDurSatInhibit, neventdursatinhibit);
    incrIntParam(channel_id, P_nImpDynamSatEvent, nimpdynamsatevent);
    incrIntParam(channel_id, P_nEventEnergyDiscard, neventenergydiscard);
    incrIntParam(channel_id, P_nEventNotBinned, neventnotbinned);
    if (frame_length > 0) {
        setDoubleParam(channel_id, P_eventsSpecTriggerRate, 1.0e9 / (double)frame_length); // frame length units is nano seconds
    } else {
        setDoubleParam(channel_id, P_eventsSpecTriggerRate, 0.0);
    }
    setDoubleParam(channel_id, P_eventsSpecMaxEventTime, m_max_event_time[channel_id]);
    if (load_data_file) {
        std::cerr << "Data file loaded" << std::endl;
        setIntegerParam(channel_id, P_loadDataFileStatus, 0);
//        setADAcquire(channel_id, 0);
        m_event_file_last_pos[channel_id] = save_event_file_last_pos;
        f = save_f;
    }
    if (reload_live_data) {
        std::cerr << "ReLoading live data complete" << std::endl;
    }        
    if (f_ascii != NULL) {
        fflush(f_ascii);
    }
    return new_data;
}

void CAENMCADriver::incrIntParam(int channel_id, int param, int incr)
{
    int old_val = 0;
    if (getIntegerParam(channel_id, param, &old_val) == asynSuccess) {
        setIntegerParam(channel_id, param, old_val + incr);
    }
}

void CAENMCADriver::updateAD(int addr, bool new_data)
{
    static const char* functionName = "updateAD";
	int acquiring;
    int status = asynSuccess;
    int imageCounter;
    int numImages, numImagesCounter;
    int imageMode;
    int arrayCallbacks;
    NDArray *pImage;
    double acquireTime, acquirePeriod, delay, updateTime;
    epicsTimeStamp startTime, endTime;
    double elapsedTime;
    static std::vector<int> old_acquiring(maxAddr, 0);
	static std::vector<epicsTimeStamp> last_update(maxAddr, {0,0});
    int eventSpec2d_nTBins = 0, eventSpec2d_engBinGroup = 1;
    getIntegerParam(addr, P_eventSpec2DEnergyBinGroup, &eventSpec2d_engBinGroup);
    getIntegerParam(addr, P_eventSpec2DNTimeBins, &eventSpec2d_nTBins);
    int eventSpec2d_nx = MAX_ENERGY_BINS / eventSpec2d_engBinGroup;
    int eventSpec2d_ny = eventSpec2d_nTBins;
			try 
			{
				acquiring = 0;
				getIntegerParam(addr, ADAcquire, &acquiring);
				getDoubleParam(addr, ADAcquirePeriod, &acquirePeriod);
				
				if (acquiring == 0)
				{
					old_acquiring[addr] = acquiring;
				}
				else if (old_acquiring[addr] == 0 && acquiring == 1)
				{
					setIntegerParam(addr, ADNumImagesCounter, 0);
					old_acquiring[addr] = acquiring;
				}
			    if (!new_data) {
                    return;
                }
				setIntegerParam(addr, ADStatus, ADStatusAcquire); 
				epicsTimeGetCurrent(&startTime);
				getIntegerParam(addr, ADImageMode, &imageMode);

				/* Get the exposure parameters */
				getDoubleParam(addr, ADAcquireTime, &acquireTime);  // not really used

				setShutter(addr, ADShutterOpen);
				callParamCallbacks(addr, addr);
				
				status = computeImage(addr, m_event_spec_2d[addr], eventSpec2d_nx, eventSpec2d_ny);

	//            if (status) continue;

				// could sleep to make up to acquireTime
			
				/* Close the shutter */
				setShutter(addr, ADShutterClosed);
			
				setIntegerParam(addr, ADStatus, ADStatusReadout);
				/* Call the callbacks to update any changes */
				callParamCallbacks(addr, addr);

				pImage = this->pArrays[addr];
				if (pImage == NULL)
				{
					return;
				}

				/* Get the current parameters */
				getIntegerParam(addr, NDArrayCounter, &imageCounter);
				getIntegerParam(addr, ADNumImages, &numImages);
				getIntegerParam(addr, ADNumImagesCounter, &numImagesCounter);
				getIntegerParam(addr, NDArrayCallbacks, &arrayCallbacks);
				++imageCounter;
				++numImagesCounter;
				setIntegerParam(addr, NDArrayCounter, imageCounter);
				setIntegerParam(addr, ADNumImagesCounter, numImagesCounter);

				/* Put the frame number and time stamp into the buffer */
				pImage->uniqueId = imageCounter;
				pImage->timeStamp = startTime.secPastEpoch + startTime.nsec / 1.e9;
				updateTimeStamp(&pImage->epicsTS);

				/* Get any attributes that have been defined for this driver */
				this->getAttributes(pImage->pAttributeList);

				if (arrayCallbacks) {
				  /* Call the NDArray callback */
				  /* Must release the lock here, or we can get into a deadlock, because we can
				   * block on the plugin lock, and the plugin can be calling us */
				  unlock();
				  asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
						"%s:%s: calling imageData callback addr %d\n", driverName, functionName, addr);
				  doCallbacksGenericPointer(pImage, NDArrayData, addr);
                  lock();
				}
				epicsTimeGetCurrent(&endTime);
				elapsedTime = epicsTimeDiffInSeconds(&endTime, &startTime);
				updateTime = epicsTimeDiffInSeconds(&endTime, &(last_update[addr]));
				last_update[addr] = endTime;
				/* Call the callbacks to update any changes */
				callParamCallbacks(addr, addr);
				/* sleep for the acquire period minus elapsed time. */
				delay = acquirePeriod - elapsedTime;
				asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
						"%s:%s: delay=%f\n",
						driverName, functionName, delay);
				if (delay >= 0.0) {
					/* We set the status to waiting to indicate we are in the period delay */
					setIntegerParam(addr, ADStatus, ADStatusWaiting);
					callParamCallbacks(addr, addr);
					{
						unlock();
						epicsThreadSleep(delay);
                        lock();
					}
					setIntegerParam(addr, ADStatus, ADStatusIdle);
					callParamCallbacks(addr, addr);  
				}
			}
			catch(...)
			{
				std::cerr << "Exception in pollerThread4" << std::endl;
			}
}

/** Computes the new image data */
template <typename epicsType> 
int CAENMCADriver::computeImage(int addr, const std::vector<epicsType>& data, int nx, int ny)
{
    int status = asynSuccess;
    NDDataType_t dataType;
    int itemp;
    int binX, binY, minX, minY, sizeX, sizeY, reverseX, reverseY;
    int xDim=0, yDim=1, colorDim=-1;
    int maxSizeX, maxSizeY;
    int colorMode;
    int ndims=0;
    NDDimension_t dimsOut[3];
    size_t dims[3];
    NDArrayInfo_t arrayInfo;
    NDArray *pImage;
    const char* functionName = "computeImage";

    /* NOTE: The caller of this function must have taken the mutex */

    status |= getIntegerParam(addr, ADBinX,         &binX);
    status |= getIntegerParam(addr, ADBinY,         &binY);
    status |= getIntegerParam(addr, ADMinX,         &minX);
    status |= getIntegerParam(addr, ADMinY,         &minY);
    status |= getIntegerParam(addr, ADSizeX,        &sizeX);
    status |= getIntegerParam(addr, ADSizeY,        &sizeY);
    status |= getIntegerParam(addr, ADReverseX,     &reverseX);
    status |= getIntegerParam(addr, ADReverseY,     &reverseY);
    status |= getIntegerParam(addr, ADMaxSizeX,     &maxSizeX);
    status |= getIntegerParam(addr, ADMaxSizeY,     &maxSizeY);
    status |= getIntegerParam(addr, NDColorMode,    &colorMode);
    status |= getIntegerParam(addr, NDDataType,     &itemp); 
	dataType = (NDDataType_t)itemp;
	if (status)
	{
		asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
			"%s:%s: error getting parameters\n",
			driverName, functionName);
		return (status);
	}
    if (maxSizeX != nx)
    {
        maxSizeX = nx;
        status |= setIntegerParam(addr, ADMaxSizeX, maxSizeX);
    }
    if (sizeX != nx)
    {
        sizeX = nx;
        status |= setIntegerParam(addr, ADSizeX, sizeX);
    }
    if (maxSizeY != ny)
    {
        maxSizeY = ny;
        status |= setIntegerParam(addr, ADMaxSizeY, maxSizeY);
    }
    if (sizeY != ny)
    {
        sizeY = ny;
        status |= setIntegerParam(addr, ADSizeY, sizeY);
    }

    /* Make sure parameters are consistent, fix them if they are not */
    if (binX < 1) {
        binX = 1;
        status |= setIntegerParam(addr, ADBinX, binX);
    }
    if (binY < 1) {
        binY = 1;
        status |= setIntegerParam(addr, ADBinY, binY);
    }
    if (minX < 0) {
        minX = 0;
        status |= setIntegerParam(addr, ADMinX, minX);
    }
    if (minY < 0) {
        minY = 0;
        status |= setIntegerParam(addr, ADMinY, minY);
    }
    if (minX > maxSizeX-1) {
        minX = maxSizeX-1;
        status |= setIntegerParam(addr, ADMinX, minX);
    }
    if (minY > maxSizeY-1) {
        minY = maxSizeY-1;
        status |= setIntegerParam(addr, ADMinY, minY);
    }
    if (minX+sizeX > maxSizeX) {
        sizeX = maxSizeX-minX;
        status |= setIntegerParam(addr, ADSizeX, sizeX);
    }
    if (minY+sizeY > maxSizeY) {
        sizeY = maxSizeY-minY;
        status |= setIntegerParam(addr, ADSizeY, sizeY);
    }
    
	if (status)
	{
		asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
			"%s:%s: error setting parameters\n",
			driverName, functionName);
		return (status);
	}

    if (sizeX == 0 || sizeY == 0)
    {
        return asynSuccess;
    }

    switch (colorMode) {
        case NDColorModeMono:
            ndims = 2;
            xDim = 0;
            yDim = 1;
            break;
        case NDColorModeRGB1:
            ndims = 3;
            colorDim = 0;
            xDim     = 1;
            yDim     = 2;
            break;
        case NDColorModeRGB2:
            ndims = 3;
            colorDim = 1;
            xDim     = 0;
            yDim     = 2;
            break;
        case NDColorModeRGB3:
            ndims = 3;
            colorDim = 2;
            xDim     = 0;
            yDim     = 1;
            break;
    }

// we could be more efficient
//    if (resetImage) {
    /* Free the previous raw buffer */
        if (m_pRaw) m_pRaw->release();
        /* Allocate the raw buffer we use to compute images. */
        dims[xDim] = maxSizeX;
        dims[yDim] = maxSizeY;
        if (ndims > 2) dims[colorDim] = 3;
        m_pRaw = this->pNDArrayPool->alloc(ndims, dims, dataType, 0, NULL);

        if (!m_pRaw) {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                      "%s:%s: error allocating raw buffer\n",
                      driverName, functionName);
            return(status);
        }
//    }

    switch (dataType) {
        case NDInt8:
            status |= computeArray<epicsInt8>(addr, data, maxSizeX, maxSizeY);
            break;
        case NDUInt8:
            status |= computeArray<epicsUInt8>(addr, data, maxSizeX, maxSizeY);
            break;
        case NDInt16:
            status |= computeArray<epicsInt16>(addr, data, maxSizeX, maxSizeY);
            break;
        case NDUInt16:
            status |= computeArray<epicsUInt16>(addr, data, maxSizeX, maxSizeY);
            break;
        case NDInt32:
            status |= computeArray<epicsInt32>(addr, data, maxSizeX, maxSizeY);
            break;
        case NDUInt32:
            status |= computeArray<epicsUInt32>(addr, data, maxSizeX, maxSizeY);
            break;
        case NDInt64:
            status |= computeArray<epicsInt64>(addr, data, maxSizeX, maxSizeY);
            break;
        case NDUInt64:
            status |= computeArray<epicsUInt64>(addr, data, maxSizeX, maxSizeY);
            break;
        case NDFloat32:
            status |= computeArray<epicsFloat32>(addr, data, maxSizeX, maxSizeY);
            break;
        case NDFloat64:
            status |= computeArray<epicsFloat64>(addr, data, maxSizeX, maxSizeY);
            break;
    }

    /* Extract the region of interest with binning.
     * If the entire image is being used (no ROI or binning) that's OK because
     * convertImage detects that case and is very efficient */
    m_pRaw->initDimension(&dimsOut[xDim], sizeX);
    m_pRaw->initDimension(&dimsOut[yDim], sizeY);
    if (ndims > 2) m_pRaw->initDimension(&dimsOut[colorDim], 3);
    dimsOut[xDim].binning = binX;
    dimsOut[xDim].offset  = minX;
    dimsOut[xDim].reverse = reverseX;
    dimsOut[yDim].binning = binY;
    dimsOut[yDim].offset  = minY;
    dimsOut[yDim].reverse = reverseY;

    /* We save the most recent image buffer so it can be used in the read() function.
     * Now release it before getting a new version. */	 
    if (this->pArrays[addr]) this->pArrays[addr]->release();
    status = this->pNDArrayPool->convert(m_pRaw,
                                         &this->pArrays[addr],
                                         dataType,
                                         dimsOut);
    if (status) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: error allocating buffer in convert()\n",
                    driverName, functionName);
        return(status);
    }
    pImage = this->pArrays[addr];
    pImage->getInfo(&arrayInfo);
    status = asynSuccess;
    status |= setIntegerParam(addr, NDArraySize,  (int)arrayInfo.totalBytes);
    status |= setIntegerParam(addr, NDArraySizeX, (int)pImage->dims[xDim].size);
    status |= setIntegerParam(addr, NDArraySizeY, (int)pImage->dims[yDim].size);
    if (status) asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: error setting parameters\n",
                    driverName, functionName);
    return(status);
}

template <typename epicsType>
static epicsType transFunc(epicsType value, int trans_mode)
{
    switch(trans_mode)
    {
        case 0:
            return value;
            break;
        case 1:
            return static_cast<epicsType>(sqrt(value));
            break;
        case 2:
            return static_cast<epicsType>(log(1.0 + value));
            break;
        default:
            return value;
            break;
    }
}

// supplied array of x,y,t
template <typename epicsTypeOut, typename epicsTypeIn> 
int CAENMCADriver::computeArray(int addr, const std::vector<epicsTypeIn>& data, int sizeX, int sizeY)
{
    epicsTypeOut *pMono=NULL, *pRed=NULL, *pGreen=NULL, *pBlue=NULL;
    int columnStep=0, rowStep=0, colorMode;
    int status = asynSuccess;
    double exposureTime, gain;
    int i, j, k, trans_mode = 0;

    status = getDoubleParam (ADGain,        &gain);
    status = getIntegerParam(NDColorMode,   &colorMode);
    status = getDoubleParam (ADAcquireTime, &exposureTime);
    status = getIntegerParam(addr, P_eventSpec2DTransMode, &trans_mode);


    switch (colorMode) {
        case NDColorModeMono:
            pMono = (epicsTypeOut *)m_pRaw->pData;
            break;
        case NDColorModeRGB1:
            columnStep = 3;
            rowStep = 0;
            pRed   = (epicsTypeOut *)m_pRaw->pData;
            pGreen = (epicsTypeOut *)m_pRaw->pData+1;
            pBlue  = (epicsTypeOut *)m_pRaw->pData+2;
            break;
        case NDColorModeRGB2:
            columnStep = 1;
            rowStep = 2 * sizeX;
            pRed   = (epicsTypeOut *)m_pRaw->pData;
            pGreen = (epicsTypeOut *)m_pRaw->pData + sizeX;
            pBlue  = (epicsTypeOut *)m_pRaw->pData + 2*sizeX;
            break;
        case NDColorModeRGB3:
            columnStep = 1;
            rowStep = 0;
            pRed   = (epicsTypeOut *)m_pRaw->pData;
            pGreen = (epicsTypeOut *)m_pRaw->pData + sizeX*sizeY;
            pBlue  = (epicsTypeOut *)m_pRaw->pData + 2*sizeX*sizeY;
            break;
    }
    m_pRaw->pAttributeList->add("ColorMode", "Color mode", NDAttrInt32, &colorMode);
	memset(m_pRaw->pData, 0, m_pRaw->dataSize);
    k = 0;
	for (i=0; i<sizeY; i++) {
		switch (colorMode) {
			case NDColorModeMono:
				for (j=0; j<sizeX; j++) {
					pMono[k] = transFunc(static_cast<epicsTypeOut>(gain * data[k]), trans_mode);
                    ++k;
				}
				break;
			case NDColorModeRGB1:
			case NDColorModeRGB2:
			case NDColorModeRGB3:
				for (j=0; j<sizeX; j++) {
					pRed[k] = pGreen[k] = pBlue[k] = transFunc(static_cast<epicsTypeOut>(gain * data[k]), trans_mode);
					pRed   += columnStep;
					pGreen += columnStep;
					pBlue  += columnStep;
					++k;
				}
				pRed   += rowStep;
				pGreen += rowStep;
				pBlue  += rowStep;
				break;
		}
	}
    return(status);
}

extern "C" {

	/// @param[in] portName @copydoc initArg0
	int CAENMCAConfigure(const char *portName, const char *deviceName)
	{
		try
		{
			new CAENMCADriver(portName, deviceName);
			return(asynSuccess);
		}
		catch (const std::exception& ex)
		{
			errlogSevPrintf(errlogFatal, "CAENMCAConfigure failed: %s\n", ex.what());
			return(asynError);
		}
	}

	// EPICS iocsh shell commands 

	static const iocshArg initArg0 = { "portName", iocshArgString };			///< A name for the asyn driver instance we will create - used to refer to it from EPICS DB files
	static const iocshArg initArg1 = { "deviceName", iocshArgString };			///< A name for device to connect to
	static const iocshArg initArg2 = { "simulate", iocshArgInt };			    ///< 1 to simulate

	static const iocshArg * const initArgs[] = { &initArg0, &initArg1, &initArg2 };

	static const iocshFuncDef initFuncDef = { "CAENMCAConfigure", sizeof(initArgs) / sizeof(iocshArg*), initArgs };

	static void initCallFunc(const iocshArgBuf *args)
	{
        CAENMCA::simulate = (args[2].ival != 0 ? true : false);
		CAENMCAConfigure(args[0].sval, args[1].sval);
	}

	/// Register new commands with EPICS IOC shell
	static void CAENMCARegister(void)
	{
		iocshRegister(&initFuncDef, initCallFunc);
	}

	epicsExportRegistrar(CAENMCARegister);
}

