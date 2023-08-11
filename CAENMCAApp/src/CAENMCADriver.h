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
	std::vector<epicsFloat64> m_event_spec_x[2];
	std::vector<epicsFloat64> m_event_spec_y[2];
    std::vector<std::string> m_old_list_filename;
    std::vector<FILE*> m_event_file_fd;
    std::vector<int64_t> m_event_file_last_pos;
    std::vector<uint64_t> m_frame_time; 
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
	void startAcquisition(int addr, int value);
	void stopAcquisition(int addr, int value);
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
	void setEnergySpectrumParameter(CAEN_MCA_HANDLE channel, int32_t spectrum_id, const char* parname, double value);
	void getEnergySpectrum(int32_t channel_id, int32_t spectrum_id, std::vector<epicsInt32>& data);
	template <typename T> void energySpectrumSetProperty(CAEN_MCA_HANDLE channel, int32_t spectrum_id, int prop, T value);
    CAEN_MCA_HANDLE getSpectrumHandle(int32_t channel_id, int32_t spectrum_id);
    CAEN_MCA_HANDLE getSpectrumHandle(CAEN_MCA_HANDLE channel, int32_t spectrum_id);
    void setEnergySpectrumFilename(int32_t channel_id, int32_t spectrum_id, const char* filename);
    void setEnergySpectrumNumBins(int32_t channel_id, int32_t spectrum_id, int nbins);
    void setListModeType(int32_t channel_id,  CAEN_MCA_ListSaveMode_t mode);
    void setEnergySpectrumAutosave(int32_t channel_id, int32_t spectrum_id, double period);
    void setListModeEnable(int32_t channel_id,  bool enable);
    void processListFile(int channel_id);

#define FIRST_CAEN_PARAM P_deviceName

	int P_deviceName; // string
    int P_availableConfigurations; // string
    int P_configuration; // string
	int P_numEnergySpec; // int
 	int P_energySpec; // int array
	int P_energySpecCounts; // int
	int P_energySpecNBins; // int
	int P_energySpecFilename; // string
    int P_energySpecRealtime; // float
	int P_energySpeclivetime; // float
	int P_energySpecdeadtime; // float
	int P_energySpecOverflows; // int
    int P_energySpecUnderflows; // int
    int P_energySpecAutosave; // double
    int P_energySpecClear; // int
	int P_nEvents; // int
 	int P_eventsSpecY; // double array
 	int P_eventsSpecX; // double array
 	int P_eventsSpecNEvents; // int
 	int P_eventsSpecNTriggers; // int
    int P_eventsSpecNBins; // int
    int P_eventsSpecBinWidth; // double
	int P_vmon; // double
	int P_vset; // double
	int P_imon; // double
	int P_iset; // double
	int P_tmon; // double
	int P_rampup; // double
	int P_rampdown; // double
	int P_hvPolarity; // int
	int P_hvStatus; // int
    int P_hvRangeName; // string
	int P_chanEnabled; // int
	int P_chanPolarity; // int
    int P_chanMemFull; // int
    int P_chanMemEmpty; // int
	int P_listFile; // string
	int P_listEnabled; // int
    int P_listSaveMode; // int
    int P_listMaxNEvents; // int
    int P_acqRunning; // int
    int P_acqRunningCh; // int
    int P_hvOn; // int
    int P_restart; // int
	int P_acqInit; // int
	int P_acqStartMode; // int
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
#define P_availableConfigurationsString "CONFIG_AVAIL"
#define P_configurationString   "CONFIG"
#define P_numEnergySpecString "NUMENERGYSPEC"
#define P_energySpecString "ENERGYSPEC"
#define P_energySpecClearString "ENERGYSPECCLEAR"
#define P_energySpecCountsString "ENERGYSPECCOUNTS"
#define P_energySpecNBinsString "ENERGYSPECNBINS"
#define P_energySpecFilenameString  "ENERGYSPECFILENAME"
#define P_energySpecRealtimeString "ENERGYSPECREALTIME"
#define P_energySpeclivetimeString "ENERGYSPECLIVETIME"
#define P_energySpecdeadtimeString "ENERGYSPECDEADTIME"
#define P_energySpecOverflowsString "ENERGYSPECOVERFLOWS"
#define P_energySpecUnderflowsString "ENERGYSPECUNDERFLOWS"
#define P_energySpecAutosaveString "ENERGYSPECAUTOSAVE"
#define P_nEventsString "NEVENTS"
#define P_vmonString "VMON"
#define P_vsetString "VSET"
#define P_imonString "IMON"
#define P_isetString "ISET"
#define P_tmonString "TMON"
#define P_rampupString "RAMPUP"
#define P_rampdownString "RAMPDOWN"
#define P_hvPolarityString "HVPOLARITY"
#define P_hvStatusString "HVSTATUS"
#define P_hvRangeNameString "HVRANGENAME"
#define P_chanEnabledString "CHANENABLED"
#define P_chanPolarityString "CHANPOLARITY"
#define P_chanMemFullString "CHANMEMFULL"
#define P_chanMemEmptyString "CHANMEMEMPTY"
#define P_listFileString "LISTFILE"
#define P_listEnabledString "LISTENABLED"
#define P_listSaveModeString "LISTSAVEMODE"
#define P_listMaxNEventsString  "LISTMAXNEVENTS"
#define P_acqRunningString "ACQRUNNING"
#define P_acqRunningChString "ACQRUNNINGCH"
#define P_hvOnString "HVON"
#define P_restartString "RESTART"
#define P_acqInitString "ACQINIT"
#define P_acqStartModeString "ACQSTARTMODE"
#define P_startAcquisitionString "ACQSTART"
#define P_stopAcquisitionString "ACQSTOP"
#define P_eventsSpecYString   "EVENTSPECY"
#define P_eventsSpecXString   "EVENTSPECX"
#define P_eventsSpecNEventsString  "EVENTSPECNEV"
#define P_eventsSpecNTriggersString    "EVENTSPECNTRIG"
#define P_eventsSpecNBinsString   "EVENTSPECNBINS"
#define P_eventsSpecBinWidthString  "EVENTSPECBINW"

#endif /* CAENMCADRIVER_H */
