#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cstdio>
#include <cstring>
#include <epicsThread.h>

#include <highfive/highfive.hpp>
namespace hf = HighFive;

#include "h5nexus.h"

hf::Group createNeXusGroup(hf::File& current, const std::string& name, const std::string& nxclass)
{
    hf::Group new_group = current.createGroup(name);
    new_group.createAttribute<std::string>("NX_class", nxclass);
    return new_group;
}

hf::Group createNeXusGroup(hf::Group& current, const std::string& name, const std::string& nxclass)
{
    hf::Group new_group = current.createGroup(name);
    new_group.createAttribute<std::string>("NX_class", nxclass);
    return new_group;
}

void createNeXusStructure(const std::string&filename, hf::File& out_file)
{
    out_file.createAttribute<std::string>("HDF5_Version", "1.10.0");
    out_file.createAttribute<std::string>("file_name", filename);
    out_file.createAttribute<std::string>("file_time", "now");
    out_file.createAttribute<std::string>("NeXus_version", "0.0.0");
    out_file.createAttribute<std::string>("IDF_version", "2");
    out_file.createAttribute<std::string>("beamline", "MUX");
    out_file.createAttribute<std::string>("name", "MUX");
    out_file.createAttribute<std::string>("program_name", "CAENMCA-IOC-01");
    out_file.createAttribute<std::string>("run_cycle", "25_3");
    hf::Group raw_data_1 = createNeXusGroup(out_file, "raw_data_1", "NXentry");
    hf::Group instrument = createNeXusGroup(raw_data_1, "instrument", "NXinstrument");
    instrument.createDataSet<std::string>("name", "MUX");
    hf::Group source = createNeXusGroup(instrument, "source", "NXsource");
    source.createDataSet<std::string>("name", "ISIS");
    source.createDataSet<std::string>("probe", "negative muons");
    source.createDataSet<std::string>("type", "Pulsed Muon Source");    
    hf::Group sample = createNeXusGroup(raw_data_1, "sample", "NXsample");
    sample.createDataSet("distance", 0.0);
    hf::Group user = createNeXusGroup(raw_data_1, "user_1", "NXuser");
}

