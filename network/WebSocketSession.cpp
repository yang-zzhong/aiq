// network/WebSocketSession.cpp
#include "WebSocketSession.h"
#include <boost/asio/bind_executor.hpp>
#include <sstream>      // For stringstream in session_id
#include <iomanip>      // For setfill, setw
#include <random>       // For session_id
#include <chrono>       // For chrono::seconds


// Helper function to generate a somewhat unique session ID
std::string WebSocketSession::generate_session_id() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << dist(rng);
    return ss.str();
}


WebSocketSession::WebSocketSession( tcp::socket&& socket,
                                    EventQueue& queue,
                                    SubscriptionManager& sub_mgr,
                                    net::io_context& ioc)
    : ws_(std::move(socket)), // Takes ownership of the raw TCP socket
      event_queue_(queue),
      sub_manager_(sub_mgr), // <<< STORE THIS
      strand_(net::make_strand(ioc.get_executor())), // <<< MODIFIED HERE: Use ioc.get_executor()
      session_id_(generate_session_id())
{
    std::cout << "WS Session [" << session_id_ << "]: Created." << std::endl;
}

WebSocketSession::~WebSocketSession() {
    std::cout << "WS Session [" << session_id_ << "]: Destroyed." << std::endl;
    // Unsubscribe from all topics if not already done by do_close
    // Posting to strand ensures thread safety if destructor called from different thread
    // However, this might be tricky if io_context is stopping.
    // Best to ensure do_close is robustly called.
    // if(ws_.is_open()){ // A simple check, not foolproof
    //    sub_manager_.unsubscribe_all(session_id_);
    // }
}

void WebSocketSession::run() {
    // We need to be executing within a strand to perform async operations
    // on the I/O objects in this session.
    // net::dispatch ensures that the handler is executed immediately if
    // we are already on the strand, otherwise it posts it.
    net::dispatch(ws_.get_executor(),
        beast::bind_front_handler(
            &WebSocketSession::on_run_start,
            shared_from_this()));
}

// This intermediate step ensures we're on the strand before starting async ops
void WebSocketSession::on_run_start() {
    // Set suggested timeout settings for the websocket
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));

    // Set a decorator to change the Server of the handshake
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res) {
            res.set(http::field::server,
                std::string(BOOST_BEAST_VERSION_STRING) + " websocket-event-queue-server");
        }));

    // Accept the websocket handshake
    ws_.async_accept(
        net::bind_executor(
            strand_, // Ensure handler runs on the strand
            beast::bind_front_handler(
                &WebSocketSession::on_accept,
                shared_from_this())));
}


void WebSocketSession::on_accept(beast::error_code ec) {
    if (ec) {
        std::cerr << "WS Session [" << session_id_ << "]: Accept error: " << ec.message() << std::endl;
        return do_close(); // Or just let the session die
    }
    std::cout << "WS Session [" << session_id_ << "]: Accepted connection." << std::endl;

    // Start reading messages
    do_read();
}

void WebSocketSession::do_read() {
    buffer_.clear(); // Clear previous message from buffer
    ws_.async_read(
        buffer_,
        net::bind_executor(
            strand_,
            beast::bind_front_handler(
                &WebSocketSession::on_read,
                shared_from_this())));
}

void WebSocketSession::on_read(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    // This indicates that the session was closed
    if (ec == websocket::error::closed || ec == http::error::end_of_stream || ec == net::error::eof) {
        std::cout << "WS Session [" << session_id_ << "]: Closed by client." << std::endl;
        return do_close();
    }
    if (ec) {
        std::cerr << "WS Session [" << session_id_ << "]: Read error: " << ec.message() << std::endl;
        return do_close();
    }

    // Echo the message back for now, or process it
    // For text messages:
    if (ws_.got_text()) {
        std::string message_text = beast::buffers_to_string(buffer_.data());
        std::cout << "WS Session [" << session_id_ << "]: Received: " << message_text << std::endl;
        process_message(message_text);
        // Continue reading
        do_read();
    } else if (ws_.got_binary()) {
        // This server expects text (JSON) messages
        std::cerr << "WS Session [" << session_id_ << "]: Received binary message, expected text. Closing." << std::endl;
        send_error_response(std::nullopt, "Binary messages not supported. Send JSON text.");
        return do_close(); // Or just ignore and do_read()
    }
    // Note: Pings/Pongs are handled automatically by Beast unless `control_callback` is set.
}

void WebSocketSession::process_message(const std::string& message_text) {
    try {
        json raw_json = json::parse(message_text);
        WebSocketProtocol::BaseWsMessage base_msg = raw_json.get<WebSocketProtocol::BaseWsMessage>();

        switch (base_msg.command) {
            case WebSocketProtocol::Command::PRODUCE_REQUEST:
                handle_produce_request(raw_json.get<WebSocketProtocol::ProduceWsRequest>());
                break;
            case WebSocketProtocol::Command::SUBSCRIBE_TOPIC_REQUEST:
                handle_subscribe_topic_request(raw_json.get<WebSocketProtocol::SubscribeTopicWsRequest>());
                break;
            case WebSocketProtocol::Command::UNSUBSCRIBE_TOPIC_REQUEST:
                handle_unsubscribe_topic_request(raw_json.get<WebSocketProtocol::UnsubscribeTopicWsRequest>());
                break;
            case WebSocketProtocol::Command::CREATE_TOPIC_REQUEST:
                handle_create_topic_request(raw_json.get<WebSocketProtocol::CreateTopicWsRequest>());
                break;
            case WebSocketProtocol::Command::LIST_TOPICS_REQUEST:
                handle_list_topics_request(base_msg); // Base is enough
                break;
            case WebSocketProtocol::Command::GET_NEXT_OFFSET_REQUEST:
                handle_get_next_offset_request(raw_json.get<WebSocketProtocol::GetNextOffsetWsRequest>());
                break;
            default:
                std::cerr << "WS Session [" << session_id_ << "]: Unknown command type: "
                          << static_cast<int>(base_msg.command) << std::endl;
                send_error_response(base_msg.req_id, "Unknown command received.", base_msg.command);
                break;
        }
    } catch (const json::parse_error& e) {
        std::cerr << "WS Session [" << session_id_ << "]: JSON parse error: " << e.what() << std::endl;
        send_error_response(std::nullopt, "Invalid JSON format: " + std::string(e.what()));
    } catch (const json::exception& e) { // Catches type errors, missing fields etc.
        std::cerr << "WS Session [" << session_id_ << "]: JSON processing error: " << e.what() << std::endl;
        // Try to get req_id if possible, otherwise nullopt
        std::optional<uint64_t> req_id;
        try {
            json temp_json = json::parse(message_text);
            if(temp_json.contains("req_id") && temp_json["req_id"].is_number_unsigned()) {
                req_id = temp_json["req_id"].get<uint64_t>();
            }
        } catch(...) { /* ignore, can't get req_id */ }
        send_error_response(req_id, "JSON message structure error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        std::cerr << "WS Session [" << session_id_ << "]: Exception processing message: " << e.what() << std::endl;
        send_error_response(std::nullopt, "Internal server error processing message.");
    }
}


void WebSocketSession::do_write(const std::string& message_text) {
    // Ensure we are using text mode for sending JSON
    ws_.text(true);

    ws_.async_write(
        net::buffer(message_text),
        net::bind_executor(
            strand_,
            beast::bind_front_handler(
                &WebSocketSession::on_write,
                shared_from_this())));
}

void WebSocketSession::on_write(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec) {
        std::cerr << "WS Session [" << session_id_ << "]: Write error: " << ec.message() << std::endl;
        return do_close(); // Or let session die
    }
    // std::cout << "WS Session [" << session_id_ << "]: Message sent (" << bytes_transferred << " bytes)." << std::endl;
    // Buffer is not cleared here, it's managed by the caller or `do_read`
}

void WebSocketSession::do_close() {
    // This function is not run on the strand, so post the close operation.
    // Or, if it's always called from a strand context, direct call is fine.
    // For safety, always operate on ws_ via its strand.
    if (ws_.is_open()) {
         std::cout << "WS Session [" << session_id_ << "]: Initiating close." << std::endl;
        // Unsubscribe from all topics this session was part of
        sub_manager_.unsubscribe_all(session_id_); // <<< ADD THIS

        // Post a call to `websocket::stream::async_close`
        // No need to bind_executor if we're posting to the strand's context already
        // but explicit is safer.
        net::post(strand_, [self = shared_from_this()]() {
            if (self->ws_.is_open()) { // Re-check as it might be closed by another op
                 self->ws_.async_close(websocket::close_code::normal,
                    net::bind_executor(self->strand_,
                        [self_inner = self](beast::error_code ec_close) {
                            if (ec_close) {
                                std::cerr << "WS Session [" << self_inner->session_id_ << "]: Close error: " << ec_close.message() << std::endl;
                            } else {
                                std::cout << "WS Session [" << self_inner->session_id_ << "]: Closed gracefully." << std::endl;
                            }
                            // The actual TCP socket is closed by the stream destructor.
                        }));
            }
        });
    } else {
        std::cout << "WS Session [" << session_id_ << "]: Already closed or not open." << std::endl;
    }
}


// --- Request Handler Implementations ---

void WebSocketSession::handle_produce_request(const WebSocketProtocol::ProduceWsRequest& req) {
    WebSocketProtocol::ProduceWsResponse resp;
    resp.command = WebSocketProtocol::Command::PRODUCE_RESPONSE;
    resp.req_id = req.req_id;
    resp.topic = req.topic;

    try {
        resp.offset = event_queue_.produce(req.topic, req.message_payload);
        resp.success = true;
    } catch (const std::exception& e) {
        resp.success = false;
        resp.error_message = e.what();
        std::cerr << "WS Session [" << session_id_ << "]: Produce error for topic '" << req.topic << "': " << e.what() << std::endl;
    }
    send_ws_message(resp);
}

void WebSocketSession::handle_subscribe_topic_request(const WebSocketProtocol::SubscribeTopicWsRequest& req) {
    WebSocketProtocol::SubscribeTopicWsResponse resp;
    resp.command = WebSocketProtocol::Command::SUBSCRIBE_TOPIC_RESPONSE;
    resp.req_id = req.req_id;
    resp.topic = req.topic;

    // The callback function that SubscriptionManager will use to send us messages
    MessageDeliveryCallback delivery_cb =
        [self = weak_from_this()](const std::string& topic, const std::vector<Message>& msgs) {
        if (auto strong_self = self.lock()) { // Ensure session still exists
            // This callback will be invoked by SubscriptionManager on our client_executor (strand_)
            strong_self->deliver_subscribed_messages(topic, msgs);
        }
    };

    // Get the executor for this session's strand
    auto client_exec = net::get_associated_executor(strand_);

    if (sub_manager_.subscribe(req.topic, req.subscriber_id, req.start_offset, client_exec, std::move(delivery_cb))) {
        resp.success = true;
        std::cout << "WS Session [" << session_id_ << "]: Subscription request to topic '" << req.topic
                  << "' from offset " << req.start_offset << " successful." << std::endl;
    } else {
        resp.success = false;
        resp.error_message = "Failed to subscribe with SubscriptionManager.";
        std::cerr << "WS Session [" << session_id_ << "]: Subscription to topic '" << req.topic << "' failed at manager." << std::endl;
    }
    send_ws_message(resp);

    // Optional: Implement "catch-up" logic here. After successful subscription,
    // immediately try to fetch messages from req.start_offset up to current latest
    // and send them via deliver_subscribed_messages.
    // This would require a call to event_queue_.consume()
    // Be careful with threading: post this task to the strand.
    if (resp.success) {
        net::post(strand_, [self = shared_from_this(), topic_name = req.topic, start_offset = req.start_offset]() {
            try {
                // std::cout << "WS Session [" << self->session_id_ << "]: Catching up messages for " << topic_name << " from " << start_offset << std::endl;
                std::vector<Message> initial_messages = self->event_queue_.consume(topic_name, start_offset, 100); // Consume a batch
                if (!initial_messages.empty()) {
                    self->deliver_subscribed_messages(topic_name, initial_messages);
                    // The SubscriptionManager will be notified of subsequent messages from EventQueue::produce
                    // The 'next_offset_needed' in SubscriberInfo for this client should be updated by this delivery
                    // or by the first call from notify_new_message in SubscriptionManager.
                    // For simplicity here, the SubscriptionManager's next_offset_needed will start from start_offset
                    // and get updated on the first push.
                    // A more robust catch-up would coordinate this offset update.
                }
            } catch (const std::exception& e) {
                std::cerr << "WS Session [" << self->session_id_ << "]: Error during catch-up for topic " << topic_name << ": " << e.what() << std::endl;
            }
        });
    }
}

void WebSocketSession::handle_unsubscribe_topic_request(const WebSocketProtocol::UnsubscribeTopicWsRequest& req) {
    WebSocketProtocol::UnsubscribeTopicWsResponse resp;
    resp.command = WebSocketProtocol::Command::UNSUBSCRIBE_TOPIC_RESPONSE;
    resp.req_id = req.req_id;
    resp.topic = req.topic;

    if (sub_manager_.unsubscribe(req.topic, req.subscriber_id)) {
        resp.success = true;
        std::cout << "WS Session [" << req.subscriber_id << "]: Unsubscribe from topic '" << req.topic << "' successful." << std::endl;
    } else {
        resp.success = false;
        resp.error_message = "Failed to unsubscribe or not subscribed.";
    }
    send_ws_message(resp);
}

// This is the callback method called by SubscriptionManager
void WebSocketSession::deliver_subscribed_messages(const std::string& topic_name, const std::vector<Message>& messages) {
    // This method is already posted to run on this session's strand by SubscriptionManager
    if (messages.empty()) return;

    WebSocketProtocol::MessageBatchWsNotification notification;
    notification.command = WebSocketProtocol::Command::MESSAGE_BATCH_NOTIFICATION;
    // notification.req_id might be a subscription ID if you implement that
    notification.topic = topic_name;
    notification.messages = messages; // Message struct should be JSON serializable

    std::cout << "WS Session [" << session_id_ << "]: Delivering " << messages.size()
              << " msgs for subscribed topic '" << topic_name << "'." << std::endl;
    send_ws_message(notification);
}

void WebSocketSession::handle_create_topic_request(const WebSocketProtocol::CreateTopicWsRequest& req) {
    WebSocketProtocol::CreateTopicWsResponse resp;
    resp.command = WebSocketProtocol::Command::CREATE_TOPIC_RESPONSE;
    resp.req_id = req.req_id;
    resp.topic = req.topic;
    try {
        event_queue_.create_topic(req.topic);
        resp.success = true;
    } catch (const std::exception& e) {
        resp.success = false;
        resp.error_message = e.what();
        std::cerr << "WS Session [" << session_id_ << "]: Create topic error for '" << req.topic << "': " << e.what() << std::endl;
    }
    send_ws_message(resp);
}

void WebSocketSession::handle_list_topics_request(const WebSocketProtocol::BaseWsMessage& req_base) {
    WebSocketProtocol::ListTopicsWsResponse resp;
    resp.command = WebSocketProtocol::Command::LIST_TOPICS_RESPONSE;
    resp.req_id = req_base.req_id;
    try {
        resp.topics = event_queue_.list_topics();
        resp.success = true;
    } catch (const std::exception& e) {
        resp.success = false;
        resp.error_message = e.what();
        std::cerr << "WS Session [" << session_id_ << "]: List topics error: " << e.what() << std::endl;
    }
    send_ws_message(resp);
}

void WebSocketSession::handle_get_next_offset_request(const WebSocketProtocol::GetNextOffsetWsRequest& req) {
    WebSocketProtocol::GetNextOffsetWsResponse resp;
    resp.command = WebSocketProtocol::Command::GET_NEXT_OFFSET_RESPONSE;
    resp.req_id = req.req_id;
    resp.topic = req.topic;
    try {
        resp.next_offset = event_queue_.get_next_topic_offset(req.topic);
        resp.success = true;
    } catch (const std::exception& e) {
        resp.success = false;
        resp.error_message = e.what();
        std::cerr << "WS Session [" << session_id_ << "]: Get next offset error for '" << req.topic << "': " << e.what() << std::endl;
    }
    send_ws_message(resp);
}

// --- Helper to send JSON messages ---
template<typename T>
void WebSocketSession::send_ws_message(const T& message_payload) {
    // This must be called from the strand or post to it.
    // Most callers (request handlers) are already on the strand from on_read.
    // check_and_send_subscribed_messages is also posted to strand.
    if(!strand_.running_in_this_thread()) {
       return net::post(strand_, [self = shared_from_this(), message_payload](){ // Capture by value
           self->send_ws_message(message_payload);
       });
    }

    try {
        json j = message_payload; // Serialize to JSON
        do_write(j.dump());
    } catch (const json::exception& e) {
        std::cerr << "WS Session [" << session_id_ << "]: JSON serialization error for outgoing message: " << e.what() << std::endl;
        // Cannot easily send an error back if serialization itself failed. Log and potentially close.
    } catch (const std::exception& e) {
        std::cerr << "WS Session [" << session_id_ << "]: Exception sending WS message: " << e.what() << std::endl;
    }
}

void WebSocketSession::send_error_response(std::optional<uint64_t> req_id, const std::string& error_msg,
                                           std::optional<WebSocketProtocol::Command> original_cmd) {
    WebSocketProtocol::ErrorWsResponse err_resp;
    err_resp.command = WebSocketProtocol::Command::ERROR_RESPONSE;
    err_resp.req_id = req_id;
    err_resp.error_message = error_msg;
    err_resp.original_command_type = original_cmd;
    send_ws_message(err_resp);
}