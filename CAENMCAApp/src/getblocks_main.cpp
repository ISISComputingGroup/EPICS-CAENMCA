#include <vector>
#include <map>
#include <string>

#include <epicsExport.h>

#include "getblocks.h"

int main(int argc, char* argv[])
{
    seblock_map_t blocks;
    getBlocks(0, blocks);
    return 0;    
}
