// network/SubscriptionManager.cpp
#include "SubscriptionManager.h"
#include <iostream>
#include <algorithm> // For std::remove_if

// If using direct WebSocketSession interaction
// #include "WebSocketSession.h" // Include full header here

SubscriptionManager::SubscriptionManager() {
    std::cout << "SubscriptionManager: Initialized." << std::endl;
}

bool SubscriptionManager::subscribe(const std::string& topic_name,
                                    const std::string& subscriber_id,
                                    uint64_t start_offset,
                                    boost::asio::any_io_executor client_executor,
                                    MessageDeliveryCallback delivery_callback) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::cout << "SubscriptionManager: Client '" << subscriber_id << "' subscribing to topic '"
              << topic_name << "' from offset " << start_offset << std::endl;

    topic_subscriptions_[topic_name][subscriber_id] = {
        subscriber_id,
        start_offset,
        std::move(delivery_callback),
        std::move(client_executor)
    };
    return true;
}

bool SubscriptionManager::unsubscribe(const std::string& topic_name, const std::string& subscriber_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto topic_it = topic_subscriptions_.find(topic_name);
    if (topic_it != topic_subscriptions_.end()) {
        auto& subscribers = topic_it->second;
        if (subscribers.erase(subscriber_id) > 0) {
            std::cout << "SubscriptionManager: Client '" << subscriber_id << "' unsubscribed from topic '" << topic_name << "'" << std::endl;
            if (subscribers.empty()) {
                topic_subscriptions_.erase(topic_it); // Clean up topic if no subscribers left
            }
            return true;
        }
    }
    std::cout << "SubscriptionManager: Client '" << subscriber_id << "' not found for unsubscribe from topic '" << topic_name << "'" << std::endl;
    return false;
}

void SubscriptionManager::unsubscribe_all(const std::string& subscriber_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "SubscriptionManager: Unsubscribing client '" << subscriber_id << "' from all topics." << std::endl;
    for (auto it = topic_subscriptions_.begin(); it != topic_subscriptions_.end(); /* manual increment */) {
        auto& subscribers = it->second;
        if (subscribers.erase(subscriber_id) > 0) {
            std::cout << "  - Unsubscribed '" << subscriber_id << "' from '" << it->first << "'" << std::endl;
        }
        if (subscribers.empty()) {
            it = topic_subscriptions_.erase(it); // Erase topic and advance iterator
        } else {
            ++it;
        }
    }
}

// Implementation of the INewMessageListener interface
void SubscriptionManager::on_new_message(const Message& new_message) {
    // This is the method called by EventQueue.
    // The logic here is what used to be in notify_new_message().
    std::lock_guard<std::mutex> lock(mutex_);

    const std::string& topic_name = new_message.topic;
    auto topic_it = topic_subscriptions_.find(topic_name);
    if (topic_it == topic_subscriptions_.end()) return;

    std::vector<Message> single_message_batch = {new_message};

    for (auto& sub_pair : topic_it->second) {
        SubscriberInfo& sub_info = sub_pair.second;
        if (new_message.offset >= sub_info.next_offset_needed) {
            boost::asio::post(sub_info.client_executor, [
                delivery_cb = sub_info.deliver_messages,
                topic = topic_name,
                messages_batch = single_message_batch,
                subscriber_id = sub_info.subscriber_id
            ]() {
                delivery_cb(topic, messages_batch);
            });
            sub_info.next_offset_needed = new_message.offset + 1;
        }
    }
}