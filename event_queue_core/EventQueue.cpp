// EventQueue.cpp
#include "EventQueue.h"
#include <stdexcept>
#include <iostream> // For cout/cerr

namespace fs = std::filesystem;

EventQueue::EventQueue(const std::string& base_data_dir) : base_data_dir_(base_data_dir) {
    if (!fs::exists(base_data_dir_)) {
        if (!fs::create_directories(base_data_dir_)) {
            throw std::runtime_error("Failed to create base data directory: " + base_data_dir_);
        }
    } else if (!fs::is_directory(base_data_dir_)) {
        throw std::runtime_error("Base data path exists but is not a directory: " + base_data_dir_);
    }
    load_existing_topics();
}

EventQueue::~EventQueue() {
    // Topics will be destroyed by unique_ptr, their destructors handle file closing.
    std::cout << "EventQueue shutting down. Topics will be closed." << std::endl;
}

void EventQueue::add_listener(INewMessageListener* listener) {
    if (listener) {
        std::lock_guard<std::mutex> lock(listeners_mutex_);
        listeners_.insert(listener);
        std::cout << "EventQueue: Listener added." << std::endl;
    }
}

void EventQueue::remove_listener(INewMessageListener* listener) {
    if (listener) {
        std::lock_guard<std::mutex> lock(listeners_mutex_);
        if (listeners_.erase(listener)) {
            std::cout << "EventQueue: Listener removed." << std::endl;
        }
    }
}

void EventQueue::load_existing_topics() {
    std::lock_guard<std::mutex> lock(topics_map_mutex_);
    std::cout << "Loading existing topics from: " << base_data_dir_ << std::endl;
    for (const auto& entry : fs::directory_iterator(base_data_dir_)) {
        if (entry.is_directory()) {
            std::string topic_name = entry.path().filename().string();
            std::string topic_path = entry.path().string();
            try {
                std::cout << "Loading topic: " << topic_name << " from " << topic_path << std::endl;
                topics_[topic_name] = std::make_unique<Topic>(topic_name, topic_path, false /*create_if_missing=false*/);
            } catch (const std::exception& e) {
                std::cerr << "Error loading topic " << topic_name << ": " << e.what() << std::endl;
                // Decide if you want to halt or continue
            }
        }
    }
    std::cout << "Finished loading topics. Found " << topics_.size() << " topics." << std::endl;

}


Topic* EventQueue::get_or_create_topic(const std::string& topic_name) {
    // First, try read-only access to avoid locking if topic exists
    {
        // No lock here for the read, relying on map's thread-safety for find if elements are not modified.
        // However, if another thread is creating, this can be problematic.
        // A full lock is safer.
        std::lock_guard<std::mutex> lock(topics_map_mutex_);
        auto it = topics_.find(topic_name);
        if (it != topics_.end()) {
            return it->second.get();
        }
    }

    // Topic not found, acquire lock to create it
    std::lock_guard<std::mutex> lock(topics_map_mutex_);
    // Double check, another thread might have created it in the meantime
    auto it = topics_.find(topic_name);
    if (it != topics_.end()) {
        return it->second.get();
    }

    std::cout << "Creating new topic: " << topic_name << std::endl;
    fs::path topic_dir_path = fs::path(base_data_dir_) / topic_name;
    try {
        auto new_topic = std::make_unique<Topic>(topic_name, topic_dir_path.string());
        Topic* new_topic_ptr = new_topic.get();
        topics_[topic_name] = std::move(new_topic);
        return new_topic_ptr;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create topic " << topic_name << ": " << e.what() << std::endl;
        return nullptr; // Or rethrow
    }
}

bool EventQueue::create_topic(const std::string& topic_name) {
    return get_or_create_topic(topic_name) != nullptr;
}


uint64_t EventQueue::produce(const std::string& topic_name, const std::string& payload) {
    if (topic_name.empty() || payload.empty()) {
        throw std::invalid_argument("Topic name and payload cannot be empty.");
    }
    Topic* topic = get_or_create_topic(topic_name);
    if (!topic) {
        throw std::runtime_error("Failed to get or create topic: " + topic_name);
    }

    uint64_t offset = topic->append_message(payload);

    Message new_msg(offset, topic_name, payload);

    // Notify registered listeners
    std::vector<INewMessageListener*> current_listeners_copy;
    {
        std::lock_guard<std::mutex> lock(listeners_mutex_);
        // Copy to avoid holding lock while calling potentially long-running listener methods
        for(INewMessageListener* listener : listeners_) {
            current_listeners_copy.push_back(listener);
        }
    }

    for (INewMessageListener* listener : current_listeners_copy) {
        try {
            listener->on_new_message(new_msg); // <<< CORE NOTIFICATION
        } catch (const std::exception& e) {
            std::cerr << "EventQueue: Exception from listener during on_new_message: " << e.what() << std::endl;
            // Decide how to handle listener errors (e.g., remove misbehaving listener)
        }
    }
    return offset;
}

std::vector<Message> EventQueue::consume(const std::string& topic_name, uint64_t start_offset, uint32_t max_messages) {
    if (topic_name.empty()) {
        throw std::invalid_argument("Topic name cannot be empty.");
    }
    Topic* topic = nullptr;
    {
        std::lock_guard<std::mutex> lock(topics_map_mutex_); // Protect map access during find
        auto it = topics_.find(topic_name);
        if (it == topics_.end()) {
            // Option 1: Topic doesn't exist, return empty
            // std::cerr << "Consume warning: Topic " << topic_name << " does not exist." << std::endl;
            return {}; 
            // Option 2: Throw error
            // throw std::runtime_error("Topic not found: " + topic_name);
        }
        topic = it->second.get();
    }
    // Topic object itself has its own mutex for file operations
    return topic->get_messages(start_offset, max_messages);
}

uint64_t EventQueue::get_next_topic_offset(const std::string& topic_name) {
    if (topic_name.empty()) {
        throw std::invalid_argument("Topic name cannot be empty.");
    }
    Topic* topic = nullptr;
    {
        std::lock_guard<std::mutex> lock(topics_map_mutex_);
        auto it = topics_.find(topic_name);
        if (it == topics_.end()) {
             // If topic doesn't exist, next offset is conventionally 0
            return 0;
        }
        topic = it->second.get();
    }
    return topic->get_next_offset();
}

std::vector<std::string> EventQueue::list_topics() {
    std::vector<std::string> topic_names;
    std::lock_guard<std::mutex> lock(topics_map_mutex_);
    for(const auto& pair : topics_) {
        topic_names.push_back(pair.first);
    }
    return topic_names;
}
