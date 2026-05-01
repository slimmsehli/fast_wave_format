#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint> // <--- Added this missing header!

// Helper to trim whitespace
std::string_view trim(std::string_view sv) {
    size_t start = sv.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "";
    size_t end = sv.find_last_not_of(" \t\r\n");
    return sv.substr(start, end - start + 1);
}

// Helper to split strings
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

#include <iostream>
#include <chrono>
#include <sys/resource.h>

int main(int argc, char* argv[]) {
    auto start_time = std::chrono::high_resolution_clock::now();


    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.vcd>\n";
        return 1;
    }

    std::ifstream file(argv[1]);
    if (!file.is_open()) {
        std::cerr << "Failed to open VCD file.\n";
        return 1;
    }

    std::string target_vcd_id = "";
    uint32_t current_id = 0;
    
    std::string line;
    bool in_header = true;
    char last_known_value = 'X';
    
    while (std::getline(file, line)) {
        std::string_view sv = trim(line);
        if (sv.empty()) continue;

        if (in_header) {
            // Find the symbol for Signal ID 1 (the 2nd variable)
            if (sv.starts_with("$var")) {
                if (current_id == 1) {
                    auto tokens = split(sv);
                    if (tokens.size() >= 4) {
                        target_vcd_id = std::string(tokens[3]); 
                    }
                }
                current_id++;
            } else if (sv.starts_with("$enddefinitions")) {
                in_header = false;
                if (target_vcd_id.empty()) {
                    std::cout << "Signal ID 1 not found in header.\n";
                    return 1;
                }
            }
        } else {
            // Data Dump Parsing
            if (sv[0] == '#') {
                uint64_t current_time = std::stoull(std::string(sv.substr(1)));
                if (current_time > 5500) {
                    // We passed the target time, stop reading!
                    break; 
                }
            } 
            // Check if this line is a change for our target 1-bit signal
            else if (sv.length() > target_vcd_id.length() && sv.ends_with(target_vcd_id)) {
                // E.g., "1!" where target is "!"
                if (sv.length() == target_vcd_id.length() + 1) {
                    last_known_value = sv[0];
                }
            }
        }
    }

    std::cout << "Value of Signal 1 at Time 5500 is: " << last_known_value << "\n";

    // 2. Stop the clock
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    // 3. Get memory usage
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    // Note: ru_maxrss is in Kilobytes on Linux, but Bytes on macOS. 
    // Assuming Linux here:
    long memory_kb = usage.ru_maxrss;
    // 4. Print the results
    std::cout << "\n--- Execution Metrics ---" << std::endl;
    std::cout << "Time taken: " << elapsed.count() << " seconds" << std::endl;
    std::cout << "Peak memory used: " << memory_kb << " KB" << std::endl;


    return 0;
}