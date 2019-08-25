
## Load record instances

epicsEnvSet EPICS_CA_MAX_ARRAY_BYTES 10000000

CAENMCAConfigure("L1", "eth://130.246.55.0")
CAENMCAConfigure("L2", "eth://130.246.52.130")

dbLoadRecords("../../db/CAENMCA.db","P=$(MYPVPREFIX),Q=HEX0:,PORT=L1")
dbLoadRecords("../../db/CAENMCAChan.db","P=$(MYPVPREFIX),Q=HEX0:,PORT=L1,CHAN=0")
dbLoadRecords("../../db/CAENMCAChan.db","P=$(MYPVPREFIX),Q=HEX0:,PORT=L1,CHAN=1")
dbLoadRecords("../../db/CAENMCAHVChan.db","P=$(MYPVPREFIX),Q=HEX0:,PORT=L1,CHAN=0")
dbLoadRecords("../../db/CAENMCAHVChan.db","P=$(MYPVPREFIX),Q=HEX0:,PORT=L1,CHAN=1")

dbLoadRecords("../../db/CAENMCA.db","P=$(MYPVPREFIX),Q=HEX1:,PORT=L2")
dbLoadRecords("../../db/CAENMCAChan.db","P=$(MYPVPREFIX),Q=HEX1:,PORT=L2,CHAN=0")
dbLoadRecords("../../db/CAENMCAChan.db","P=$(MYPVPREFIX),Q=HEX1:,PORT=L2,CHAN=1")
dbLoadRecords("../../db/CAENMCAHVChan.db","P=$(MYPVPREFIX),Q=HEX1:,PORT=L2,CHAN=0")
dbLoadRecords("../../db/CAENMCAHVChan.db","P=$(MYPVPREFIX),Q=HEX1:,PORT=L2,CHAN=1")

cd "${TOP}/iocBoot/${IOC}"
iocInit

