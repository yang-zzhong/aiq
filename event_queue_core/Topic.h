// Topic.h
#pragma once
#include "Message.h"
#include "BinaryUtils.h"
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <map> // For in-memory index
#include <filesystem> // C++17 for path manipulation
#include <iostream>   // For cerr

// Forward declaration
class EventQueue;

class Topic {
public:
    Topic(const std::string& name, const std::string& topic_dir_path, bool create_if_missing = true);
    ~Topic();

    Topic(const Topic&) = delete;
    Topic& operator=(const Topic&) = delete;
    Topic(Topic&&) = default;
    Topic& operator=(Topic&&) = default;

    std::string get_name() const { return name_; }
    uint64_t append_message(const std::string& payload);
    std::vector<Message> get_messages(uint64_t start_offset, uint32_t max_messages);
    uint64_t get_next_offset() const;


private:
    void load_or_create_files();
    void load_metadata();
    void save_metadata();
    void load_index();
    void rebuild_index_if_needed(); // In case of crash before index write

    std::string name_;
    std::string dir_path_;
    std::string data_file_path_;
    std::string index_file_path_;
    std::string metadata_file_path_;

    std::ofstream data_writer_;
    std::ofstream index_writer_;
    // Reader streams are opened on demand in get_messages

    uint64_t next_offset_ = 0;
    std::map<uint64_t, uint64_t> offset_to_byte_pos_; // In-memory index: message_offset -> file_byte_offset

    mutable std::mutex topic_mutex_; // Protects file access and next_offset_
};
