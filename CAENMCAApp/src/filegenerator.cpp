#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cstdio>
#include <cstring>
#include <epicsThread.h>
#include <shareLib.h>
#include <CAENMCA.h>
#include <map>
#include "CAENMCADriver.h"

static const char* getArgStr(int arg, int argc, char* argv[], const char* default_arg)
{
    return arg < argc ? argv[arg] : default_arg;    
}

static double getArgDouble(int arg, int argc, char* argv[], double default_arg)
{
    return arg < argc ? atof(argv[arg]) : default_arg;    
}

// args: output_filename file_prefix run_number { dev_name addr hex_dir hex_file a b } * 4
int main(int argc, char* argv[])
{
    const char* file_prefix = getArgStr(1, argc, argv, NULL);
    const char* run_number = getArgStr(2, argc, argv, NULL);
    
    const char* share_path = getArgStr(3, argc, argv, NULL);
    const char* file_dir;
    
    const char* list_filename;
    const char* device_name;
    double energyScaleA, energyScaleB;
    
    std::string copyDataArgs;
    
    for(int j=0; j<2; ++j) {
        for(int i=0; i<2; ++i) {
            copyDataArgs += " ";
            copyDataArgs += CAENMCADriver::makeCopyDataArgs(i, share_path, file_dir, list_filename,
                 device_name, energyScaleA, energyScaleB);
        }
    }
    std::string dataFile = CAENMCADriver::createTemplateNexusFile(file_prefix, run_number);
    CAENMCADriver::copyData(dataFile, file_prefix, run_number, copyDataArgs);
}
  