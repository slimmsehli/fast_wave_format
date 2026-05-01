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

struct ColumnBuffer {
    uint32_t signal_id;
    uint32_t width_bits;
    std::vector<uint32_t> time_deltas; 
    std::vector<uint8_t> data;         
};

// --- 2. The Chunk Builder (Transposition Buffer) ---

class ChunkBuilder {
private:
    uint64_t chunk_start_time;
    uint64_t max_time_per_chunk;
    std::unordered_map<uint32_t, ColumnBuffer> columns;

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
        col.time_deltas.push_back(current_time - chunk_start_time);

        size_t num_changes = col.time_deltas.size() - 1;
        uint8_t bit_offset = (num_changes % 4) * 2; 

        if (bit_offset == 0) {
            col.data.push_back(static_cast<uint8_t>(state)); 
        } else {
            col.data.back() |= (static_cast<uint8_t>(state) << bit_offset); 
        }
    }

    void addBusChange(uint32_t signal_id, uint64_t current_time, const std::vector<uint8_t>& raw_bytes) {
        auto& col = columns[signal_id];
        col.time_deltas.push_back(current_time - chunk_start_time);
        col.data.insert(col.data.end(), raw_bytes.begin(), raw_bytes.end());
    }

    bool requiresFlush(uint64_t current_time) {
        return (current_time - chunk_start_time) >= max_time_per_chunk;
    }

    // UPDATED: Now takes the outfile stream
    // Add this helper inside your ChunkBuilder class
    void encodeVarint(std::vector<uint8_t>& buffer, uint32_t value) {
        do {
            uint8_t byte = value & 0x7F; // Get bottom 7 bits
            value >>= 7;
            if (value != 0) byte |= 0x80; // Set continuation bit if more data
            buffer.push_back(byte);
        } while (value != 0);
    }

    // Replace your flushToDisk with this:
    void flushToDisk(std::ofstream& outfile, uint64_t next_start_time) {
        if (!outfile.is_open()) return;

        uint32_t active_columns = 0;
        for (const auto& pair : columns) {
            if (!pair.second.time_deltas.empty()) active_columns++;
        }

        if (active_columns == 0) {
            chunk_start_time = next_start_time;
            return; 
        }

        uint64_t chunk_end_time = chunk_start_time + max_time_per_chunk;
        outfile.write(reinterpret_cast<const char*>(&chunk_start_time), sizeof(chunk_start_time));
        outfile.write(reinterpret_cast<const char*>(&chunk_end_time), sizeof(chunk_end_time));
        outfile.write(reinterpret_cast<const char*>(&active_columns), sizeof(active_columns));

        for (auto& pair : columns) {
            auto& col = pair.second;
            if (col.time_deltas.empty()) continue;

            outfile.write(reinterpret_cast<const char*>(&col.signal_id), sizeof(col.signal_id));
            
            // --- VARINT COMPRESSION START ---
            std::vector<uint8_t> compressed_deltas;
            for (uint32_t delta : col.time_deltas) {
                encodeVarint(compressed_deltas, delta);
            }
            
            // Write the number of original deltas, then the compressed byte size
            uint32_t num_deltas = col.time_deltas.size();
            uint32_t compressed_size = compressed_deltas.size();
            outfile.write(reinterpret_cast<const char*>(&num_deltas), sizeof(num_deltas));
            outfile.write(reinterpret_cast<const char*>(&compressed_size), sizeof(compressed_size));
            outfile.write(reinterpret_cast<const char*>(compressed_deltas.data()), compressed_size);
            // --- VARINT COMPRESSION END ---

            uint32_t num_bytes = col.data.size();
            outfile.write(reinterpret_cast<const char*>(&num_bytes), sizeof(num_bytes));
            outfile.write(reinterpret_cast<const char*>(col.data.data()), num_bytes);

            col.time_deltas.clear();
            col.data.clear();
        }
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

    // Strips leading and trailing whitespace, tabs, and hidden Windows \r characters
    std::string_view trim(std::string_view sv) {
        size_t start = sv.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos) return ""; // String is all whitespace
        
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
            // Trim whitespace and \r before doing anything else
            std::string_view sv = trim(line);
            if (sv.empty()) continue;

            if (in_header) {
                // Using .starts_with (Requires C++20)
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
                // UPDATED: Now passing outfile to parseDumpLine
                parseDumpLine(sv, outfile);
            }
        }
        
        // Final flush
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

    // UPDATED: Signature now correctly accepts the std::ofstream reference
    void parseDumpLine(std::string_view line, std::ofstream& outfile) {
        if (line[0] == '#') {
            current_time = std::stoull(std::string(line.substr(1)));
            
            if (chunk_builder.requiresFlush(current_time)) {
                // UPDATED: Correctly passing outfile and current_time
                chunk_builder.flushToDisk(outfile, current_time);
            }
        } 
        else if (line[0] == 'b' || line[0] == 'B') {
            size_t space_idx = line.find(' ');
            if (space_idx != std::string_view::npos) {
                std::string_view value = line.substr(1, space_idx - 1);
                std::string_view vcd_id = line.substr(space_idx + 1);
                
                // Simple safety check in case a weird VCD declares a bus late
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

    // Initialize converter with a chunk size of 10,000 time ticks
    VcdConverter converter(10000000); 
    
    std::cout << "Starting Waveform Compiler...\n";
    converter.processVCD(input_filepath, output_filepath);
    
    return 0;
}