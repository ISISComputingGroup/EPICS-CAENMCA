#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cstdio>
#include <cstring>
#include <epicsThread.h>

#include <hdf5_hl.h>

#ifndef _WIN32
#define _fsopen(a,b,c) fopen(a,b)
#define _ftelli64 ftell
#define _fseeki64 fseek
#endif /* ndef _WIN32 */

static std::string describeFlags(unsigned flags);

// add n items to dataset that already contains n_total
void appendData(uint64_t n, uint64_t n_total, hid_t dset, void* data, hid_t data_type)
{
    if (n == 0)
    {
        return;
    }
    hsize_t extdims[1] = { n_total + n }; // extend dataset
    hsize_t dims[1] =  { n };
    herr_t status = H5Dset_extent(dset, extdims);
    hid_t dataspace = H5Screate_simple(1, dims, NULL);
    hid_t filespace = H5Dget_space(dset);
    hsize_t start[1] = { n_total }, count[1] = { n };
    H5Sselect_hyperslab(filespace, H5S_SELECT_SET, start, NULL, count, NULL);
    status = H5Dwrite(dset, data_type, dataspace, filespace,
                  H5P_DEFAULT, data);
    H5Sclose(dataspace);
    H5Sclose(filespace);
    H5Dflush(dset);
}

int main(int argc, char* argv[])
{
    const char* input_filename = argv[1];
    const char* output_filename = argv[2];
    bool exit_when_done = (argc > 3 && atoi(argv[3]) != 0);
    const int NEVENTS_READ = 1000;
    uint64_t trigger_time[NEVENTS_READ], frame_time[NEVENTS_READ];
    int16_t energy[NEVENTS_READ];
    uint32_t extras[NEVENTS_READ], frame_number[NEVENTS_READ];
    int fake_events = 0;
    const size_t EVENT_SIZE = 14;
    hsize_t maxdims[1] = { H5S_UNLIMITED };
    hsize_t dims[1] = { 1 };
    hsize_t chunk[1] = { 1000 };
    herr_t  status;
    hid_t dataspace;
    if ( (sizeof(trigger_time[0]) + sizeof(energy[0]) + sizeof(extras[0])) != EVENT_SIZE )
    {
        std::cerr << "size error" << std::endl;
        return 0;
    }
    hid_t fapl = H5Pcreate (H5P_FILE_ACCESS);
    status = H5Pset_libver_bounds (fapl, H5F_LIBVER_LATEST, H5F_LIBVER_LATEST);
    hid_t out_file = H5Fcreate(output_filename, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);
    status = H5Pclose(fapl);    
    dataspace = H5Screate_simple(1, dims, maxdims);
    hid_t dcpl   = H5Pcreate(H5P_DATASET_CREATE);
    H5Pset_chunk(dcpl, 1, chunk);
    hid_t dset_trigger_time = H5Dcreate(out_file, "trigger_time", H5T_NATIVE_UINT64,
                                        dataspace, H5P_DEFAULT, dcpl, H5P_DEFAULT);
    hid_t dset_frame_time = H5Dcreate(out_file, "frame_time", H5T_NATIVE_UINT64,
                                        dataspace, H5P_DEFAULT, dcpl, H5P_DEFAULT);
    hid_t dset_frame_number = H5Dcreate(out_file, "frame_number", H5T_NATIVE_UINT32,
                                        dataspace, H5P_DEFAULT, dcpl, H5P_DEFAULT);
    hid_t dset_extras = H5Dcreate(out_file, "extras", H5T_NATIVE_UINT32,
                                        dataspace, H5P_DEFAULT, dcpl, H5P_DEFAULT);
    hid_t dset_energy = H5Dcreate(out_file, "energy", H5T_NATIVE_INT16,
                                        dataspace, H5P_DEFAULT, dcpl, H5P_DEFAULT);
    status = H5Pclose(dcpl);    
    H5Sclose(dataspace);
    FILE* f = NULL;
    while( (f = _fsopen(input_filename, "rb", _SH_DENYNO)) == NULL )
    {
        epicsThreadSleep(1.0);
    }
    int64_t frame = 0, last_pos, current_pos, new_bytes, nevents = 0, nevents_total = 0;
    uint64_t frame_start = 0;
    status = H5Fstart_swmr_write (out_file);
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
            if (exit_when_done)
            {
                break;
            }
            else
            {
                epicsThreadSleep(1.0);
                continue;
            }
        }   
        int n = 0;
        for(int j=0; j<nevents; ++j)
        {
            if (fread(trigger_time + n, sizeof(trigger_time[0]), 1, f) != 1)
            {
                std::cerr << "fread time error" << std::endl;
                return 0;
            }
            // trigger_time[i] /= 1000;  // convert from ps to ns
            if (fread(energy + n, sizeof(energy[0]), 1, f) != 1)
            {
                std::cerr << "fread energy error" << std::endl;
                return 0;
            }
            if (fread(extras + n, sizeof(extras[0]), 1, f) != 1)
            {
                std::cerr << "fread extras error" << std::endl;
                return 0;
            }
            if (extras[n] == 0x8 && energy[n] == 0)
            {
                ++frame;
                frame_start = trigger_time[n];
            }
            frame_time[n] = trigger_time[n] - frame_start;
            frame_number[n] = frame;
            if ( energy[n] > 0 && energy[n] != 32767 && !(extras[n] & 0x8) )
            {
                ++n; // real event
            }
        }
        appendData(n, nevents_total, dset_trigger_time, trigger_time, H5T_NATIVE_UINT64);
        appendData(n, nevents_total, dset_frame_time, frame_time, H5T_NATIVE_UINT64);
        appendData(n, nevents_total, dset_frame_number, frame_number, H5T_NATIVE_UINT32);
        appendData(n, nevents_total, dset_extras, extras, H5T_NATIVE_UINT32);
        appendData(n, nevents_total, dset_energy, energy, H5T_NATIVE_INT16);
        nevents_total += n;
    }
    fclose(f);   
    status = H5Dclose(dset_trigger_time);
    status = H5Dclose(dset_frame_time);
    status = H5Dclose(dset_frame_number);
    status = H5Dclose(dset_extras);
    status = H5Dclose(dset_energy);
    H5Fclose(out_file);
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
