/*************************************************************************\ 
* Copyright (c) 2013 Science and Technology Facilities Council (STFC), GB. 
* All rights reverved. 
* This file is distributed subject to a Software License Agreement found 
* in the file LICENSE.txt that is included with this distribution. 
\*************************************************************************/ 

/// @file CAENMCADriver.h Header for #CAENMCADriver class.

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
	virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
	virtual asynStatus writeOctet(asynUser *pasynUser, const char *value, size_t maxChars, size_t *nActual);

	virtual void report(FILE* fp, int details);
	
private:

    CAEN_MCA_HANDLE m_device_h;
    std::vector<CAEN_MCA_HANDLE> m_chan_h;
    std::vector<CAEN_MCA_HANDLE> m_hv_chan_h;
	std::vector<epicsInt32> m_energy_spec[2];
	CAEN_MCA_BoardFamilyCode_t m_famcode;

	double getParameterValue(CAEN_MCA_HANDLE handle, const char *name);
	void setParameterValue(CAEN_MCA_HANDLE handle, const char *name, double value);
	void getParameterInfo(CAEN_MCA_HANDLE handle, const char *name);
	void getParameterInfo(CAEN_MCA_HANDLE parameter);
	void getParameterCollectionInfo(CAEN_MCA_HANDLE handle);
	std::string getParameterValueList(CAEN_MCA_HANDLE handle, const char *name);
	void setParameterValueList(CAEN_MCA_HANDLE handle, const char *name, const std::string& value);
	void setHVState(CAEN_MCA_HANDLE hvchan, bool is_on);
	bool isHVOn(CAEN_MCA_HANDLE hvchan);
	bool isAcqRunning();
	bool isAcqRunning(CAEN_MCA_HANDLE chan);
	void startAcquisition(int chan_mask);
	void stopAcquisition(int chan_mask);
	void controlAcquisition(int chan_mask, bool start);
	void getBoardInfo();
	void getChannelInfo(int32_t channel_id);
    void loadConfiguration(const char* name);
    void listConfigurations(std::vector<std::string>& configs);
	void readRegister(uint32_t address, uint32_t& value);
	void getHVInfo(uint32_t hv_chan_id);
	void getLists(uint32_t channel_id);
    void setListModeFilename(int32_t channel_id, const char* filename);	
	template <typename T> void setData(CAEN_MCA_HANDLE handle, CAEN_MCA_DataType_t dataType, uint64_t dataMask, T value);
	void setListsData(CAEN_MCA_HANDLE channel, bool timetag, bool energy, bool extras);
	void energySpectrumClear(CAEN_MCA_HANDLE channel, int32_t spectrum_id);
	void setEnergySpectrumParameter(CAEN_MCA_HANDLE channel, int32_t spectrum_id, const char* parname, double value);
	void getEnergySpectrum(int32_t channel_id, int32_t spectrum_id, std::vector<epicsInt32>& data);
	template <typename T> void energySpectrumSetProperty(CAEN_MCA_HANDLE channel, int32_t spectrum_id, int prop, T value);
    CAEN_MCA_HANDLE getSpectrumHandle(int32_t channel_id, int32_t spectrum_id);
    CAEN_MCA_HANDLE getSpectrumHandle(CAEN_MCA_HANDLE channel, int32_t spectrum_id);
    void setEnergySpectrumFilename(int32_t channel_id, int32_t spectrum_id, const char* filename);
    void setEnergySpectrumNumBins(int32_t channel_id, int32_t spectrum_id, int nbins);

#define FIRST_CAEN_PARAM P_deviceName

	int P_deviceName; // string
	int P_energySpec; // int array
	int P_energySpecCounts; // int
	int P_nEvents; // int
	int P_vmon; // double
	int P_listFile; // string
	int P_startAcquisition; // int
	int P_stopAcquisition; // int

#define LAST_CAEN_PARAM 	P_stopAcquisition
#define NUM_CAEN_PARAMS	(&LAST_CAEN_PARAM - &FIRST_CAEN_PARAM + 1)

	static void pollerTaskC(void* arg)
	{
	    CAENMCADriver* driver = static_cast<CAENMCADriver*>(arg);
		driver->pollerTask();	    
	}
	void pollerTask();
};

#define P_deviceNameString "DEVICE"
#define P_vmonString "VMON"
#define P_energySpecString "ENERGYSPEC"
#define P_nEventsString "NEVENTS"
#define P_listFileString "LISTFILE"
#define P_energySpecCountsString "ENERGYSPECCOUNTS"
#define P_startAcquisitionString "START"
#define P_stopAcquisitionString "STOP"

#endif /* CAENMCADRIVER_H */
