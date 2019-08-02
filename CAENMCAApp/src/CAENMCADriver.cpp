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
#include <exception>
#include <iostream>
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
		return "unknown";
		break;
	}
}



#define ERROR_CHECK(__ret) \
    if (__ret != CAEN_MCA_RetCode_Success) { \
        throw std::runtime_error(translateCAENError(__ret)); \
    }

/// Constructor for the webgetDriver class.
/// Calls constructor for the asynPortDriver base class and sets up driver parameters.
///
/// \param[in] portName @copydoc initArg0
CAENMCADriver::CAENMCADriver(const char *portName, const char* deviceName)
	: asynPortDriver(portName,
		0, /* maxAddr */
		NUM_CAEN_PARAMS,
		asynInt32Mask | asynInt32ArrayMask | asynFloat64Mask | asynFloat64ArrayMask | asynOctetMask | asynDrvUserMask, /* Interface mask */
		asynInt32Mask | asynInt32ArrayMask | asynFloat64Mask | asynFloat64ArrayMask | asynOctetMask,  /* Interrupt mask */
		ASYN_CANBLOCK, /* asynFlags.  This driver can block but it is not multi-device */
		1, /* Autoconnect */
		0, /* Default priority */
		0),	/* Default stack size*/
	m_famcode(CAEN_MCA_FAMILY_CODE_UNKNOWN)

{
	const char *functionName = "CAENMCADriver";
	createParam(P_deviceNameString, asynParamOctet, &P_deviceName);
	createParam(P_startAcquisitionString, asynParamInt32, &P_startAcquisition);
	setStringParam(P_deviceName, deviceName);
	int32_t ret;
	m_device = CAEN_MCA_OpenDevice(deviceName, &ret, NULL);
	ERROR_CHECK(ret);
	ret = CAEN_MCA_GetData(m_device, CAEN_MCA_DATA_BOARD_INFO, DATAMASK_BRDINFO_FAMCODE, &m_famcode);
	ERROR_CHECK(ret);
	getBoardInfo();

	if (epicsThreadCreate("CAENMCADriverPoller",
		epicsThreadPriorityMedium,
		epicsThreadGetStackSize(epicsThreadStackMedium),
		(EPICSTHREADFUNC)pollerTaskC, this) == 0)
	{
		printf("%s:%s: epicsThreadCreate failure\n", driverName, functionName);
		return;
	}
}

// CAEN_MCA_CloseDevice(m_device);

void CAENMCADriver::startAcquisition(int chan_mask)
{
	int32_t ret;
	if (m_famcode == CAEN_MCA_FAMILY_CODE_XXHEX)
	{
		if (chan_mask == 0x3) // both channels
		{
			ret = CAEN_MCA_SendCommand(m_device, CAEN_MCA_CMD_ACQ_START, DATAMASK_CMD_NONE, DATAMASK_CMD_NONE);
			ERROR_CHECK(ret);
		}
		else
		{
			for (int i = 0; i < 2; ++i)
			{
				if ((chan_mask & (1 << i)) != 0)
				{
					CAEN_MCA_HANDLE channel = CAEN_MCA_GetChildHandle(m_device, CAEN_MCA_HANDLE_CHANNEL, i);
					ret = CAEN_MCA_SendCommand(channel, CAEN_MCA_CMD_ACQ_START, DATAMASK_CMD_NONE, DATAMASK_CMD_NONE);
					ERROR_CHECK(ret);
				}
			}
		}
	}
	else
	{
		ret = CAEN_MCA_SendCommand(m_device, CAEN_MCA_CMD_ACQ_START, DATAMASK_CMD_NONE, DATAMASK_CMD_NONE);
		ERROR_CHECK(ret);
	}
}

void CAENMCADriver::getBoardInfo()
{

	uint32_t channels, hvchannels, serialNum, nbits, tsample, pcbrev;
	char modelName[MODEL_NAME_MAXLEN] = { '\0' };

	int32_t ret = CAEN_MCA_GetData(
		m_device,
		CAEN_MCA_DATA_BOARD_INFO,
		DATAMASK_BRDINFO_MODELNAME |
		DATAMASK_BRDINFO_NCHANNELS |
		DATAMASK_BRDINFO_SERIALNUM |
		DATAMASK_BRDINFO_NHVCHANNELS |
		DATAMASK_BRDINFO_PCBREV |
		DATAMASK_BRDINFO_ADC_BIT_COUNT |
		DATAMASK_BRDINFO_TSAMPLE_PS,
		modelName,
		&channels,
		&serialNum,
		&hvchannels,
		&pcbrev,
		&nbits,
		&tsample
	);
	ERROR_CHECK(ret);

	fprintf(stdout, "Device name: %s\n", modelName);
	fprintf(stdout, "PCB revision: %d\n", pcbrev);
	fprintf(stdout, "Number of input channels: %d\n", channels);
	fprintf(stdout, "Number of ADC bits: %d\n", nbits);
	fprintf(stdout, "Sample period (in picoseconds): %d\n", tsample);
	fprintf(stdout, "Number of HV channels: %d\n", channels);
	fprintf(stdout, "Serial number: %d\n", serialNum);
}

void CAENMCADriver::pollerTask()
{
}

asynStatus CAENMCADriver::readOctet(asynUser *pasynUser, char *value, size_t maxChars, size_t *nActual, int *eomReason)
{
	int function = pasynUser->reason;
	asynStatus status = asynSuccess;
	const char *paramName = NULL;
	getParamName(function, &paramName);
	return asynPortDriver::readOctet(pasynUser, value, maxChars, nActual, eomReason);
}

asynStatus CAENMCADriver::readInt32(asynUser *pasynUser, epicsInt32 *value)
{
	int function = pasynUser->reason;
	return asynPortDriver::readInt32(pasynUser, value);
}

asynStatus CAENMCADriver::readFloat64(asynUser *pasynUser, epicsFloat64 *value)
{
	int function = pasynUser->reason;
	return asynPortDriver::readFloat64(pasynUser, value);
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

