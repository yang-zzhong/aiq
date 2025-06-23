// network/SubscriptionManager.h
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <memory> // For std::weak_ptr
#include <boost/asio/post.hpp> // To post notifications to client's strand
#include <boost/asio/any_io_executor.hpp>

#include "../event_queue_core/Message.h" // Assumes Message.h is here
#include "../event_queue_core/INewMessageListener.h" // Assumes Message.h is here

// Forward declaration for WebSocketSession to avoid circular include
// We only need to know it exists to store a weak_ptr to it.
// A more generic approach would use a std::function callback.
class WebSocketSession; // If directly interacting with WebSocketSession
                       // Alternatively, use a more generic callback mechanism

// A generic callback type for delivering messages to a subscriber
// Parameters: topic_name, vector_of_messages
using MessageDeliveryCallback = std::function<void(const std::string&, const std::vector<Message>&)>;

struct SubscriberInfo {
    std::string subscriber_id; // Unique ID for the subscriber (e.g., WebSocket session ID)
    uint64_t next_offset_needed;
    MessageDeliveryCallback deliver_messages;
    boost::asio::any_io_executor client_executor; // Executor to post delivery task for thread safety

    // For direct WebSocketSession interaction (alternative to generic callback)
    // std::weak_ptr<WebSocketSession> ws_session_wptr;
};

class SubscriptionManager : public INewMessageListener {
public:
    SubscriptionManager();

    // Called by WebSocketSession or SSE handler to subscribe
    // Returns a subscription ID or some handle (optional, can use client_id)
    bool subscribe(const std::string& topic_name,
                   const std::string& subscriber_id, // e.g., session ID
                   uint64_t start_offset,
                   boost::asio::any_io_executor client_executor, // Executor of the client session
                   MessageDeliveryCallback delivery_callback);

    void on_new_message(const Message& message) override;

    // Called by WebSocketSession or SSE handler to unsubscribe
    bool unsubscribe(const std::string& topic_name, const std::string& subscriber_id);
    void unsubscribe_all(const std::string& subscriber_id); // When a client disconnects

private:
    std::mutex mutex_;
    // Key: topic_name
    // Value: map of <subscriber_id, SubscriberInfo>
    std::map<std::string, std::map<std::string, SubscriberInfo>> topic_subscriptions_;

    // Optional: If you need to retrieve messages directly (alternative to notify_new_messages)
    // EventQueue& event_queue_; // Be careful with dependencies
};