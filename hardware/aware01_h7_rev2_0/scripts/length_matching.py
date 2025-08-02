import re
import pcbnew
import os

# Load the PCB file
board = pcbnew.GetBoard()

project_dir = os.path.dirname(board.GetFileName())

# Define regex patterns to match U2 and FMC nets
u2_pattern = re.compile(r"Net-\(U2-A(\d+)\)")
fmc_pattern = re.compile(r"/FMC_A(\d+)")

# Create a dictionary for the U2 nets and FMC nets
u2_nets = {}
fmc_nets = {}

# Populate the dictionaries with net names and codes
for track in pcb.GetTracks():
    # Match U2 nets
    u2_match = u2_pattern.search(track.GetNetname())
    if u2_match:
        u2_nets[u2_match.group(1)] = track.GetNetcode()
    
    # Match FMC nets
    fmc_match = fmc_pattern.search(track.GetNetname())
    if fmc_match:
        fmc_nets[fmc_match.group(1)] = track.GetNetcode()

# Now, generate pairs based on the matching suffix (e.g., A0 with A0, A1 with A1)
for suffix in u2_nets:
    if suffix in fmc_nets:
        u2_net_code = u2_nets[suffix]
        fmc_net_code = fmc_nets[suffix]
        
        # Sum trace lengths for the corresponding U2 and FMC nets
        total_length = 0
        for track in pcb.GetTracks():
            if track.GetNetcode() == u2_net_code or track.GetNetcode() == fmc_net_code:
                total_length += track.GetLength()

        # Print the result for this pair
        print(f"Combined Net: Net-(U2-A{suffix}) and /FMC_A{suffix}, Total Trace Length: {total_length} mm")
