// BinaryUtils.cpp
#include "BinaryUtils.h" // Include the header for declarations

namespace BinaryUtils {

    // DEFINITIONS for non-template functions
    void write_string(std::ofstream& ofs, const std::string& str) {
        uint32_t len = static_cast<uint32_t>(str.length());
        BinaryUtils::write_binary(ofs, len); // Call the template function
        ofs.write(str.data(), len);
    }

    std::string read_string(std::ifstream& ifs) {
        uint32_t len = BinaryUtils::read_binary<uint32_t>(ifs); // Call the template function
        if (len > 1024 * 1024 * 100) {
             throw std::runtime_error("String length too large, possible data corruption.");
        }
        std::string str(len, '\0');
        ifs.read(&str[0], len);
         if (ifs.gcount() != len) {
            if (ifs.eof()) throw std::runtime_error("Premature EOF while reading string data.");
            throw std::runtime_error("Failed to read expected string data size.");
        }
        return str;
    }

} // namespace BinaryUtils