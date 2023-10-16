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


static const char *driverName = "CAENMCADriver"; ///< Name of driver for use in message printing 

/// EPICS driver report function for iocsh dbior command
void CAENMCADriver::report(FILE* fp, int details)
{
	asynPortDriver::report(fp, details);
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
    static CAEN_MCA_HANDLE OpenDevice(const std::string& path, int32_t* index)
    {
        int32_t retcode;
        CAEN_MCA_HANDLE h = CAEN_MCA_OpenDevice(path.c_str(), &retcode, index);
        ERROR_CHECK("CAENMCA::OpenDevice()", retcode);
        return h;
    }

    static void closeDevice(CAEN_MCA_HANDLE handle)
    {
        CAEN_MCA_CloseDevice(handle);
    }

    static void GetData(CAEN_MCA_HANDLE handle, CAEN_MCA_DataType_t dataType, uint64_t dataMask, ...)
    {
        va_list args;
        va_start(args, dataMask);
        int32_t retcode = CAEN_MCA_GetDataV(handle, dataType, dataMask, args);
        va_end(args);
        ERROR_CHECK("CAENMCA::GetData()", retcode);        
    }

    static void SetData(CAEN_MCA_HANDLE handle, CAEN_MCA_DataType_t dataType, uint64_t dataMask, ...)
    {
        va_list args;
        va_start(args, dataMask);
        int32_t retcode = CAEN_MCA_SetDataV(handle, dataType, dataMask, args);
        va_end(args);
        ERROR_CHECK("CAENMCA::SetData()", retcode);        
    }

    static void SendCommand(CAEN_MCA_HANDLE handle, CAEN_MCA_CommandType_t cmdType, uint64_t cmdMaskIn, uint64_t cmdMaskOut, ...)
    {
        va_list args;
        va_start(args, cmdMaskOut);
        int32_t retcode = CAEN_MCA_SendCommandV(handle, cmdType, cmdMaskIn, cmdMaskOut, args);
        va_end(args);
        ERROR_CHECK("CAENMCA::SendCommand()", retcode);        
    }

    static CAEN_MCA_HANDLE GetChildHandle(CAEN_MCA_HANDLE handle, CAEN_MCA_HandleType_t handleType, int32_t index)
    {
        CAEN_MCA_HANDLE h = CAEN_MCA_GetChildHandle(handle, handleType, index);
        if (h == NULL)
        {
            throw CAENMCAException("GetChildHandle(): failed");
        }
        return h;
    }
    
    static CAEN_MCA_HANDLE GetChildHandleByName(CAEN_MCA_HANDLE handle, CAEN_MCA_HandleType_t handleType, const std::string& name)
    {
        CAEN_MCA_HANDLE h = CAEN_MCA_GetChildHandleByName(handle, handleType, name.c_str());
        if (h == NULL)
        {
            throw CAENMCAException("GetChildHandleByName(): failed for name \"" + name + "\"");
        }
        return h;
    }
    
    static void getHandlesFromCollection(CAEN_MCA_HANDLE parent, CAEN_MCA_HandleType_t handleType, std::vector<CAEN_MCA_HANDLE>& handles)
    {
        handles.resize(0);
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

/// Constructor for the webgetDriver class.
/// Calls constructor for the asynPortDriver base class and sets up driver parameters.
///
/// \param[in] portName @copydoc initArg0
CAENMCADriver::CAENMCADriver(const char *portName, const char* deviceName)
	: asynPortDriver(portName,
		2, /* maxAddr */
		NUM_CAEN_PARAMS,
		asynInt32Mask | asynInt32ArrayMask | asynFloat64Mask | asynFloat64ArrayMask | asynOctetMask | asynDrvUserMask, /* Interface mask */
		asynInt32Mask | asynInt32ArrayMask | asynFloat64Mask | asynFloat64ArrayMask | asynOctetMask,  /* Interrupt mask */
		ASYN_CANBLOCK, /* asynFlags.  This driver can block but it is not multi-device */
		1, /* Autoconnect */
		0, /* Default priority */
		0),	/* Default stack size*/
	m_famcode(CAEN_MCA_FAMILY_CODE_UNKNOWN),m_device_h(NULL),m_old_list_filename(2),m_event_file_fd(2, NULL),
    m_event_file_last_pos(2, 0),m_frame_time(2, 0),m_max_event_time(2, 0)
{
	const char *functionName = "CAENMCADriver";

	createParam(P_deviceNameString, asynParamOctet, &P_deviceName);
	createParam(P_availableConfigurationsString, asynParamOctet, &P_availableConfigurations);
	createParam(P_configurationString, asynParamOctet, &P_configuration);
	createParam(P_numEnergySpecString, asynParamInt32, &P_numEnergySpec);
	createParam(P_energySpecClearString, asynParamInt32, &P_energySpecClear);
	createParam(P_energySpecString, asynParamInt32Array, &P_energySpec);
	createParam(P_energySpecTestString, asynParamInt32Array, &P_energySpecTest);
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
    createParam(P_eventsSpecBinWidthString, asynParamFloat64, &P_eventsSpecBinWidth);
    createParam(P_eventsSpecNTimeTagRolloverString, asynParamInt32, &P_eventsSpecNTimeTagRollover);
    createParam(P_eventsSpecNTimeTagResetString, asynParamInt32, &P_eventsSpecNTimeTagReset);
    createParam(P_eventsSpecNEventEnergySatString, asynParamInt32, &P_eventsSpecNEventEnergySat);
    createParam(P_eventsSpecMaxEventTimeString, asynParamFloat64, &P_eventsSpecMaxEventTime);
    
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

void CAENMCADriver::controlAcquisition(int chan_mask, bool start)
{
    CAEN_MCA_CommandType_t cmdtype = (start ? CAEN_MCA_CMD_ACQ_START : CAEN_MCA_CMD_ACQ_STOP);
	if (m_famcode == CAEN_MCA_FAMILY_CODE_XXHEX)
	{
		if (chan_mask == 0x3) // both channels
		{
			CAENMCA::SendCommand(m_device_h, cmdtype, DATAMASK_CMD_NONE, DATAMASK_CMD_NONE);
		}
		else
		{
			for (int i = 0; i < 2; ++i)
			{
				if ((chan_mask & (1 << i)) != 0)
				{
					CAENMCA::SendCommand(m_chan_h[i], cmdtype, DATAMASK_CMD_NONE, DATAMASK_CMD_NONE);
				}
			}
		}
	}
	else
	{
		CAENMCA::SendCommand(m_device_h, cmdtype, DATAMASK_CMD_NONE, DATAMASK_CMD_NONE);
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
            processListFile(i);
		    doCallbacksFloat64Array(m_event_spec_x[i].data(), m_event_spec_x[i].size(), P_eventsSpecX, i);
		    doCallbacksFloat64Array(m_event_spec_y[i].data(), m_event_spec_y[i].size(), P_eventsSpecY, i);
		    doCallbacksInt32Array(m_energy_spec_test[i].data(), m_energy_spec_test[i].size(), P_energySpecTest, i);
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
	return asynPortDriver::readOctet(pasynUser, value, maxChars, nActual, eomReason);
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
	    return asynPortDriver::writeOctet(pasynUser, value, maxChars, nActual);
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
	return asynPortDriver::readInt32(pasynUser, value);
}

asynStatus CAENMCADriver::readFloat64(asynUser *pasynUser, epicsFloat64 *value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *paramName = NULL;
	getParamName(function, &paramName);
	int addr = 0;
	getAddress(pasynUser, &addr);
	return asynPortDriver::readFloat64(pasynUser, value);
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
            CAENMCA::SendCommand(getSpectrumHandle(addr, 0), CAEN_MCA_CMD_ENERGYSPECTRUM_CLEAR, DATAMASK_CMD_NONE, DATAMASK_CMD_NONE);
        }
		else if (function == P_restart)
        {
            CAENMCA::SendCommand(m_device_h, CAEN_MCA_CMD_RESTART , DATAMASK_CMD_NONE, DATAMASK_CMD_NONE);
        }
		asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
			"%s:%s: function=%d, name=%s, value=%d\n",
			driverName, functionName, function, paramName, value);
		return asynPortDriver::writeInt32(pasynUser, value);
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

void CAENMCADriver::processListFile(int channel_id)
{
    std::string filename, deviceName, ethPrefix = "eth://";
    int enabled, save_mode;
	getStringParam(channel_id, P_listFile, filename);
	getIntegerParam(channel_id, P_listEnabled, &enabled);
	getIntegerParam(channel_id, P_listSaveMode, &save_mode);
	getStringParam(P_deviceName, deviceName);
    uint64_t trigger_time = 0, frame_length = 0, max_event_time = 0;
    int16_t energy;
    uint32_t extras;
    const size_t EVENT_SIZE = 14;
    struct stat stat_struct;
    FILE*& f = m_event_file_fd[channel_id];
    if ( (sizeof(trigger_time) + sizeof(energy) + sizeof(extras)) != EVENT_SIZE )
    {
        std::cerr << "size error" << std::endl;
        return;
    }
    if (!enabled || save_mode != CAEN_MCA_SAVEMODE_FILE_BINARY)
    {
        if (f != NULL)
        {
            fclose(f);
            f = NULL;
        }
        return;
    }
    int64_t frame = 0, last_pos, current_pos = 0, new_bytes, nevents;
    m_energy_spec_test[channel_id].resize(32768);
	if (f != NULL)
	{
		current_pos = _ftelli64(f);
	}
    std::string prefix = "\\\\127.0.0.1\\storage\\";
    if (!deviceName.compare(0, ethPrefix.size(), ethPrefix)) {
        prefix = std::string("\\\\") + deviceName.substr(ethPrefix.size()) + "\\storage\\";
    }
    int nevents_real = 0, nbins = 0;
    int ntimerollover = 0, ntimereset = 0, neventenergysat = 0;
    double binw = 1.0;
    getIntegerParam(channel_id, P_eventsSpecNBins, &nbins);
    getDoubleParam(channel_id, P_eventsSpecBinWidth, &binw);
    m_event_spec_x[channel_id].resize(nbins);
    m_event_spec_y[channel_id].resize(nbins);
    if (f == NULL || filename != m_old_list_filename[channel_id] || current_pos == -1 || current_pos != m_event_file_last_pos[channel_id])
    {
        std::string p_filename = prefix + filename;
        if (f != NULL)
        {
            fclose(f);
            f = NULL;
        }
        if (stat(p_filename.c_str(), &stat_struct) != 0 || stat_struct.st_size == 0)
        {
            return;
        }
        if ( (f = _fsopen(p_filename.c_str(), "rb", _SH_DENYNO)) == NULL )
        {
            return;
        }
        m_old_list_filename[channel_id] = filename;
        m_event_file_last_pos[channel_id] = 0;
        m_max_event_time[channel_id] = 0;
        current_pos = 0;
        setIntegerParam(channel_id, P_eventsSpecNEvents, 0);
        setIntegerParam(channel_id, P_eventsSpecNTriggers, 0);
        setIntegerParam(channel_id, P_eventsSpecNTimeTagRollover, 0);
        setIntegerParam(channel_id, P_eventsSpecNTimeTagReset, 0);
        setIntegerParam(channel_id, P_eventsSpecNEventEnergySat, 0);
        std::fill(m_event_spec_y[channel_id].begin(), m_event_spec_y[channel_id].end(), 0.0);
        std::fill(m_energy_spec_test[channel_id].begin(), m_energy_spec_test[channel_id].end(), 0);
    }
    if (_fseeki64(f, 0, SEEK_END) != 0)
    {
        std::cerr << "fseek forward error" << std::endl;
        return;
    }   
    if ( (current_pos = _ftelli64(f)) == -1)
    {
        std::cerr << "ftell curr error" << std::endl;
        return;
    }
    if (_fseeki64(f, m_event_file_last_pos[channel_id], SEEK_SET) != 0)
    {
        std::cerr << "fseek back error" << std::endl;
        return;
    }   
    setDoubleParam(channel_id, 	P_listFileSize, (double)current_pos / (1024.0 * 1024.0)); // convert to MBytes
    new_bytes = current_pos - m_event_file_last_pos[channel_id];
	if (new_bytes < 0)
	{
		fclose(f);
		f = NULL;
		return;
	}
    nevents = new_bytes / EVENT_SIZE;
    if (nevents == 0)
    {
        return;
    }
    for(int i=0; i<nbins; ++i)
    {
        m_event_spec_x[channel_id][i] = i * binw;
    }
    for(int i=0; i<nevents; ++i)
    {
        if (fread(&trigger_time, sizeof(trigger_time), 1, f) != 1)
        {
            std::cerr << "fread time error" << std::endl;
            return;
        }
        if (fread(&energy, sizeof(energy), 1, f) != 1)
        {
            std::cerr << "fread energy error" << std::endl;
            return;
        }
        if (fread(&extras, sizeof(extras), 1, f) != 1)
        {
            std::cerr << "fread extras error" << std::endl;
            return;
        }
        if (extras == 0x8 && energy == 0)
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
        if (extras & 0x80) {
            ++neventenergysat;
        }
        if ( energy > 0 && (!(extras & 0x8)) )
        {
            ++nevents_real;
            uint64_t tdiff = trigger_time - m_frame_time[channel_id];
            if (tdiff > max_event_time) {
                max_event_time = tdiff;
            }
            int n = tdiff / binw;
            if (n >= 0 && n < nbins)
            {
                m_event_spec_y[channel_id][n] += 1.0;
            }
            if (energy != 32767)
            {
                ++(m_energy_spec_test[channel_id][energy]);
            }
        }
        //std::cout << frame << ": " << trigger_time << "  " << trigger_time - m_frame_time[channel_id] << "  " << energy << "  (" << describeFlags(extras) << ")" << std::endl;
    }
    if (max_event_time > m_max_event_time[channel_id]) {
        m_max_event_time[channel_id] = max_event_time;
    }
	m_event_file_last_pos[channel_id] = _ftelli64(f);
    incrIntParam(channel_id, P_eventsSpecNEvents, nevents_real);
    incrIntParam(channel_id, P_eventsSpecNTriggers, frame);
    incrIntParam(channel_id, P_eventsSpecNTimeTagRollover, ntimerollover);
    incrIntParam(channel_id, P_eventsSpecNTimeTagReset, ntimereset);
    incrIntParam(channel_id, P_eventsSpecNEventEnergySat, neventenergysat);
    if (frame_length > 0) {
        setDoubleParam(channel_id, P_eventsSpecTriggerRate, 1.0e12 / (double)frame_length); // frame length units is pico seconds
    } else {
        setDoubleParam(channel_id, P_eventsSpecTriggerRate, 0.0);
    }
    setDoubleParam(channel_id, P_eventsSpecMaxEventTime, m_max_event_time[channel_id]);
    return;
}

void CAENMCADriver::incrIntParam(int channel_id, int param, int incr)
{
    int old_val = 0;
    if (getIntegerParam(channel_id, param, &old_val) == asynSuccess) {
        setIntegerParam(channel_id, param, old_val + incr);
    }
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

	static const iocshArg * const initArgs[] = { &initArg0, &initArg1 };

	static const iocshFuncDef initFuncDef = { "CAENMCAConfigure", sizeof(initArgs) / sizeof(iocshArg*), initArgs };

	static void initCallFunc(const iocshArgBuf *args)
	{
		CAENMCAConfigure(args[0].sval, args[1].sval);
	}

	/// Register new commands with EPICS IOC shell
	static void CAENMCARegister(void)
	{
		iocshRegister(&initFuncDef, initCallFunc);
	}

	epicsExportRegistrar(CAENMCARegister);
}

