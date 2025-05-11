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
    const int NEVENTS_READ = 100000;
    typedef uint64_t trigger_time_t, frame_time_t;
    typedef uint32_t extras_t;
    typedef int16_t energy_t;
    trigger_time_t trigger_time;
    frame_time_t reference_time;
    bool reference_time_set = false;
    energy_t energy_raw;
    extras_t extras;

    std::vector<int32_t> event_frame_number(NEVENTS_READ);
    std::vector<uint32_t> event_id(NEVENTS_READ);
    std::vector<uint64_t> event_index(NEVENTS_READ);
    std::vector<double> event_time_offset(NEVENTS_READ);
    std::vector<double> event_time_zero(NEVENTS_READ);
    std::vector<int32_t> event_energy_raw(NEVENTS_READ);
    std::vector<double> event_energy(NEVENTS_READ);
    std::vector<uint32_t> event_flags(NEVENTS_READ);
    
    int fake_events = 0;
    const size_t EVENT_SIZE = 14;

    if ( (sizeof(trigger_time) + sizeof(energy_raw) + sizeof(extras)) != EVENT_SIZE )
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
    hf::Group instrument = createNeXusGroup(raw_data_1, "detector_1_events", "NXdata");
    // using decltype() didn't work hence _t typedefs
    
    hf::DataSet dset_event_time_zero = instrument.createDataSet("event_time_zero", dataspace, hf::create_datatype<double>(), props);
    dset_event_time_zero.createAttribute<std::string>("units", "s");
    hf::DataSet dset_event_time_offset = instrument.createDataSet("event_time_offset", dataspace, hf::create_datatype<double>(), props);
    dset_event_time_offset.createAttribute<std::string>("units", "ns");
    hf::DataSet dset_event_frame_number = instrument.createDataSet("event_frame_number", dataspace, hf::create_datatype<int32_t>(), props);
    hf::DataSet dset_event_id = instrument.createDataSet("event_id", dataspace, hf::create_datatype<uint32_t>(), props);
    hf::DataSet dset_event_index = instrument.createDataSet("event_index", dataspace, hf::create_datatype<uint64_t>(), props);
    hf::DataSet dset_event_energy = instrument.createDataSet("event_energy", dataspace, hf::create_datatype<double>(), props);
    hf::DataSet dset_event_energy_raw= instrument.createDataSet("event_energy_raw", dataspace, hf::create_datatype<int32_t>(), props);
    hf::DataSet dset_event_flags = instrument.createDataSet("event_flags", dataspace, hf::create_datatype<uint32_t>(), props);
    
    // wait for file access
    FILE* f = NULL;
    while( (f = _fsopen(input_filename, "rb", _SH_DENYNO)) == NULL )
    {
        epicsThreadSleep(1.0);
    }

    int frame = -1;
    int64_t last_pos, current_pos, new_bytes, nevents = 0;
    size_t nevents_total = 0, nevents_raw_total = 0, nframes_total = 0;
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
        size_t n = 0, nf = 0;
        for(int j=0; j<nevents; ++j)
        {            
            if (fread(&trigger_time, sizeof(trigger_time), 1, f) != 1)
            {
                std::cerr << "fread time error" << std::endl;
                return 0;
            }
            if (fread(&energy_raw, sizeof(energy_raw), 1, f) != 1)
            {
                std::cerr << "fread energy error" << std::endl;
                return 0;
            }
            if (fread(&extras, sizeof(extras), 1, f) != 1)
            {
                std::cerr << "fread extras error" << std::endl;
                return 0;
            }
            // reference to time of first event
            if (!reference_time_set) {
                reference_time = trigger_time;
                reference_time_set = true;
            }
            if (extras == 0x8 && energy_raw == 0)
            {
                ++frame;
                frame_start = trigger_time;
                event_frame_number[nf] = frame;
                event_time_zero[nf] = (trigger_time - reference_time) / 1e12; // ps to second
                event_index[nf] = n + nevents_total;
                if (nframes_total == 0 && nf == 0) {
                    std::cerr << "First frame sync after " << event_time_zero[nf] << " seconds into run" << std::endl;
                }
                ++nf;
            }
            if (frame < 0) {
                continue; // skip events until see first frame
            }
            if ( energy_raw > 0 && energy_raw != 32767 && !(extras & 0x8) )
            {
                event_energy_raw[n] = energy_raw;
                event_energy[n] = energy_a * energy_raw + energy_b;
                event_id[n] = 0;
                event_time_offset[n] = (trigger_time - frame_start) / 1e3; // ps to ns
                event_flags[n] = extras;
                if (nevents_total == 0 && n == 0) {
                    std::cerr << "First detector event after " << (trigger_time - reference_time) / 1e12 << " seconds into run" << std::endl;
                }
                ++n; // real event
            }
        }
        appendData(nf, nframes_total, dset_event_frame_number, event_frame_number.data());
        appendData(nf, nframes_total, dset_event_index, event_index.data());
        appendData(nf, nframes_total, dset_event_time_zero, event_time_zero.data());
        appendData(n, nevents_total, dset_event_time_offset, event_time_offset.data());
        appendData(n, nevents_total, dset_event_id, event_id.data());
        appendData(n, nevents_total, dset_event_energy_raw, event_energy_raw.data());
        appendData(n, nevents_total, dset_event_energy, event_energy.data());
        appendData(n, nevents_total, dset_event_flags, event_flags.data());
        nevents_total += n;
        nframes_total += nf;
        nevents_raw_total += nevents;
    }
    fclose(f);
    if (nframes_total != frame + 1) {
        std::cerr << "frame total error" << std::endl;
    }
    std::cout << "Processed " << nframes_total << " frames with "<< nevents_total << " detector events and " << nevents_raw_total - nevents_total - nframes_total << " other events" << std::endl; 
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
