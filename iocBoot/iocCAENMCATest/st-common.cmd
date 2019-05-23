
## Load record instances
dbLoadRecords("../../db/CAENMCATest.db")

CAENMCAConfigure("L1", "eth://130.246.55.0")
cd "${TOP}/iocBoot/${IOC}"
iocInit

