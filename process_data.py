import h5py
import numpy as np

f = h5py.File('a.h5', 'r')

#event_time_zero[num frames] - frame times sicne start (s)
#event index[num_frames] - index into event list of start of events for that frame
#event_time_offset[num_events] - fame relative time for each event (ns)
#event_energy_raw[num_events] - hexagon enegry value (integer 0 to 32767)
#event_energy[num_events] - event_energy_raw scaled by a * x + b
#event_flags - event hexagin flags

event_energy = f['/raw_data_1/detector_1_events/event_energy']
event_time_zero = f['/raw_data_1/detector_1_events/event_time_zero']
event_index = f['/raw_data_1/detector_1_events/event_index']
event_time_offset = f['/raw_data_1/detector_1_events/event_time_offset']
num_events = event_energy.size
num_frames = event_time_zero.size
print(f"Number of events = {num_events}")
print(f"Number of frames = {num_frames}")

for frame in range(0, num_frames):
    # event_index[frame] points to the offset in event_energy where events
    # for this frame are located. we have all the events up to just before
    # where event_index[frame+1] points to. If there are no events in a frame then
    # event_index[frame] and event_index[frame+1] have the same value
    print(f"Events for frame {frame} at {event_time_zero[frame]} seconds from start")
    if frame != num_frames - 1:
        next_frame_index = event_index[frame + 1]
    else:
        next_frame_index = num_events
    num_frame_events = next_frame_index - event_index[frame]
    print(f"This frame has {num_frame_events} events")
    start = int(event_index[frame])
    for e in range(0, num_frame_events):
        print(f"Time {event_time_offset[start + e]}ns, energy {event_energy[start + e]}")
