/*************************************************************************\
* Copyright (c) 2013 Science and Technology Facilities Council (STFC), GB.
* All rights reverved.
* This file is distributed subject to a Software License Agreement found
* in the file LICENSE.txt that is included with this distribution.
\*************************************************************************/

/// @file CAENMCADriver.cpp Implementation of #CAENMCADriver class and CAENMCAConfigure() iocsh command

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <inttypes.h>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <map>

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
	m_famcode(CAEN_MCA_FAMILY_CODE_UNKNOWN),m_device_h(NULL)

{
	const char *functionName = "CAENMCADriver";

	createParam(P_deviceNameString, asynParamOctet, &P_deviceName);
	createParam(P_vmonString, asynParamFloat64, &P_vmon);
	createParam(P_energySpecCountsString, asynParamInt32, &P_energySpecCounts);
	createParam(P_nEventsString, asynParamInt32, &P_nEvents);
	createParam(P_startAcquisitionString, asynParamInt32, &P_startAcquisition);
	createParam(P_stopAcquisitionString, asynParamInt32, &P_stopAcquisition);	
	createParam(P_energySpecString, asynParamInt32Array, &P_energySpec);
	createParam(P_listFileString, asynParamOctet, &P_listFile);

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

//CAEN_MCA_HANDLE parameter = CAEN_MCA_GetChildHandleByName(m_channel, CAEN_MCA_HANDLE_PARAMETER, PARAM_CH_ENABLED);
//PARAM_CH_POLARITY      is   POLARITY_POSITIVE or POLARITY_NEGATIVE
//PARAM_CH_ACQ_RUN


void CAENMCADriver::getHVInfo(uint32_t hv_chan_id)
{
	CAEN_MCA_HANDLE hvchannel = m_hv_chan_h[hv_chan_id];
	CAEN_MCA_HANDLE hvrange = CAENMCA::GetChildHandle(hvchannel, CAEN_MCA_HANDLE_HVRANGE, 0);
	double vset_min, vset_max, vset_incr, vmax_max, vmon, imon, hvpol, hvstat;
	CAENMCA::GetData(
		hvrange,
		CAEN_MCA_DATA_HVRANGE_INFO,
		DATAMASK_HVRANGEINFO_VSET_MIN |
		DATAMASK_HVRANGEINFO_VSET_MAX |
		DATAMASK_HVRANGEINFO_VSET_INCR |
		DATAMASK_HVRANGEINFO_VMAX_MAX,
		&vset_min,
		&vset_max,
		&vset_incr,
		&vmax_max);
	vmon = getParameterValue(hvrange, "PARAM_HVRANGE_VMON");
	imon = getParameterValue(hvrange, "PARAM_HVRANGE_IMON");
	hvpol = getParameterValue(hvchannel, "PARAM_HVCH_POLARITY");
	hvstat = getParameterValue(hvchannel, "PARAM_HVCH_STATUS");
	setDoubleParam(hv_chan_id, P_vmon, vmon);
//	std::cerr << "vmon " << vmon << " imon " << imon << " hvpol " << (hvpol == CAEN_MCA_POLARITY_TYPE_POSITIVE ? " POS" : " NEG") << " hvstat " << hvstat << std::endl;
//	getParameterInfo(hvrange, "PARAM_HVRANGE_VMON");
//	getParameterInfo(hvchannel, "PARAM_HVCH_STATUS");
}

void CAENMCADriver::stopAcquisition(int chan_mask)
{
    controlAcquisition(chan_mask, false);
}

void CAENMCADriver::startAcquisition(int chan_mask)
{
    controlAcquisition(chan_mask, true);
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
	uint32_t nEnergySpectra;
	CAENMCA::GetData(
		channel,
		CAEN_MCA_DATA_CHANNEL_INFO,
		DATAMASK_CHANNELINFO_NENERGYSPECTRA,
		&nEnergySpectra
	);
	fprintf(stdout, "Num energy spectra: %d\n", nEnergySpectra);	
}


void CAENMCADriver::getBoardInfo()
{

	uint32_t channels, hvchannels, serialNum, nbits, tsample, pcbrev, nbitsEnergy;
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
		&tsample,
		&nbitsEnergy
	);
 	
	fprintf(stdout, "Device name: %s\n", modelName);
	fprintf(stdout, "PCB revision: %d\n", pcbrev);
	fprintf(stdout, "Number of input channels: %d\n", channels);
	fprintf(stdout, "Number of ADC bits: %d\n", nbits);
	fprintf(stdout, "Number of Energy bits: %d\n", nbitsEnergy);
	fprintf(stdout, "Sample period (in picoseconds): %d\n", tsample);
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
//	std::cerr << "nbins " << nbins << " entries " << nentries << " filename " << filename << std::endl;
	data.resize(nbins);
}
	
void CAENMCADriver::setListModeFilename(int32_t channel_id, const char* filename)
{ 
	CAENMCA::SetData(m_chan_h[channel_id], CAEN_MCA_DATA_LIST_MODE, DATAMASK_LIST_FILENAME, filename);
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

void CAENMCADriver::setEnergySpectrumNumBins(int32_t channel_id, int32_t spectrum_id, int nbins)
{
	setParameterValue(getSpectrumHandle(channel_id, spectrum_id), "PARAM_ENERGY_SPECTRUM_NBINS", (double)nbins);
}

void CAENMCADriver::energySpectrumClear(CAEN_MCA_HANDLE channel, int32_t spectrum_id)
{
	CAEN_MCA_HANDLE spectrum = CAENMCA::GetChildHandle(channel, CAEN_MCA_HANDLE_ENERGYSPECTRUM, spectrum_id);
	CAENMCA::SendCommand(spectrum, CAEN_MCA_CMD_ENERGYSPECTRUM_CLEAR, DATAMASK_CMD_NONE, DATAMASK_CMD_NONE);
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
	while(true)
	{
	    lock();

        //std::cerr << "hv0 on " << isHVOn(m_hv_chan_h[0]) << std::endl;
        //std::cerr << "hv1 on " << isHVOn(m_hv_chan_h[1]) << std::endl;
	
	    //std::cerr << isAcqRunning() << " " << isAcqRunning(m_chan_h[0]) << " " << isAcqRunning(m_chan_h[1]) << std::endl;
	    for(int i=0;i<2; ++i)
		{
	        getEnergySpectrum(i, 0, m_energy_spec[i]);
		    doCallbacksInt32Array(&(m_energy_spec[i][0]), m_energy_spec[i].size(), P_energySpec, i);
            getHVInfo(i);
		    getLists(i);
		    callParamCallbacks(i);
		}
		
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
		  CAENMCA::SetData(m_chan_h[addr], CAEN_MCA_DATA_LIST_MODE, DATAMASK_LIST_FILENAME, value_s.c_str());
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
			startAcquisition(0x3);
		}
		else if (function == P_stopAcquisition)
		{
			stopAcquisition(0x3);
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
	datatimetag.resize(LISTS_DATA_MAXLEN);
	std::vector<uint32_t> dataenergy;
	dataenergy.resize(LISTS_DATA_MAXLEN);
	std::vector<uint16_t> dataflags;
	dataflags.resize(LISTS_DATA_MAXLEN);
    CAEN_MCA_HANDLE channel = m_chan_h[channel_id];
	int32_t ret = CAEN_MCA_GetData(
		channel,
		CAEN_MCA_DATA_LIST_MODE,
		DATAMASK_LIST_ENABLE |
		DATAMASK_LIST_SAVEMODE |
		DATAMASK_LIST_FILENAME |
		DATAMASK_LIST_FILE_DATAMASK |
		DATAMASK_LIST_GETFAKEEVTS,
//		DATAMASK_LIST_MAXNEVTS |
//		DATAMASK_LIST_NEVTS |
//		DATAMASK_LIST_DATA_TIMETAG |
//		DATAMASK_LIST_DATA_ENERGY |
//		DATAMASK_LIST_DATA_FLAGS_DATAMASK,
		&enabled,
		&savemode,
		filename,
		&datamask,
		&getfake
//		&maxnevts,
//		&nevts
//		&(datatimetag[0]),
//		&(dataenergy[0]),
//		&(dataflags[0])
	);
	
	datatimetag.resize(nevts);
	dataenergy.resize(nevts);
	dataflags.resize(nevts);
	
	setIntegerParam(channel_id, P_nEvents, nevts);
	setStringParam(channel_id, P_listFile, filename);

// unless CAEN_MCA_SAVEMODE_MEMORY then should not  use DATAMASK_LIST_DATA_ as they reset buffer
	
#if 0
		fprintf(stdout, "Enabled: %" PRIu32 "\n", enabled);

		fprintf(stdout, "Save mode: ");
		switch (savemode) {
		case CAEN_MCA_SAVEMODE_FILE_ASCII: fprintf(stdout, "File (ASCII)\n"); break;
		case CAEN_MCA_SAVEMODE_FILE_BINARY: fprintf(stdout, "File (binary)\n"); break;
		case CAEN_MCA_SAVEMODE_MEMORY: fprintf(stdout, "Memory\n"); break;
		}

		fprintf(stdout, "Get fake events: %s\n", getfake ? "true" : "false");

		bool timetag = datamask & LIST_FILE_DATAMASK_TIMETAG;
		bool energy = datamask & LIST_FILE_DATAMASK_ENERGY;
		bool extras = datamask & LIST_FILE_DATAMASK_FLAGS;

		fprintf(stdout, "Data mask (for file modes): ");
		fprintf(stdout, "Timetag (%s)\t", timetag ? "true" : "false");
		fprintf(stdout, "Energy (%s)\t", energy ? "true" : "false");
		fprintf(stdout, "Extras (%s)\t", extras ? "true" : "false");
		fprintf(stdout, "\n");
#endif
        if (nevts > 0)
		{
			fprintf(stdout, "Channel %" PRIu32 " Events: %" PRIu32 " (max: %" PRIu32 ") Filename: %s\n", channel_id, nevts, maxnevts, filename);
		}
#if 0
		fprintf(stdout, "First 10 events received:\n");
		for (uint32_t i = 0; i < nevts && i < 10; i++) {
			fprintf(stdout, "\t#%" PRIu32 ":", i);
			fprintf(stdout, "\tTimetag: %" PRIu64 "", datatimetag[i]);
			fprintf(stdout, "\tEnergy: %" PRIu32 "", dataenergy[i]);
			fprintf(stdout, "\tFlags: 0x%08" PRIx16 "", dataflags[i]);
			fprintf(stdout, "\n");
		}
#endif
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

