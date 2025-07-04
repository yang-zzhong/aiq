// EventQueue.h
#pragma once
#include "Topic.h"
#include <string>
#include <vector>
#include <set>
#include <map>
#include <mutex>
#include <memory> // For std::unique_ptr
#include <filesystem>
#include "INewMessageListener.h" 
#include "EventQueue.h"

class LocalEventQueue : public EventQueue {
public:
    LocalEventQueue(const std::string& base_data_dir);
    ~LocalEventQueue();

    LocalEventQueue(const LocalEventQueue&) = delete;
    LocalEventQueue& operator=(const LocalEventQueue&) = delete;

    std::vector<std::string> list_topics();

    // Explicitly creates a topic if it doesn't exist.
    // Produce also creates topics on demand.
    bool create_topic(const std::string& topic_name);

    // Returns the offset of the produced message
    uint64_t produce(const std::string& topic_name, const std::string& payload);

    // Consumes messages from a specific topic starting at start_offset
    std::vector<Message> consume(const std::string& topic_name, uint64_t start_offset, uint32_t max_messages = 100);
    

private:
    Topic* get_or_create_topic(const std::string& topic_name);
    void load_existing_topics();

    std::string base_data_dir_;
    std::map<std::string, std::unique_ptr<Topic>> topics_;
    std::mutex topics_map_mutex_; // Mutex for accessing the topics_ map
};
