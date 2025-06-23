// client/TcpClient.h
#pragma once
#include <boost/asio.hpp>
#include <string>
#include <vector>
#include "../network/NetworkProtocol.h" // For structures and enums
#include "../event_queue_core/Message.h" // For Message struct

using boost::asio::ip::tcp;

class TcpClient {
public:
    TcpClient(boost::asio::io_context& io_context, const std::string& host, short port);

    // Make connect a separate step to allow for error handling
    bool connect();
    void disconnect();

    // Methods for each command
    bool produce(const std::string& topic, const std::string& payload, uint64_t& out_offset, std::string& out_error);
    bool consume(const std::string& topic, uint64_t start_offset, uint32_t max_messages, 
                 std::vector<Message>& out_messages, std::string& out_error);
    bool get_topic_offset(const std::string& topic, uint64_t& out_offset, std::string& out_error);
    bool create_topic(const std::string& topic, std::string& out_error);
    bool list_topics(std::vector<std::string>& out_topics, std::string& out_error);


private:
    // Generic send request and receive response
    bool send_request_receive_response(
        NetworkProtocol::RequestHeader req_header,
        const std::vector<char>& req_payload,
        NetworkProtocol::ResponseHeader& out_resp_header,
        std::vector<char>& out_resp_payload,
        std::string& out_error_string
    );

    boost::asio::io_context& io_context_;
    tcp::socket socket_;
    tcp::resolver::results_type endpoints_;
    std::string host_;
    short port_;
};
