#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstdint> // Required for uint32_t and uint64_t

// Helper to generate standard VCD ASCII IDs (!, ", #, ..., !a, !b)
std::string getVcdId(uint32_t id) {
    std::string res = "";
    do {
        res += static_cast<char>('!' + (id % 94));
        id /= 94;
    } while (id > 0);
    return res;
}

int main(int argc, char* argv[]) {
    std::string filename = (argc > 1) ? argv[1] : "heavy_sim.vcd";
    std::ofstream file(filename, std::ios::binary); // binary mode for faster writing

    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing.\n";
        return 1;
    }

    std::cout << "Starting generation of 1GB VCD file: " << filename << "\n";
    std::cout << "This may take 10-20 seconds...\n";

    // --- 1. Write Header ---
    file << "$date Today $end\n";
    file << "$version Synthetic VCD Generator $end\n";
    file << "$timescale 1ns $end\n";
    file << "$scope module TOP $end\n";

    uint32_t num_buses = 50;
    uint32_t num_wires = 5000;
    uint32_t current_id = 0;

    // Create Clock (ID 0)
    file << "$var wire 1 " << getVcdId(current_id++) << " clk $end\n";
    
    // Create Target Wire (ID 1) - This is what our reader will search for!
    file << "$var wire 1 " << getVcdId(current_id++) << " target_signal $end\n";

    // Create Buses
    for (uint32_t i = 0; i < num_buses; ++i) {
        file << "$var wire 32 " << getVcdId(current_id++) << " bus_" << i << " $end\n";
    }

    // Create Wires
    for (uint32_t i = 0; i < num_wires; ++i) {
        file << "$var wire 1 " << getVcdId(current_id++) << " wire_" << i << " $end\n";
    }

    file << "$upscope $end\n";
    file << "$enddefinitions $end\n";

    // --- 2. Simulation Loop ---
    uint64_t sim_time = 0; // Renamed to avoid clashing with C++ time() function
    bool clk = false;
    bool target_wire_state = false;
    
    // 1 Gigabyte target (1024 * 1024 * 1024 bytes)
    const std::streampos TARGET_SIZE = 1073741824ULL; 

    // Pre-allocate a buffer to hold the states of the 5000 wires so we know when they flip
    std::vector<bool> wire_states(num_wires, false);

    while (true) {
        file << "#" << sim_time << "\n";

        // Toggle Clock (ID 0)
        clk = !clk;
        file << (clk ? "1" : "0") << getVcdId(0) << "\n";

        // Toggle Target Signal (ID 1) every 50,000 ns
        if (sim_time % 50000 == 0) {
            target_wire_state = !target_wire_state;
            file << (target_wire_state ? "1" : "0") << getVcdId(1) << "\n";
        }

        // Output some bus changes (simulate counters)
        for (uint32_t i = 0; i < num_buses; i += 5) {
            file << "b10101010 " << getVcdId(2 + i) << "\n"; // Dummy binary value
        }

        // Randomly toggle about 50 wires per time step to simulate realistic RTL noise
        for (int i = 0; i < 50; ++i) {
            uint32_t w_idx = rand() % num_wires;
            wire_states[w_idx] = !wire_states[w_idx];
            file << (wire_states[w_idx] ? "1" : "0") << getVcdId(2 + num_buses + w_idx) << "\n";
        }

        sim_time += 5; // Advance time by 5ns

        // Every 10,000 steps, check if we hit 1GB on the hard drive
        if (sim_time % 50000 == 0) {
            if (file.tellp() >= TARGET_SIZE) {
                break;
            }
        }
    }

    file.close();
    std::cout << "Done! Generated " << sim_time << " nanoseconds of simulation data.\n";
    return 0;
}