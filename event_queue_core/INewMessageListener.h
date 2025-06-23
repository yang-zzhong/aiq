// event_queue_core/INewMessageListener.h (or similar name)
#pragma once
#include "Message.h" // From core
#include <string>
#include <vector>
#include <cstdlib>

class INewMessageListener {
public:
    virtual ~INewMessageListener() = default;

    // Called when a single new message is available for a topic
    virtual void on_new_message(const Message& message) = 0;

    // Optional: Called when a batch of new messages is available (if your system supports this efficiently)
    // virtual void on_new_messages_batch(const std::string& topic_name, const std::vector<Message>& messages) = 0;
};