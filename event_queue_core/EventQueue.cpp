#include <iostream>
#include "EventQueue.h"

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

void EventQueue::notify_new_message(Message new_msg) {
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
}