/*************************************************************************\ 
* Copyright (c) 2013 Science and Technology Facilities Council (STFC), GB. 
* All rights reverved. 
* This file is distributed subject to a Software License Agreement found 
* in the file LICENSE.txt that is included with this distribution. 
\*************************************************************************/ 

/// @file webgetDriver.h Header for #webgetDriver class.
/// @author Freddie Akeroyd, STFC ISIS Facility, GB

#ifndef CAENMCADRIVER_H
#define CAENMCADRIVER_H

/// EPICS Asyn port driver class. 
class epicsShareClass CAENMCADriver : public asynPortDriver 
{
public:
	CAENMCADriver(const char *portName, const char *deviceName);
	virtual asynStatus readOctet(asynUser *pasynUser, char *value, size_t maxChars, size_t *nActual, int *eomReason);
	virtual asynStatus readInt32(asynUser *pasynUser, epicsInt32 *value);
    virtual asynStatus readFloat64(asynUser *pasynUser, epicsFloat64 *value);
	virtual void report(FILE* fp, int details);
	
private:

    CAEN_MCA_HANDLE m_device;
	CAEN_MCA_BoardFamilyCode_t m_famcode;

	void startAcquisition(int chan_mask);
	void getBoardInfo();

#define FIRST_CAEN_PARAM P_deviceName

	int P_deviceName; // string
	int P_startAcquisition; // int

#define LAST_CAEN_PARAM 	P_startAcquisition
#define NUM_CAEN_PARAMS	(&LAST_CAEN_PARAM - &FIRST_CAEN_PARAM + 1)

	static void pollerTaskC(void* arg)
	{
//	    webgetDriver* driver = static_cast<webgetDriver*>(arg);
//		driver->pollerTask();	    
	}
//	void pollerTask();
};

#define P_deviceNameString "DEVICE"
#define P_startAcquisitionString "START"

#endif /* WEBGETDRIVER_H */
