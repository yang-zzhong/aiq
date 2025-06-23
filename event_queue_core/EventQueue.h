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

class EventQueue {
public:
    EventQueue(const std::string& base_data_dir);
    ~EventQueue();

    EventQueue(const EventQueue&) = delete;
    EventQueue& operator=(const EventQueue&) = delete;

    // Returns the offset of the produced message
    uint64_t produce(const std::string& topic_name, const std::string& payload);

    // Consumes messages from a specific topic starting at start_offset
    std::vector<Message> consume(const std::string& topic_name, uint64_t start_offset, uint32_t max_messages = 100);
    
    // Gets the next available offset for a topic (i.e., where the next message will be written)
    uint64_t get_next_topic_offset(const std::string& topic_name);

    // Explicitly creates a topic if it doesn't exist.
    // Produce also creates topics on demand.
    bool create_topic(const std::string& topic_name);
    
    std::vector<std::string> list_topics();

    void add_listener(INewMessageListener* listener);     // <<< ADD THIS
    void remove_listener(INewMessageListener* listener); // <<< ADD THIS

private:
    Topic* get_or_create_topic(const std::string& topic_name);
    void load_existing_topics();

    std::string base_data_dir_;
    std::map<std::string, std::unique_ptr<Topic>> topics_;
    std::mutex topics_map_mutex_; // Mutex for accessing the topics_ map
    std::set<INewMessageListener*> listeners_; // <<< ADD THIS
    std::mutex listeners_mutex_;               // <<< ADD THIS
};
