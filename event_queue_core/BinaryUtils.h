// BinaryUtils.h
#pragma once
#include <fstream>
#include <cstdint>
#include <string>
#include <stdexcept>

namespace BinaryUtils {

    // Template function definitions typically HAVE to be in the header
    // or in an included .tpp/.ipp file, unless you use explicit template instantiation.
    // So, templates are usually fine as they are if defined in the header.
    // The problem is with non-template functions.

    template<typename T>
    void write_binary(std::ofstream& ofs, const T& value) { // Definition stays in header for templates
        ofs.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    template<typename T>
    T read_binary(std::ifstream& ifs) { // Definition stays in header for templates
        T value;
        ifs.read(reinterpret_cast<char*>(&value), sizeof(T));
        if (ifs.gcount() != sizeof(T)) {
            if (ifs.eof()) throw std::runtime_error("Premature EOF while reading binary data.");
            throw std::runtime_error("Failed to read expected binary data size.");
        }
        return value;
    }

    // DECLARATIONS for non-template functions
    void write_string(std::ofstream& ofs, const std::string& str);
    std::string read_string(std::ifstream& ifs);

}