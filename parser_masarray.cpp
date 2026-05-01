#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <cstdint>

// --- 1. Data Structures & Enums ---

enum class LogicState : uint8_t {
    VAL_0 = 0b00,
    VAL_1 = 0b01,
    VAL_X = 0b10,
    VAL_Z = 0b11
};

struct SignalInfo {
    uint32_t int_id;
    std::string name;
    uint32_t width;
    std::string vcd_id; 
};

// UPDATED for Master Time Dictionary
struct ColumnBuffer {
    uint32_t signal_id;
    uint32_t width_bits;
    std::vector<uint16_t> time_indices; // 2-byte pointers to the Master Array
    std::vector<uint8_t> data;         
};

// --- 2. The Chunk Builder (Master Array Version) ---

class ChunkBuilder {
private:
    uint64_t chunk_start_time;
    uint64_t max_time_per_chunk;
    std::unordered_map<uint32_t, ColumnBuffer> columns;
    
    // --- MASTER TIME ARRAY ---
    std::vector<uint32_t> master_time_deltas;

    uint16_t getMasterTimeIndex(uint64_t current_time) {
        uint32_t delta = current_time - chunk_start_time;
        if (master_time_deltas.empty() || master_time_deltas.back() != delta) {
            master_time_deltas.push_back(delta);
        }
        return static_cast<uint16_t>(master_time_deltas.size() - 1);
    }

public:
    ChunkBuilder(uint64_t chunk_duration) 
        : chunk_start_time(0), max_time_per_chunk(chunk_duration) {}

    void initSignal(uint32_t signal_id, uint32_t width) {
        if (columns.find(signal_id) == columns.end()) {
            columns[signal_id] = {signal_id, width, {}, {}};
        }
    }

    void addWireChange(uint32_t signal_id, uint64_t current_time, LogicState state) {
        auto& col = columns[signal_id];
        col.time_indices.push_back(getMasterTimeIndex(current_time));

        size_t num_changes = col.time_indices.size() - 1;
        uint8_t bit_offset = (num_changes % 4) * 2; 

        if (bit_offset == 0) {
            col.data.push_back(static_cast<uint8_t>(state)); 
        } else {
            col.data.back() |= (static_cast<uint8_t>(state) << bit_offset); 
        }
    }

    void addBusChange(uint32_t signal_id, uint64_t current_time, const std::vector<uint8_t>& raw_bytes) {
        auto& col = columns[signal_id];
        col.time_indices.push_back(getMasterTimeIndex(current_time));
        col.data.insert(col.data.end(), raw_bytes.begin(), raw_bytes.end());
    }

    bool requiresFlush(uint64_t current_time) {
        return (current_time - chunk_start_time) >= max_time_per_chunk;
    }

    void flushToDisk(std::ofstream& outfile, uint64_t next_start_time) {
        if (!outfile.is_open() || master_time_deltas.empty()) {
            chunk_start_time = next_start_time;
            return; 
        }

        uint32_t active_columns = 0;
        for (const auto& pair : columns) {
            if (!pair.second.time_indices.empty()) active_columns++;
        }

        if (active_columns == 0) {
            chunk_start_time = next_start_time;
            return;
        }

        std::cout << "Flushing chunk spanning " << chunk_start_time << " to " 
                  << (chunk_start_time + max_time_per_chunk) << "...\n";

        uint64_t chunk_end_time = chunk_start_time + max_time_per_chunk;
        outfile.write(reinterpret_cast<const char*>(&chunk_start_time), sizeof(chunk_start_time));
        outfile.write(reinterpret_cast<const char*>(&chunk_end_time), sizeof(chunk_end_time));
        outfile.write(reinterpret_cast<const char*>(&active_columns), sizeof(active_columns));

        // --- WRITE THE MASTER TIME ARRAY ONCE ---
        uint32_t num_master_times = master_time_deltas.size();
        outfile.write(reinterpret_cast<const char*>(&num_master_times), sizeof(num_master_times));
        outfile.write(reinterpret_cast<const char*>(master_time_deltas.data()), num_master_times * sizeof(uint32_t));

        for (auto& pair : columns) {
            auto& col = pair.second;
            if (col.time_indices.empty()) continue;

            outfile.write(reinterpret_cast<const char*>(&col.signal_id), sizeof(col.signal_id));
            
            // Write Indices (2 bytes each instead of 4)
            uint32_t num_indices = col.time_indices.size();
            outfile.write(reinterpret_cast<const char*>(&num_indices), sizeof(num_indices));
            outfile.write(reinterpret_cast<const char*>(col.time_indices.data()), num_indices * sizeof(uint16_t));

            uint32_t num_bytes = col.data.size();
            outfile.write(reinterpret_cast<const char*>(&num_bytes), sizeof(num_bytes));
            outfile.write(reinterpret_cast<const char*>(col.data.data()), num_bytes);

            col.time_indices.clear();
            col.data.clear();
        }

        master_time_deltas.clear();
        chunk_start_time = next_start_time;
    }
};

// --- 3. The VCD Parser ---

class VcdConverter {
private:
    std::unordered_map<std::string, uint32_t> vcd_id_to_int;
    std::unordered_map<uint32_t, SignalInfo> signal_metadata;
    uint32_t next_int_id = 0;
    uint64_t current_time = 0;
    
    ChunkBuilder chunk_builder;

    std::string_view trim(std::string_view sv) {
        size_t start = sv.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos) return ""; 
        size_t end = sv.find_last_not_of(" \t\r\n");
        return sv.substr(start, end - start + 1);
    }

    std::vector<std::string_view> split(std::string_view str, char delim = ' ') {
        std::vector<std::string_view> result;
        size_t start = 0, end = str.find(delim);
        while (end != std::string_view::npos) {
            if (end != start) result.push_back(str.substr(start, end - start));
            start = end + 1;
            end = str.find(delim, start);
        }
        if (start < str.length()) result.push_back(str.substr(start));
        return result;
    }

    LogicState charToLogicState(char c) {
        switch (c) {
            case '1': return LogicState::VAL_1;
            case 'x': case 'X': return LogicState::VAL_X;
            case 'z': case 'Z': return LogicState::VAL_Z;
            default: return LogicState::VAL_0;
        }
    }

    std::vector<uint8_t> binaryStringToBytes(std::string_view bin_str) {
        std::vector<uint8_t> bytes((bin_str.length() + 7) / 8, 0);
        for (size_t i = 0; i < bin_str.length(); ++i) {
            if (bin_str[bin_str.length() - 1 - i] == '1') {
                bytes[i / 8] |= (1 << (i % 8));
            }
        }
        return bytes;
    }

public:
    VcdConverter(uint64_t chunk_duration) : chunk_builder(chunk_duration) {}

    void processVCD(const std::string& input_filepath, const std::string& output_filepath) {
        std::ifstream infile(input_filepath);
        if (!infile.is_open()) {
            std::cerr << "Failed to open input VCD file: " << input_filepath << std::endl;
            return;
        }

        std::ofstream outfile(output_filepath, std::ios::binary);
        if (!outfile.is_open()) {
            std::cerr << "Failed to create output binary file: " << output_filepath << std::endl;
            return;
        }

        std::string line;
        bool in_header = true;

        while (std::getline(infile, line)) {
            std::string_view sv = trim(line);
            if (sv.empty()) continue;

            if (in_header) {
                if (sv.starts_with("$enddefinitions")) {
                    in_header = false;
                    outfile.write("WAVE", 4); 
                    
                    uint32_t num_sigs = signal_metadata.size();
                    outfile.write(reinterpret_cast<const char*>(&num_sigs), sizeof(num_sigs));
                    
                    for (const auto& [id, sig] : signal_metadata) {
                        outfile.write(reinterpret_cast<const char*>(&sig.int_id), sizeof(sig.int_id));
                        outfile.write(reinterpret_cast<const char*>(&sig.width), sizeof(sig.width));
                    }
                    std::cout << "Parsed Header: " << num_sigs << " signals found. Dictionary written.\n";

                } else if (sv.starts_with("$var")) {
                    parseVarDef(sv);
                }
            } else {
                parseDumpLine(sv, outfile);
            }
        }
        
        chunk_builder.flushToDisk(outfile, current_time);
        
        infile.close();
        outfile.close();
        std::cout << "Compilation complete. Custom waveform saved to " << output_filepath << "\n";
    }

private:
    void parseVarDef(std::string_view line) {
        auto tokens = split(line);
        if (tokens.size() >= 5) {
            SignalInfo sig;
            sig.int_id = next_int_id++;
            sig.width = std::stoi(std::string(tokens[2]));
            sig.vcd_id = std::string(tokens[3]);
            sig.name = std::string(tokens[4]); 
            
            vcd_id_to_int[sig.vcd_id] = sig.int_id;
            signal_metadata[sig.int_id] = sig;
            chunk_builder.initSignal(sig.int_id, sig.width);
        }
    }

    void parseDumpLine(std::string_view line, std::ofstream& outfile) {
        if (line[0] == '#') {
            current_time = std::stoull(std::string(line.substr(1)));
            
            if (chunk_builder.requiresFlush(current_time)) {
                chunk_builder.flushToDisk(outfile, current_time);
            }
        } 
        else if (line[0] == 'b' || line[0] == 'B') {
            size_t space_idx = line.find(' ');
            if (space_idx != std::string_view::npos) {
                std::string_view value = line.substr(1, space_idx - 1);
                std::string_view vcd_id = line.substr(space_idx + 1);
                
                if (vcd_id_to_int.find(std::string(vcd_id)) != vcd_id_to_int.end()) {
                    uint32_t int_id = vcd_id_to_int[std::string(vcd_id)];
                    std::vector<uint8_t> bytes = binaryStringToBytes(value);
                    chunk_builder.addBusChange(int_id, current_time, bytes);
                }
            }
        } 
        else {
            LogicState state = charToLogicState(line[0]);
            std::string_view vcd_id = line.substr(1);
            
            if (vcd_id_to_int.find(std::string(vcd_id)) != vcd_id_to_int.end()) {
                uint32_t int_id = vcd_id_to_int[std::string(vcd_id)];
                chunk_builder.addWireChange(int_id, current_time, state);
            }
        }
    }
};

// --- 4. Main Entry Point ---

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Error: Missing arguments.\n";
        std::cerr << "Usage: " << argv[0] << " <input.vcd> <output.wave>\n";
        return 1; 
    }

    std::string input_filepath = argv[1];
    std::string output_filepath = argv[2];

    VcdConverter converter(10000000); 
    
    std::cout << "Starting Waveform Compiler (Master Array Version)...\n";
    converter.processVCD(input_filepath, output_filepath);
    
    return 0;
}