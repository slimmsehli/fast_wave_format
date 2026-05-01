#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <algorithm>

enum class LogicState : uint8_t {
    VAL_0 = 0b00,
    VAL_1 = 0b01,
    VAL_X = 0b10,
    VAL_Z = 0b11
};

struct ChunkLocation {
    uint64_t start_time;
    uint64_t end_time;
    std::streampos byte_offset; 
};

class WaveReaderVarint {
private:
    std::ifstream file;
    std::unordered_map<uint32_t, uint32_t> signal_widths;
    std::vector<ChunkLocation> chunk_index;

    // Helper to decode Varints back into normal integers
    std::vector<uint32_t> decodeVarints(const std::vector<uint8_t>& buffer, uint32_t num_expected) {
        std::vector<uint32_t> deltas;
        deltas.reserve(num_expected);
        size_t i = 0;
        
        while (i < buffer.size() && deltas.size() < num_expected) {
            uint32_t value = 0;
            uint32_t shift = 0;
            while (i < buffer.size()) {
                uint8_t byte = buffer[i++];
                value |= ((byte & 0x7F) << shift);
                shift += 7;
                if ((byte & 0x80) == 0) break; // End of this integer
            }
            deltas.push_back(value);
        }
        return deltas;
    }

public:
    WaveReaderVarint(const std::string& filepath) {
        file.open(filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open wave file.\n";
            return;
        }
        parseHeaderAndBuildIndex();
    }

    ~WaveReaderVarint() {
        if (file.is_open()) file.close();
    }

    LogicState queryWire(uint32_t target_signal_id, uint64_t target_time) {
        if (signal_widths.find(target_signal_id) == signal_widths.end() || signal_widths[target_signal_id] != 1) {
            return LogicState::VAL_X;
        }

        // 1. Binary Search chunks
        auto it = std::lower_bound(chunk_index.begin(), chunk_index.end(), target_time,
            [](const ChunkLocation& chunk, uint64_t t) {
                return chunk.end_time <= t;
            });

        if (it == chunk_index.end() || it->start_time > target_time) {
            return LogicState::VAL_X; 
        }

        // 2. Jump to chunk
        file.seekg(it->byte_offset);

        // 3. Skip header (start, end)
        file.seekg(sizeof(uint64_t) * 2, std::ios::cur); 
        uint32_t active_columns;
        file.read(reinterpret_cast<char*>(&active_columns), sizeof(active_columns));

        uint32_t target_delta = target_time - it->start_time;

        // 4. Scan columns
        for (uint32_t i = 0; i < active_columns; ++i) {
            uint32_t sig_id, num_deltas, compressed_size, num_bytes;
            
            file.read(reinterpret_cast<char*>(&sig_id), sizeof(sig_id));
            file.read(reinterpret_cast<char*>(&num_deltas), sizeof(num_deltas));
            file.read(reinterpret_cast<char*>(&compressed_size), sizeof(compressed_size));

            if (sig_id == target_signal_id) {
                // Read compressed time deltas
                std::vector<uint8_t> compressed_buffer(compressed_size);
                file.read(reinterpret_cast<char*>(compressed_buffer.data()), compressed_size);
                
                // Read packed logic data
                file.read(reinterpret_cast<char*>(&num_bytes), sizeof(num_bytes));
                std::vector<uint8_t> data(num_bytes);
                file.read(reinterpret_cast<char*>(data.data()), num_bytes);

                // Decode times and find state
                std::vector<uint32_t> time_deltas = decodeVarints(compressed_buffer, num_deltas);
                return decodeWireState(target_delta, time_deltas, data);
            } else {
                // Skip the compressed times and the data bytes for this signal
                file.seekg(compressed_size, std::ios::cur);
                file.read(reinterpret_cast<char*>(&num_bytes), sizeof(num_bytes));
                file.seekg(num_bytes, std::ios::cur);
            }
        }
        return LogicState::VAL_X; 
    }

private:
    void parseHeaderAndBuildIndex() {
        char magic[4];
        file.read(magic, 4);
        if (std::string(magic, 4) != "WAVE") {
            std::cerr << "Invalid file format.\n";
            return;
        }

        uint32_t num_sigs;
        file.read(reinterpret_cast<char*>(&num_sigs), sizeof(num_sigs));

        for (uint32_t i = 0; i < num_sigs; ++i) {
            uint32_t id, width;
            file.read(reinterpret_cast<char*>(&id), sizeof(id));
            file.read(reinterpret_cast<char*>(&width), sizeof(width));
            signal_widths[id] = width;
        }

        std::cout << "Reader Dictionary Loaded. Indexing chunks...\n";

        while (file.peek() != EOF) {
            std::streampos current_pos = file.tellg();
            
            uint64_t start_time, end_time;
            uint32_t active_columns;
            
            if (!file.read(reinterpret_cast<char*>(&start_time), sizeof(start_time))) break;
            file.read(reinterpret_cast<char*>(&end_time), sizeof(end_time));
            file.read(reinterpret_cast<char*>(&active_columns), sizeof(active_columns));

            chunk_index.push_back({start_time, end_time, current_pos});

            for (uint32_t i = 0; i < active_columns; ++i) {
                // Skip ID and num_deltas
                file.seekg(sizeof(uint32_t) * 2, std::ios::cur); 
                
                uint32_t compressed_size;
                file.read(reinterpret_cast<char*>(&compressed_size), sizeof(compressed_size));
                file.seekg(compressed_size, std::ios::cur); 
                
                uint32_t num_bytes;
                file.read(reinterpret_cast<char*>(&num_bytes), sizeof(num_bytes));
                file.seekg(num_bytes, std::ios::cur); 
            }
        }
        std::cout << "Index built. Found " << chunk_index.size() << " chunks.\n";
    }

    LogicState decodeWireState(uint32_t target_delta, const std::vector<uint32_t>& time_deltas, const std::vector<uint8_t>& data) {
        int best_index = -1;
        for (size_t i = 0; i < time_deltas.size(); ++i) {
            if (time_deltas[i] <= target_delta) {
                best_index = i;
            } else {
                break; 
            }
        }

        if (best_index == -1) return LogicState::VAL_X;

        uint8_t byte_idx = best_index / 4;
        uint8_t bit_offset = (best_index % 4) * 2;
        
        uint8_t raw_state = (data[byte_idx] >> bit_offset) & 0b11;
        return static_cast<LogicState>(raw_state);
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_varints.wave>\n";
        return 1;
    }

    WaveReaderVarint reader(argv[1]); 

    // Benchmark query
    LogicState state = reader.queryWire(1, 5500);

    std::cout << "Value of Signal 1 at Time 5500 is: ";
    switch (state) {
        case LogicState::VAL_0: std::cout << "0\n"; break;
        case LogicState::VAL_1: std::cout << "1\n"; break;
        case LogicState::VAL_X: std::cout << "X\n"; break;
        case LogicState::VAL_Z: std::cout << "Z\n"; break;
    }

    return 0;
}