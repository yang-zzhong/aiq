// network/TcpSession.h
#pragma once
#include <boost/asio.hpp>
#include <memory>
#include <vector>
#include "../event_queue_core/EventQueue.h" // The actual queue
#include "NetworkProtocol.h"

using boost::asio::ip::tcp;

class TcpSession : public std::enable_shared_from_this<TcpSession> {
public:
    TcpSession(tcp::socket socket, EventQueue& event_queue);
    void start();

private:
    void do_read_header();
    void do_read_payload(NetworkProtocol::RequestHeader req_header);
    void handle_request(NetworkProtocol::RequestHeader req_header, const std::vector<char>& payload_buffer);
    
    void send_response(NetworkProtocol::CommandType response_type,
                       NetworkProtocol::StatusCode status,
                       const std::vector<char>& payload = {});
    void send_error_response(NetworkProtocol::CommandType original_request_type,
                             NetworkProtocol::StatusCode status_code,
                             const std::string& error_message);


    tcp::socket socket_;
    EventQueue& event_queue_; // Reference to the shared event queue
    std::vector<char> read_buffer_; // For header
    std::vector<char> payload_read_buffer_; // For payload
};
