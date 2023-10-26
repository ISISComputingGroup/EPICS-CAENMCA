#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cstdio>
#include <cstring>
#include <epicsThread.h>

#ifndef _WIN32
#define _fsopen(a,b,c) fopen(a,b)
#define _ftelli64 ftell
#define _fseeki64 fseek
#endif /* ndef _WIN32 */


static std::string describeFlags(unsigned flags);

int main(int argc, char* argv[])
{
    const char* input_filename = argv[1];
    const char* output_filename = (argc > 2 ? argv[2] : "");
    bool exit_when_done = (argc > 3 && atoi(argv[3]) != 0);
    int mode = (argc > 4 ? atoi(argv[4]) : 0);
    uint64_t trigger_time, frame_time = 0;
    int16_t energy;
    uint32_t extras;
    int fake_events = 0;
    const size_t EVENT_SIZE = 14;
    if ( (sizeof(trigger_time) + sizeof(energy) + sizeof(extras)) != EVENT_SIZE )
    {
        std::cerr << "size error" << std::endl;
        return 0;
    }
    FILE *f, *out_f;
    if (strlen(output_filename) > 0) {
        out_f = _fsopen(output_filename, "wb", _SH_DENYNO); // we use "b" as cae files are LF not CRLF
    } else {
        out_f = stdout;
    }
    int64_t frame = 0, last_pos, current_pos, new_bytes, nevents = 0;
    if (mode == 2) {
        std::fstream fs(input_filename);
        std::string line;
        std::getline(fs, line, '\n'); // skip header
        while(fs.good()) {
            std::getline(fs, line, '\n');
            if (sscanf(line.c_str(), "%llu %hd %x", &trigger_time, &energy, &extras) == 3) {
                fwrite(&trigger_time, sizeof(trigger_time), 1, out_f);
                fwrite(&energy, sizeof(energy), 1, out_f);
                fwrite(&extras, sizeof(extras), 1, out_f);
                ++nevents;                
            }
        }
        fclose(out_f);
        std::cout << "Processed " << nevents << " events" << std::endl;
        return 0;
    } else if (mode == 1) {
        fprintf(out_f, "TIMETAG\t\tENERGY\tFLAGS\t\n");
    } else {
        fprintf(out_f, "time_abs\ttime_rel_to_trigger\tENERGY\tEXTRAS\tDESC\r\n");
    }
    while( (f = _fsopen(input_filename, "rb", _SH_DENYNO)) == NULL )
    {
        epicsThreadSleep(1.0);
    }
    do
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
        if (nevents == 0)
        {
            fflush(out_f);
            epicsThreadSleep(1.0);
            continue;
        }
        for(int i=0; i<nevents; ++i)
        {
            if (fread(&trigger_time, sizeof(trigger_time), 1, f) != 1)
            {
                std::cerr << "fread time error" << std::endl;
                return 0;
            }
            if (fread(&energy, sizeof(energy), 1, f) != 1)
            {
                std::cerr << "fread energy error" << std::endl;
                return 0;
            }
            if (fread(&extras, sizeof(extras), 1, f) != 1)
            {
                std::cerr << "fread extras error" << std::endl;
                return 0;
            }
            if (extras == 0x8 && energy == 0)
            {
                ++frame;
                frame_time = trigger_time;
            }
            if (mode == 1) {
                fprintf(out_f, "%llu\t%d\t0x%08x\t\n", trigger_time, energy, extras);
            } else {
                //if ( energy > 0 && (!(extras & 0x8)) )
                //{
                    fprintf(out_f, "%llu\t%llu\t%d\t0x%08x\t%s\r\n", trigger_time, trigger_time - frame_time,
                                    energy, extras, describeFlags(extras).c_str());
                //}
            }
        }
    } while(!exit_when_done);
    fclose(f);
    fflush(out_f);
    fclose(out_f);
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
