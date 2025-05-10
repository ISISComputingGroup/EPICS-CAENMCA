import os
import struct
import numpy as np

infile = "CHARM_A_19_ch000.dat"
outfile = "CHARM_A_19_ch000.nxs"
EVENT_SIZE = 14

event = struct.Struct("<QhI")
if event.size != EVENT_SIZE:
    print("event size error")
    sys.exit(1)

file_size = os.path.getsize(infile)
if file_size % EVENT_SIZE != 0:
    print("file size error")
    sys.exit(1)

num_events_raw = file_size // EVENT_SIZE

energy_array = np.empty(num_events_raw, dtype=int) # int16 in file
extras_array = np.empty(num_events_raw, dtype=np.uint32)
trigger_time_array = np.empty(num_events_raw, dtype=np.uint64)

print(f"Found {num_events_raw} raw events")

n = 0
with open(infile, "rb") as f:
    while(bytes := f.read(EVENT_SIZE * 1000)):
        for (trigger_time, energy, extras) in event.iter_unpack(bytes):
            trigger_time_array[n] = trigger_time
            energy_array[n] = energy
            extras_array[n] = extras
            n += 1

if n != num_events_raw:
    print("num raw events error")


#    typedef uint64_t trigger_time_t, frame_time_t;
#    typedef uint32_t extras_t, frame_number_t;
#    typedef int16_t energy_t;
#    trigger_time_t trigger_time[NEVENTS_READ];
#    frame_time_t frame_time[NEVENTS_READ];
#    energy_t energy[NEVENTS_READ];
#    extras_t extras[NEVENTS_READ];
#    frame_number_t frame_number[NEVENTS_READ];


#           if (extras[n] == 0x8 && energy[n] == 0)
#            {
#                ++frame;
#                frame_start = trigger_time[n];
#            }
#            frame_time[n] = trigger_time[n] - frame_start;
#            frame_number[n] = frame;
#            if ( energy[n] > 0 && energy[n] != 32767 && !(extras[n] & 0x8) )
#            {
#                ++n; // real event
#            }
#        }
#        appendData(n, nevents_total, dset_trigger_time, trigger_time);
#        appendData(n, nevents_total, dset_frame_time, frame_time);
#        appendData(n, nevents_total, dset_frame_number, frame_number);
#        appendData(n, nevents_total, dset_extras, extras);
#        appendData(n, nevents_total, dset_energy, energy);
#        nevents_total += n;
