#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cstdio>
#include <cstring>
#include <epicsThread.h>

#include <highfive/highfive.hpp>
namespace hf = HighFive;

#ifndef _WIN32
#define _fsopen(a,b,c) fopen(a,b)
#define _ftelli64 ftell
#define _fseeki64 fseek
#endif /* ndef _WIN32 */

static std::string describeFlags(unsigned flags);

// add n items to dataset that already contains n_total
template <typename T>
void appendData(size_t n, size_t n_total, hf::DataSet& dset, T* data)
{
    if (n == 0)
    {
        return;
    }
    dset.resize({n_total + n});
    dset.select({n_total}, {n}).write_raw(data);    
}

static hf::Group createNeXusGroup(hf::File& current, const std::string& name, const std::string& nxclass)
{
    hf::Group new_group = current.createGroup(name);
    new_group.createAttribute<std::string>("NX_class", nxclass);
    return new_group;
}

static hf::Group createNeXusGroup(hf::Group& current, const std::string& name, const std::string& nxclass)
{
    hf::Group new_group = current.createGroup(name);
    new_group.createAttribute<std::string>("NX_class", nxclass);
    return new_group;
}

static const char* getArgStr(int arg, int argc, char* argv[], const char* default_arg)
{
    return arg < argc ? argv[arg] : default_arg;    
}

static double getArgDouble(int arg, int argc, char* argv[], double default_arg)
{
    return arg < argc ? atof(argv[arg]) : default_arg;    
}

int main(int argc, char* argv[])
{
    const char* input_filename = getArgStr(1, argc, argv, NULL);
    const char* output_filename = getArgStr(2, argc, argv, NULL);
    //  energy =  a * energy_raw + b
    double energy_a = getArgDouble(3, argc, argv, 1.0);
    double energy_b = getArgDouble(4, argc, argv, 0.0);
    const int NEVENTS_READ = 1000;
    typedef uint64_t trigger_time_t, frame_time_t;
    typedef uint32_t extras_t, frame_number_t;
    typedef int16_t energy_t;
    trigger_time_t trigger_time[NEVENTS_READ];
    frame_time_t frame_time[NEVENTS_READ];
    energy_t energy_raw[NEVENTS_READ];
    double energy[NEVENTS_READ];
    extras_t extras[NEVENTS_READ];
    frame_number_t frame_number[NEVENTS_READ];
    int fake_events = 0;
    const size_t EVENT_SIZE = 14;

    if ( (sizeof(trigger_time[0]) + sizeof(energy_raw[0]) + sizeof(extras[0])) != EVENT_SIZE )
    {
        std::cerr << "size error" << std::endl;
        return 0;
    }
    hf::File out_file(output_filename, hf::File::Create | hf::File::Truncate);
    out_file.createAttribute<std::string>("HDF5_Version", "1.10.0");
    out_file.createAttribute<std::string>("file_name", output_filename);
    out_file.createAttribute<std::string>("file_time", "now");
    out_file.createAttribute<std::string>("NeXus_version", "0.0.0");
    hf::DataSpace dataspace = hf::DataSpace({0}, {hf::DataSpace::UNLIMITED});
    hf::DataSetCreateProps props;
    props.add(hf::Chunking(std::vector<hsize_t>{NEVENTS_READ}));
    hf::Group raw_data_1 = createNeXusGroup(out_file, "raw_data_1", "NXentry");
    hf::Group instrument = createNeXusGroup(raw_data_1, "instrument", "NXinstrument");
    // using decltype() didn't work hence _t typedefs
    hf::DataSet dset_trigger_time = instrument.createDataSet("trigger_time", dataspace, hf::create_datatype<trigger_time_t>(), props);
    dset_trigger_time.createAttribute<std::string>("units", "ns");
    hf::DataSet dset_frame_time = instrument.createDataSet("frame_time", dataspace, hf::create_datatype<frame_time_t>(), props);
    hf::DataSet dset_frame_number = instrument.createDataSet("frame_number", dataspace, hf::create_datatype<frame_number_t>(), props);
    hf::DataSet dset_extras = instrument.createDataSet("extras", dataspace, hf::create_datatype<extras_t>(), props);
    hf::DataSet dset_energy_raw = instrument.createDataSet("energy_raw", dataspace, hf::create_datatype<energy_t>(), props);
    hf::DataSet dset_energy = instrument.createDataSet("energy", dataspace, hf::create_datatype<energy_t>(), props);
    
    FILE* f = NULL;
    while( (f = _fsopen(input_filename, "rb", _SH_DENYNO)) == NULL )
    {
        epicsThreadSleep(1.0);
    }
    frame_number_t frame = 0;
    int64_t last_pos, current_pos, new_bytes, nevents = 0;
    size_t nevents_total = 0;
    uint64_t frame_start = 0;
    while(true)
    {
        if ( (last_pos = _ftelli64(f)) == -1 )
        {
            std::cerr << "ftell last error" << std::endl;
            return 0;
        }
        if (_fseeki64(f, 0, SEEK_END) != 0)
        {
            std::cerr << "fseek forward error" << std::endl;
            return 0;
        }   
        if ( (current_pos = _ftelli64(f)) == -1)
        {
            std::cerr << "ftell curr error" << std::endl;
            return 0;
        }
        if (_fseeki64(f, last_pos, SEEK_SET) != 0)
        {
            std::cerr << "fseek back error" << std::endl;
            return 0;
        }   
        new_bytes = current_pos - last_pos;
        nevents = new_bytes / EVENT_SIZE;
        if (nevents > NEVENTS_READ)
        {
            nevents = NEVENTS_READ;
        }
        if (nevents == 0)
        {
            break;
        }   
        size_t n = 0;
        for(int j=0; j<nevents; ++j)
        {
            if (fread(trigger_time + n, sizeof(trigger_time[0]), 1, f) != 1)
            {
                std::cerr << "fread time error" << std::endl;
                return 0;
            }
            trigger_time[n] /= 1000;  // convert from ps to ns
            if (fread(energy_raw + n, sizeof(energy_raw[0]), 1, f) != 1)
            {
                std::cerr << "fread energy error" << std::endl;
                return 0;
            }
            if (fread(extras + n, sizeof(extras[0]), 1, f) != 1)
            {
                std::cerr << "fread extras error" << std::endl;
                return 0;
            }
            if (extras[n] == 0x8 && energy_raw[n] == 0)
            {
                ++frame;
                frame_start = trigger_time[n];
            }
            frame_time[n] = trigger_time[n] - frame_start;
            frame_number[n] = frame;
            if ( energy_raw[n] > 0 && energy_raw[n] != 32767 && !(extras[n] & 0x8) )
            {
                energy[n] = energy_a * energy_raw[n] + energy_b;
                ++n; // real event
            }
        }
        appendData(n, nevents_total, dset_trigger_time, trigger_time);
        appendData(n, nevents_total, dset_frame_time, frame_time);
        appendData(n, nevents_total, dset_frame_number, frame_number);
        appendData(n, nevents_total, dset_extras, extras);
        appendData(n, nevents_total, dset_energy_raw, energy_raw);
        appendData(n, nevents_total, dset_energy, energy);
        nevents_total += n;
    }
    fclose(f);   
    return 0;
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
        s += "Event outside SCA interval, ";
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
