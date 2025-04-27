/*************************************************************************\ 
* Copyright (c) 2013 Science and Technology Facilities Council (STFC), GB. 
* All rights reverved. 
* This file is distributed subject to a Software License Agreement found 
* in the file LICENSE.txt that is included with this distribution. 
\*************************************************************************/ 

/// @file CAENMCADriver.h Header for #CAENMCADriver class.

#ifndef CAENMCADRIVER_H
#define CAENMCADRIVER_H

#include "ADDriver.h"

/// EPICS Asyn port driver class. 
class epicsShareClass CAENMCADriver : public ADDriver 
{
public:
	CAENMCADriver(const char *portName, const char *deviceAddr, const char* deviceName);
	virtual asynStatus readOctet(asynUser *pasynUser, char *value, size_t maxChars, size_t *nActual, int *eomReason);
	virtual asynStatus readInt32(asynUser *pasynUser, epicsInt32 *value);
    virtual asynStatus readFloat64(asynUser *pasynUser, epicsFloat64 *value);
	virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
	virtual asynStatus writeOctet(asynUser *pasynUser, const char *value, size_t maxChars, size_t *nActual);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    virtual asynStatus readFloat64Array(asynUser *pasynUser, epicsFloat64 *value, size_t nElements, size_t *nIn);
    virtual asynStatus readInt32Array(asynUser *pasynUser, epicsInt32 *value, size_t nElements, size_t *nIn);
    virtual void setShutter(int addr, int open);
	virtual void report(FILE* fp, int details);

private:
    void updateAD(int addr, bool new_events);
    void clearEnergySpectrum(int channel_id);
    NDArray* m_pRaw;
    void setADAcquire(int addr, int acquire);
    template <typename epicsType>
        int computeImage(int addr, const std::vector<epicsType>& data, int nx, int ny);
    template <typename epicsTypeOut, typename epicsTypeIn> 
        int computeArray(int addr, const std::vector<epicsTypeIn>& data, int maxSizeX, int maxSizeY);
    CAEN_MCA_HANDLE m_device_h;
    epicsTime m_start_time[2];
    epicsTime m_stop_time[2];
    std::vector<CAEN_MCA_HANDLE> m_chan_h;
    std::vector<CAEN_MCA_HANDLE> m_hv_chan_h;
	std::vector<epicsInt32> m_energy_spec[2];
	std::vector<epicsInt32> m_energy_spec_event[2];
	std::vector<epicsInt32> m_event_spec_2d[2];
	std::vector<epicsFloat64> m_event_spec_x[2];
	std::vector<epicsFloat64> m_event_spec_y[2];
    std::vector<std::string> m_old_list_filename;
    std::vector<std::tuple<FILE*,FILE*>> m_file_fd;
    std::vector<int64_t> m_event_file_last_pos;
    std::vector<uint64_t> m_frame_time; 
    std::vector<uint64_t> m_max_event_time; 
	CAEN_MCA_BoardFamilyCode_t m_famcode;
    uint32_t m_nbitsEnergy;
    uint32_t m_tsample; // picoseconds
    std::string m_share_path; // hexagon windows share path
    std::string m_file_dir;

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
	void writeRegister(uint32_t address, uint32_t value);
	void writeRegisterMask(uint32_t address, uint32_t value, uint32_t mask);
	void getHVInfo(uint32_t hv_chan_id);
	void getLists(uint32_t channel_id);
    void setListModeFilename(int32_t channel_id, const char* filename);	
	template <typename T> void setData(CAEN_MCA_HANDLE handle, CAEN_MCA_DataType_t dataType, uint64_t dataMask, T value);
	void setListsData(int32_t channel_id, bool timetag, bool energy, bool extras);
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
    bool processListFile(int channel_id);
    void incrIntParam(int channel_id, int param, int incr);
    void setFileNames();
    void incrementRunNumber();
    void endRun();
    void setStartTime(int chan_mask);
    void setStopTime(int chan_mask);
    std::string getEnergySpectrumFilename(int32_t channel_id, int32_t spectrum_id);
    std::string getListModeFilename(int32_t channel_id);
    void copyData(const std::string& filePrefix, const char* runNumber);
    void setRunNumberFromIRunNumber();
    bool setTimingRegisters();
    bool checkTimingRegisters();
    void cycleAcquisition();
    void closeListFiles();

#define FIRST_CAEN_PARAM P_deviceName

	int P_deviceName; // string
	int P_deviceAddr; // string
    int P_availableConfigurations; // string
    int P_configuration; // string
	int P_numEnergySpec; // int
 	int P_energySpec; // int array
 	int P_energySpecEvent; // int array
 	int P_energySpecEventTMin; // float
 	int P_energySpecEventTMax; // float
 	int P_energySpecEventNEvents; // int 
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
	int P_nEventsProcessed; // int
 	int P_eventsSpecY; // double array
 	int P_eventsSpecX; // double array
 	int P_eventsSpecTMin; // float
 	int P_eventsSpecTMax; // float
    int P_eventsSpecNBins; // int
    int P_eventsSpecTBinWidth; // float
 	int P_eventsSpecNEvents; // int
 	int P_eventsSpecNTriggers; // int
 	int P_eventsSpecTriggerRate; // double
 	int P_eventsSpecNTimeTagRollover; // int
 	int P_eventsSpecNTimeTagReset; // int
 	int P_eventsSpecNEventEnergySat; // int
    int P_eventsSpecMaxEventTime; // double
    int P_eventSpec2DTimeMin; // double
    int P_eventSpec2DTimeMax; // double
    int P_eventSpec2DNTimeBins; // int
    int P_eventSpec2DTBinWidth; // double
    int P_eventSpec2DEnergyBinGroup; // int
    int P_eventSpec2DTransMode; // int
	int P_nFakeEvents; // int
	int P_nImpDynamSatEvent; // int
	int P_nPileupEvent; // int
	int P_nEventEnergyOutSCA; // int
	int P_nEventDurSatInhibit; // int
	int P_nEventNotBinned; // int
	int P_nEventEnergyDiscard; // int
    int P_loadDataFileName; // string
    int P_loadDataFile; // int
    int P_loadDataStatus; // int
    int P_reloadLiveData; // int
    int P_runNumber; // string
    int P_iRunNumber; // int
    int P_filePrefix; // string
    int P_runTitle; // string
    int P_runComment; // string
    int P_startTime; // string
    int P_stopTime; // string
    int P_runDuration; // int
    int P_endRun; // int
    int P_eventSpecRateTMin; // float
    int P_eventSpecRateTMax; // float
    int P_eventSpecRate; // float    
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
    int P_listFileSize; // float
    int P_acqRunning; // int
    int P_acqRunningCh; // int
    int P_hvOn; // int
    int P_restart; // int
	int P_acqInit; // int
	int P_acqStartMode; // int
    int P_timingRegisterChan; // int
    int P_timingRegisters; // int
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

#define P_deviceNameString "DEVICENAME"
#define P_deviceAddrString "DEVICEADDR"
#define P_availableConfigurationsString "CONFIG_AVAIL"
#define P_configurationString   "CONFIG"
#define P_numEnergySpecString "NUMENERGYSPEC"
#define P_energySpecString "ENERGYSPEC"
#define P_energySpecEventString "ENERGYSPECEVENT"
#define P_energySpecEventTMinString "ENERGYSPECEVENTTMIN"
#define P_energySpecEventTMaxString "ENERGYSPECEVENTTMAX"
#define P_energySpecEventNEventsString "ENERGYSPECEVENTNEVENTS"
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
#define P_nEventsProcessedString "NEVENTSPROCESSED"
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
#define P_listFileSizeString "LISTFILESIZE"
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
#define P_eventsSpecTriggerRateString  "EVENTSPECTRIGRATE"
#define P_eventsSpecNBinsString   "EVENTSPECNBINS"
#define P_eventsSpecTBinWidthString      "EVENTSPECTBINW"
#define P_eventsSpecTMinString  "EVENTSPECTMIN"
#define P_eventsSpecTMaxString  "EVENTSPECTMAX"
#define P_eventsSpecNTimeTagRolloverString "EVENTSPECNTTROLLOVER"
#define P_eventsSpecNTimeTagResetString "EVENTSPECNTTRESET"
#define P_eventsSpecNEventEnergySatString "EVENTSPECNENGSAT"
#define P_eventsSpecMaxEventTimeString "EVENTSPECMAXEVENTTIME"
#define P_nFakeEventsString         "NFAKEEVENTS"
#define P_nImpDynamSatEventString   "NIMPDYNAMSATEVENT"
#define P_nPileupEventString        "NPILEUPEVENT"
#define P_nEventEnergyOutSCAString  "NEVENTENERGYOUTSCA"
#define P_nEventDurSatInhibitString "NEVENTDURSATINHIBIT"
#define P_nEventNotBinnedString     "NEVENTNOTBINNED"
#define P_nEventEnergyDiscardString "NEVENTENERGYDISCARD"
#define P_eventSpec2DTimeMinString        "EVENTSPEC2DTIMEMIN"
#define P_eventSpec2DTimeMaxString        "EVENTSPEC2DTIMEMAX"
#define P_eventSpec2DNTimeBinsString      "EVENTSPEC2DNTIMEBINS"
#define P_eventSpec2DEnergyBinGroupString       "EVENTSPEC2DENGBINGROUP"
#define P_eventSpec2DTBinWidthString      "EVENTSPEC2DTBINW"
#define P_loadDataFileNameString          "LOADDATAFILENAME"
#define P_loadDataFileString              "LOADDATAFILE"
#define P_loadDataStatusString        "LOADDATASTATUS"
#define P_reloadLiveDataString          "RELOADLIVEDATA"
#define P_eventSpec2DTransModeString      "EVENTSPEC2DTRANSMODE"
#define P_runTitleString "RUNTITLE"
#define P_runCommentString "RUNCOMMENT"
#define P_runNumberString "RUNNUMBER"
#define P_iRunNumberString "IRUNNUMBER"
#define P_filePrefixString "FILEPREFIX"
#define P_startTimeString "STARTTIME"
#define P_stopTimeString "STOPTIME"
#define P_runDurationString "RUNDURATION"
#define P_endRunString "ENDRUN"
#define P_eventSpecRateTMinString "EVENTSPECRATETMIN"
#define P_eventSpecRateTMaxString "EVENTSPECRATETMAX"
#define P_eventSpecRateString "EVENTSPECRATE"
#define P_timingRegisterChanString "TIMINGREGISTERCHAN"
#define P_timingRegistersString "TIMINGREGISTERS"

#endif /* CAENMCADRIVER_H */

