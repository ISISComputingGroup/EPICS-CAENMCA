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
    hf::Group raw_data_1 = createNeXusGroup(out_file, "raw_data_1", "NXentry");
}

