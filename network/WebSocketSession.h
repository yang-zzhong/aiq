// network/WebSocketSession.h
#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>

#include "../../event_queue_core/EventQueue.h" // The core queue logic
#include "WebSocketTypes.h"                 // Our WebSocket message protocol
#include "SubscriptionManager.h"

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
using json = nlohmann::json;

// Forward declaration for a potential global subscription manager
// class SubscriptionManager; (If you build a more advanced one)

// Represents a single WebSocket session (client connection)
class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    EventQueue& event_queue_;
    SubscriptionManager& sub_manager_; // <<< ADD THIS
    net::strand<net::io_context::executor_type> strand_;

    std::string session_id_; // For logging/debugging

public:
    // Takes ownership of the socket
    // WebSocketSession(tcp::socket&& socket, EventQueue& queue);
    WebSocketSession(
      tcp::socket&& socket,
      EventQueue& queue, 
      SubscriptionManager& sub_mgr, // <<< ADD THIS
      net::io_context& ioc);

    ~WebSocketSession();

    // Start the session
    void run();

    void on_run_start();

  private:
    void on_accept(beast::error_code ec);
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void process_message(const std::string& message_text);

    void do_write(const std::string& message_text);
    void on_write(beast::error_code ec, std::size_t bytes_transferred);
    
    void do_close();

    // --- Request Handlers ---
    void handle_produce_request(const WebSocketProtocol::ProduceWsRequest& req);
    void handle_subscribe_topic_request(const WebSocketProtocol::SubscribeTopicWsRequest& req);
    void handle_unsubscribe_topic_request(const WebSocketProtocol::UnsubscribeTopicWsRequest& req);
    void handle_create_topic_request(const WebSocketProtocol::CreateTopicWsRequest& req);
    void handle_list_topics_request(const WebSocketProtocol::BaseWsMessage& req); // Base is enough
    void handle_get_next_offset_request(const WebSocketProtocol::GetNextOffsetWsRequest& req);

    // Callback for SubscriptionManager to deliver messages
    void deliver_subscribed_messages(const std::string& topic_name, const std::vector<Message>& messages);

    // Helper to send JSON messages
    template<typename T>
    void send_ws_message(const T& message_payload);
    void send_error_response(std::optional<uint64_t> req_id, const std::string& error_msg,
                             std::optional<WebSocketProtocol::Command> original_cmd = std::nullopt);
    
    static std::string generate_session_id();
};