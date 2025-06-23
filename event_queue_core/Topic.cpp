// Topic.cpp
#include "Topic.h"
#include <stdexcept>
#include <algorithm> // For std::lower_bound

namespace fs = std::filesystem;

Topic::Topic(const std::string& name, const std::string& topic_dir_path, bool create_if_missing)
    : name_(name), dir_path_(topic_dir_path) {
    data_file_path_ = (fs::path(dir_path_) / "data.log").string();
    index_file_path_ = (fs::path(dir_path_) / "index.idx").string();
    metadata_file_path_ = (fs::path(dir_path_) / "metadata.meta").string();

    if (create_if_missing) {
        if (!fs::exists(dir_path_)) {
            if (!fs::create_directories(dir_path_)) {
                throw std::runtime_error("Failed to create topic directory: " + dir_path_);
            }
        }
    } else if (!fs::exists(dir_path_)) {
        throw std::runtime_error("Topic directory does not exist: " + dir_path_);
    }
    load_or_create_files();
}

Topic::~Topic() {
    std::lock_guard<std::mutex> lock(topic_mutex_);
    if (data_writer_.is_open()) {
        data_writer_.close();
    }
    if (index_writer_.is_open()) {
        index_writer_.close();
    }
    // Metadata is saved on each append or explicitly
}

void Topic::load_or_create_files() {
    // Lock is acquired by caller or constructor logic
    load_metadata(); // Load next_offset_ first
    load_index();    // Load existing index

    // Open files for appending. If they don't exist, they are created.
    data_writer_.open(data_file_path_, std::ios::binary | std::ios::app | std::ios::out);
    if (!data_writer_.is_open()) {
        throw std::runtime_error("Failed to open topic data file for writing: " + data_file_path_);
    }
    index_writer_.open(index_file_path_, std::ios::binary | std::ios::app | std::ios::out);
    if (!index_writer_.is_open()) {
        throw std::runtime_error("Failed to open topic index file for writing: " + index_file_path_);
    }
    
    rebuild_index_if_needed(); // Important recovery step
}

void Topic::load_metadata() {
    if (fs::exists(metadata_file_path_)) {
        std::ifstream meta_reader(metadata_file_path_, std::ios::binary);
        if (meta_reader.is_open()) {
            try {
                next_offset_ = BinaryUtils::read_binary<uint64_t>(meta_reader);
            } catch (const std::runtime_error& e) {
                std::cerr << "Warning: Could not read metadata for topic " << name_ << ". Assuming new topic. Error: " << e.what() << std::endl;
                next_offset_ = 0; // Reset if corrupt
                save_metadata(); // Try to save a fresh one
            }
            meta_reader.close();
        } else {
             std::cerr << "Warning: Could not open metadata file for topic " << name_ << ". Assuming new topic." << std::endl;
            next_offset_ = 0;
            save_metadata(); // Create if not openable
        }
    } else {
        next_offset_ = 0; // Default for a new topic
        save_metadata();   // Create it
    }
}

void Topic::save_metadata() {
    // Assumes lock is held by caller
    std::ofstream meta_writer(metadata_file_path_, std::ios::binary | std::ios::trunc);
    if (meta_writer.is_open()) {
        BinaryUtils::write_binary(meta_writer, next_offset_);
        meta_writer.flush(); // Ensure it's written
        meta_writer.close();
    } else {
        std::cerr << "Error: Failed to save metadata for topic " << name_ << std::endl;
        // This is a critical error, data might not be recovered properly on next start
    }
}

void Topic::load_index() {
    // Assumes lock is held by caller
    offset_to_byte_pos_.clear();
    std::ifstream idx_reader(index_file_path_, std::ios::binary);
    if (idx_reader.is_open()) {
        while (idx_reader.peek() != EOF) {
            try {
                uint64_t msg_offset = BinaryUtils::read_binary<uint64_t>(idx_reader);
                uint64_t file_pos = BinaryUtils::read_binary<uint64_t>(idx_reader);
                offset_to_byte_pos_[msg_offset] = file_pos;
            } catch (const std::runtime_error& e) {
                std::cerr << "Warning: Error reading index for topic " << name_ << ". Index might be truncated. Error: " << e.what() << std::endl;
                // Potentially stop reading or mark for rebuild
                break; 
            }
        }
        idx_reader.close();
    }
}


void Topic::rebuild_index_if_needed() {
    // This function tries to ensure index and data log are consistent.
    // It's a simplified recovery. A more robust system might use checksums or transaction logs.
    // Assumes lock is held by caller.

    uint64_t max_offset_in_index = 0;
    if (!offset_to_byte_pos_.empty()) {
        max_offset_in_index = offset_to_byte_pos_.rbegin()->first;
    }

    // If next_offset_ (from metadata) suggests more messages than index,
    // or if data file is non-empty and index is empty, something is wrong or needs recovery.
    // We'll try to read data.log and reconstruct index entries if they are missing.
    bool needs_rebuild = false;
    if (next_offset_ > 0 && (offset_to_byte_pos_.empty() || max_offset_in_index < next_offset_ -1) ) {
         if (fs::exists(data_file_path_) && fs::file_size(data_file_path_) > 0) {
            needs_rebuild = true;
            std::cerr << "Topic " << name_ << ": Index seems out of sync or incomplete (next_offset=" << next_offset_
                      << ", max_index_offset=" << max_offset_in_index << "). Attempting to scan data.log." << std::endl;
         }
    }


    if (!needs_rebuild) return;

    std::ifstream data_reader(data_file_path_, std::ios::binary);
    if (!data_reader.is_open()) {
        std::cerr << "Error: Could not open data.log for index rebuild: " << name_ << std::endl;
        return;
    }

    uint64_t current_byte_pos = 0;
    uint64_t recovered_offset_count = 0;
    uint64_t highest_recovered_offset = (offset_to_byte_pos_.empty() ? 0 : offset_to_byte_pos_.rbegin()->first);

    // Close and reopen index writer in truncate mode if we are rebuilding parts of it
    if (index_writer_.is_open()) index_writer_.close();
    index_writer_.open(index_file_path_, std::ios::binary | (offset_to_byte_pos_.empty() ? std::ios::trunc : std::ios::app));
    if (!index_writer_.is_open()) {
        std::cerr << "Error: Could not open index.idx for appending during rebuild: " << name_ << std::endl;
        return; // Cannot proceed
    }


    while (data_reader.peek() != EOF) {
        uint64_t record_start_byte_pos = data_reader.tellg();
        try {
            uint64_t msg_offset = BinaryUtils::read_binary<uint64_t>(data_reader);
            uint32_t payload_len = BinaryUtils::read_binary<uint32_t>(data_reader);
            
            if (payload_len > 1024 * 1024 * 100) { // Sanity check
                 std::cerr << "Topic " << name_ << ": Aborting rebuild. Payload length " << payload_len << " too large at offset " << msg_offset << ". Data file might be corrupt." << std::endl;
                 break;
            }
            data_reader.seekg(payload_len, std::ios::cur); // Skip payload
             if (data_reader.fail()) { // Check if seek went past EOF
                std::cerr << "Topic " << name_ << ": Aborting rebuild. Failed to seek past payload for offset " << msg_offset << ". Data file might be truncated." << std::endl;
                break;
            }


            // If this offset is not in our in-memory index or is greater than what we have, add it.
            // This handles cases where index writing failed but data was written.
            if (offset_to_byte_pos_.find(msg_offset) == offset_to_byte_pos_.end()) {
                 if (msg_offset < next_offset_) { // Only add if it's an "old" message we missed indexing
                    offset_to_byte_pos_[msg_offset] = record_start_byte_pos;
                    // Also write to the index file
                    BinaryUtils::write_binary(index_writer_, msg_offset);
                    BinaryUtils::write_binary(index_writer_, record_start_byte_pos);
                    recovered_offset_count++;
                 }
            }
            highest_recovered_offset = std::max(highest_recovered_offset, msg_offset);
            current_byte_pos = data_reader.tellg();

        } catch (const std::runtime_error& e) {
            std::cerr << "Topic " << name_ << ": Incomplete record found in data.log during rebuild at byte " << record_start_byte_pos << ". Error: " << e.what() << ". Rebuild might be partial." << std::endl;
            break; // Stop processing on error
        }
    }
    data_reader.close();
    index_writer_.flush(); // Persist any newly written index entries


    if (recovered_offset_count > 0) {
        std::cout << "Topic " << name_ << ": Rebuilt " << recovered_offset_count << " missing index entries." << std::endl;
    }

    // Crucially, update next_offset_ based on the highest offset found or recovered.
    // next_offset_ should be one greater than the highest *valid* offset.
    uint64_t new_next_offset = 0;
    if (!offset_to_byte_pos_.empty()) {
        new_next_offset = offset_to_byte_pos_.rbegin()->first + 1;
    } else if (fs::exists(data_file_path_) && fs::file_size(data_file_path_) == 0) {
        // If data log is empty, next_offset should be 0
        new_next_offset = 0;
    } else {
        // If data log is not empty but index is, it means all messages were corrupt,
        // or next_offset was 0 to begin with. Stick to 0 unless we find a valid message.
        new_next_offset = (next_offset_ == 0 && highest_recovered_offset == 0 && offset_to_byte_pos_.empty()) ? 0 : highest_recovered_offset +1;
    }


    if (new_next_offset != next_offset_) {
        std::cout << "Topic " << name_ << ": Adjusting next_offset_ from " << next_offset_ << " to " << new_next_offset << " after scan/rebuild." << std::endl;
        next_offset_ = new_next_offset;
        save_metadata(); // Persist the corrected next_offset_
    }
    
    // Re-open data writer in append mode as it might have been closed if index_writer was.
    // Index writer is already open in append mode (or trunc if it was empty)
    if (!data_writer_.is_open()) {
        data_writer_.open(data_file_path_, std::ios::binary | std::ios::app | std::ios::out);
         if (!data_writer_.is_open()) {
            throw std::runtime_error("Failed to re-open topic data file after index rebuild: " + data_file_path_);
        }
    }
    if (!index_writer_.is_open()){ // Should be open, but as a safeguard
        index_writer_.open(index_file_path_, std::ios::binary | std::ios::app | std::ios::out);
        if (!index_writer_.is_open()) {
             throw std::runtime_error("Failed to re-open topic index file after index rebuild: " + index_file_path_);
        }
    }
}


uint64_t Topic::append_message(const std::string& payload) {
    std::lock_guard<std::mutex> lock(topic_mutex_);

    uint64_t current_offset = next_offset_;
    uint64_t current_byte_pos = data_writer_.tellp(); // Position before writing this message

    // Write to data.log
    BinaryUtils::write_binary(data_writer_, current_offset);
    BinaryUtils::write_string(data_writer_, payload); // write_string writes length first
    data_writer_.flush(); // Persist data

    // Write to index.idx
    BinaryUtils::write_binary(index_writer_, current_offset);
    BinaryUtils::write_binary(index_writer_, current_byte_pos);
    index_writer_.flush(); // Persist index

    // Update in-memory state
    offset_to_byte_pos_[current_offset] = current_byte_pos;
    next_offset_++;
    save_metadata(); // Persist new next_offset_

    return current_offset;
}

std::vector<Message> Topic::get_messages(uint64_t start_offset, uint32_t max_messages) {
    std::lock_guard<std::mutex> lock(topic_mutex_);
    std::vector<Message> messages;

    if (start_offset >= next_offset_ || max_messages == 0) {
        return messages; // No messages at or after this offset, or no messages requested
    }

    std::ifstream data_reader(data_file_path_, std::ios::binary);
    if (!data_reader.is_open()) {
        std::cerr << "Error: Failed to open data file for reading: " << data_file_path_ << std::endl;
        return messages; // Or throw
    }

    // Find the actual starting byte position using the index
    // We want the first entry with offset >= start_offset
    auto it = offset_to_byte_pos_.lower_bound(start_offset);
    if (it == offset_to_byte_pos_.end()) {
        // start_offset is beyond any known offset, but less than next_offset_
        // This state should ideally not happen if next_offset_ is accurate.
        // Or it could mean start_offset is for a message not yet fully committed (rare).
        data_reader.close();
        return messages;
    }

    uint64_t current_read_offset = it->first;
    data_reader.seekg(it->second); // Seek to the byte position of the message with current_read_offset

    while (messages.size() < max_messages && current_read_offset < next_offset_) {
        if (data_reader.peek() == EOF) break; // End of file

        try {
            // We expect the message at current_read_offset to be here.
            // Read and verify offset from data.log itself
            uint64_t file_msg_offset = BinaryUtils::read_binary<uint64_t>(data_reader);
            std::string payload = BinaryUtils::read_string(data_reader); // read_string reads length first

            if (file_msg_offset != current_read_offset) {
                // This is a serious inconsistency between index and data file!
                std::cerr << "Error: Index-data mismatch for topic " << name_
                          << ". Expected offset " << current_read_offset
                          << ", found " << file_msg_offset << " in data.log." << std::endl;
                // Consider stopping or trying to resync. For now, we stop.
                break;
            }
            
            messages.emplace_back(current_read_offset, name_, payload);
            
            // Advance to the next offset
            current_read_offset++;
            // If there's an index entry for the next offset, we could use it.
            // Otherwise, we've just read sequentially, which is fine.
            // For this loop, we'll just continue reading sequentially.
            // The initial seek got us to the right spot.
            if (offset_to_byte_pos_.count(current_read_offset)) {
                 // If the next message isn't immediately sequential in the file (e.g. due to compaction, not implemented here)
                 // then seek to its indexed position. Otherwise, we've already advanced data_reader.
                 // For simple append-only log, this is mostly redundant unless there's a file corruption and jump.
                uint64_t expected_next_byte_pos = data_reader.tellg();
                if (offset_to_byte_pos_[current_read_offset] != expected_next_byte_pos && current_read_offset < next_offset_) {
                     // This implies a gap or non-sequential data, which our current append logic doesn't create.
                     // But if it did, this seek would be necessary.
                     // std::cerr << "Adjusting seek for non-sequential message " << current_read_offset << std::endl;
                     data_reader.seekg(offset_to_byte_pos_[current_read_offset]);
                }
            } else if (current_read_offset < next_offset_) {
                // Next message is expected, but not in index? This is an error.
                // For robustness, we'll just assume it's sequential or stop.
                // std::cerr << "Warning: Offset " << current_read_offset << " not in index but expected for topic " << name_ << std::endl;
                // The loop condition `current_read_offset < next_offset_` will handle this.
            }


        } catch (const std::runtime_error& e) {
            std::cerr << "Error reading message from topic " << name_ << " at/after offset " << current_read_offset
                      << ". Error: " << e.what() << std::endl;
            break; // Stop reading on error
        }
    }
    data_reader.close();
    return messages;
}

uint64_t Topic::get_next_offset() const {
    // No lock needed as next_offset_ is read, and writes are protected.
    // However, for strictness with potential concurrent modifications, a lock might be preferred.
    // Or make next_offset_ atomic. For this example, simple read is okay given typical use.
    std::lock_guard<std::mutex> lock(topic_mutex_); // Added for safety
    return next_offset_;
}
