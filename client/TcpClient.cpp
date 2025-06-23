// client/TcpClient.cpp
#include "TcpClient.h"
#include <iostream>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>


TcpClient::TcpClient(boost::asio::io_context& io_context, const std::string& host, short port)
    : io_context_(io_context), socket_(io_context), host_(host), port_(port) {
    tcp::resolver resolver(io_context_);
    endpoints_ = resolver.resolve(host, std::to_string(port));
}

bool TcpClient::connect() {
    boost::system::error_code ec;
    boost::asio::connect(socket_, endpoints_, ec);
    if (ec) {
        std::cerr << "Client connect error: " << ec.message() << std::endl;
        return false;
    }
    std::cout << "Client connected to " << host_ << ":" << port_ << std::endl;
    return true;
}

void TcpClient::disconnect() {
    if (socket_.is_open()) {
        boost::system::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec); // Ignore error on shutdown
        socket_.close(ec); // Ignore error on close
        std::cout << "Client disconnected." << std::endl;
    }
}

bool TcpClient::send_request_receive_response(
    NetworkProtocol::RequestHeader req_header_struct,
    const std::vector<char>& req_payload,
    NetworkProtocol::ResponseHeader& out_resp_header_struct,
    std::vector<char>& out_resp_payload,
    std::string& out_error_string)
{
    if (!socket_.is_open()) {
        out_error_string = "Socket not connected.";
        return false;
    }

    boost::system::error_code ec;

    // 1. Send request header
    std::vector<char> req_header_bytes = req_header_struct.serialize();
    boost::asio::write(socket_, boost::asio::buffer(req_header_bytes), ec);
    if (ec) {
        out_error_string = "Send request header failed: " + ec.message();
        return false;
    }

    // 2. Send request payload (if any)
    if (!req_payload.empty()) {
        boost::asio::write(socket_, boost::asio::buffer(req_payload), ec);
        if (ec) {
            out_error_string = "Send request payload failed: " + ec.message();
            return false;
        }
    }

    // 3. Read response header
    std::vector<char> resp_header_bytes(NetworkProtocol::ResponseHeader::SIZE);
    boost::asio::read(socket_, boost::asio::buffer(resp_header_bytes), ec);
    if (ec) {
        out_error_string = "Read response header failed: " + ec.message();
        return false;
    }
    out_resp_header_struct = NetworkProtocol::ResponseHeader::deserialize(resp_header_bytes.data());

    // 4. Read response payload (if any)
    if (out_resp_header_struct.payload_length > 0) {
        if (out_resp_header_struct.payload_length > NetworkProtocol::MAX_PAYLOAD_SIZE) {
            out_error_string = "Server response payload too large: " + std::to_string(out_resp_header_struct.payload_length);
            // We should try to read it to clear the socket, or close the socket.
            // For simplicity, just error out. A robust client might try to drain it.
            return false;
        }
        out_resp_payload.resize(out_resp_header_struct.payload_length);
        boost::asio::read(socket_, boost::asio::buffer(out_resp_payload), ec);
        if (ec) {
            out_error_string = "Read response payload failed: " + ec.message();
            return false;
        }
    }
    return true;
}


bool TcpClient::produce(const std::string& topic, const std::string& payload, uint64_t& out_offset, std::string& out_error) {
    NetworkProtocol::ProduceRequest req_payload_struct;
    req_payload_struct.topic_name = topic;
    req_payload_struct.message_payload = payload;
    std::vector<char> req_payload_bytes = req_payload_struct.serialize();

    NetworkProtocol::RequestHeader req_header;
    req_header.type = NetworkProtocol::CommandType::PRODUCE_REQUEST;
    req_header.payload_length = static_cast<uint32_t>(req_payload_bytes.size());

    NetworkProtocol::ResponseHeader resp_header;
    std::vector<char> resp_payload_bytes;

    if (!send_request_receive_response(req_header, req_payload_bytes, resp_header, resp_payload_bytes, out_error)) {
        return false;
    }

    if (resp_header.status == NetworkProtocol::StatusCode::SUCCESS) {
        if (resp_header.type != NetworkProtocol::CommandType::PRODUCE_RESPONSE) {
             out_error = "Unexpected response type for PRODUCE."; return false;
        }
        try {
            NetworkProtocol::ProduceResponse resp_struct = NetworkProtocol::ProduceResponse::deserialize(resp_payload_bytes.data(), resp_payload_bytes.size());
            out_offset = resp_struct.offset;
            return true;
        } catch (const std::exception& e) {
            out_error = "Failed to deserialize PRODUCE response: " + std::string(e.what());
            return false;
        }
    } else {
        try {
            NetworkProtocol::ErrorResponsePayload err_resp = NetworkProtocol::ErrorResponsePayload::deserialize(resp_payload_bytes.data(), resp_payload_bytes.size());
            out_error = "Server error (PRODUCE): " + err_resp.error_message + " (Status: " + std::to_string(static_cast<int>(resp_header.status)) + ")";
        } catch (const std::exception& e) {
            out_error = "Server error (PRODUCE), and failed to parse error message. Status: " + std::to_string(static_cast<int>(resp_header.status));
        }
        return false;
    }
}

bool TcpClient::consume(const std::string& topic, uint64_t start_offset, uint32_t max_messages, 
                        std::vector<Message>& out_messages, std::string& out_error) {
    NetworkProtocol::ConsumeRequest req_payload_struct;
    req_payload_struct.topic_name = topic;
    req_payload_struct.start_offset = start_offset;
    req_payload_struct.max_messages = max_messages;
    std::vector<char> req_payload_bytes = req_payload_struct.serialize();

    NetworkProtocol::RequestHeader req_header;
    req_header.type = NetworkProtocol::CommandType::CONSUME_REQUEST;
    req_header.payload_length = static_cast<uint32_t>(req_payload_bytes.size());

    NetworkProtocol::ResponseHeader resp_header;
    std::vector<char> resp_payload_bytes;

    if (!send_request_receive_response(req_header, req_payload_bytes, resp_header, resp_payload_bytes, out_error)) {
        return false;
    }
    
    if (resp_header.status == NetworkProtocol::StatusCode::SUCCESS) {
         if (resp_header.type != NetworkProtocol::CommandType::CONSUME_RESPONSE) {
             out_error = "Unexpected response type for CONSUME."; return false;
        }
        try {
            // Pass topic name for context when deserializing messages
            NetworkProtocol::ConsumeResponse resp_struct = NetworkProtocol::ConsumeResponse::deserialize(resp_payload_bytes.data(), resp_payload_bytes.size(), topic);
            out_messages = std::move(resp_struct.messages);
            return true;
        } catch (const std::exception& e) {
            out_error = "Failed to deserialize CONSUME response: " + std::string(e.what());
            return false;
        }
    } else {
         try {
            NetworkProtocol::ErrorResponsePayload err_resp = NetworkProtocol::ErrorResponsePayload::deserialize(resp_payload_bytes.data(), resp_payload_bytes.size());
            out_error = "Server error (CONSUME): " + err_resp.error_message + " (Status: " + std::to_string(static_cast<int>(resp_header.status)) + ")";
        } catch (const std::exception& e) {
            out_error = "Server error (CONSUME), and failed to parse error message. Status: " + std::to_string(static_cast<int>(resp_header.status));
        }
        return false;
    }
}

bool TcpClient::get_topic_offset(const std::string& topic, uint64_t& out_offset, std::string& out_error) {
    std::vector<char> req_payload_bytes;
    NetworkProtocol::write_string_to_buffer(req_payload_bytes, topic);

    NetworkProtocol::RequestHeader req_header;
    req_header.type = NetworkProtocol::CommandType::GET_TOPIC_OFFSET_REQUEST;
    req_header.payload_length = static_cast<uint32_t>(req_payload_bytes.size());

    NetworkProtocol::ResponseHeader resp_header;
    std::vector<char> resp_payload_bytes;

    if (!send_request_receive_response(req_header, req_payload_bytes, resp_header, resp_payload_bytes, out_error)) {
        return false;
    }

    if (resp_header.status == NetworkProtocol::StatusCode::SUCCESS) {
        if (resp_header.type != NetworkProtocol::CommandType::GET_TOPIC_OFFSET_RESPONSE) {
             out_error = "Unexpected response type for GET_TOPIC_OFFSET."; return false;
        }
        try {
            size_t offset = 0;
            out_offset = NetworkProtocol::read_uint64_from_buffer(resp_payload_bytes.data(), offset);
            return true;
        } catch (const std::exception& e) {
            out_error = "Failed to deserialize GET_TOPIC_OFFSET response: " + std::string(e.what());
            return false;
        }
    } else {
         try {
            NetworkProtocol::ErrorResponsePayload err_resp = NetworkProtocol::ErrorResponsePayload::deserialize(resp_payload_bytes.data(), resp_payload_bytes.size());
            out_error = "Server error (GET_TOPIC_OFFSET): " + err_resp.error_message + " (Status: " + std::to_string(static_cast<int>(resp_header.status)) + ")";
        } catch (const std::exception& e) {
            out_error = "Server error (GET_TOPIC_OFFSET), and failed to parse error message. Status: " + std::to_string(static_cast<int>(resp_header.status));
        }
        return false;
    }
}

bool TcpClient::create_topic(const std::string& topic, std::string& out_error) {
    std::vector<char> req_payload_bytes;
    NetworkProtocol::write_string_to_buffer(req_payload_bytes, topic);

    NetworkProtocol::RequestHeader req_header;
    req_header.type = NetworkProtocol::CommandType::CREATE_TOPIC_REQUEST;
    req_header.payload_length = static_cast<uint32_t>(req_payload_bytes.size());
    
    NetworkProtocol::ResponseHeader resp_header;
    std::vector<char> resp_payload_bytes; // Expect empty for success

    if (!send_request_receive_response(req_header, req_payload_bytes, resp_header, resp_payload_bytes, out_error)) {
        return false;
    }
     if (resp_header.status == NetworkProtocol::StatusCode::SUCCESS) {
         if (resp_header.type != NetworkProtocol::CommandType::CREATE_TOPIC_RESPONSE) {
             out_error = "Unexpected response type for CREATE_TOPIC."; return false;
        }
        return true; // Success, no payload expected
    } else {
         try {
            NetworkProtocol::ErrorResponsePayload err_resp = NetworkProtocol::ErrorResponsePayload::deserialize(resp_payload_bytes.data(), resp_payload_bytes.size());
            out_error = "Server error (CREATE_TOPIC): " + err_resp.error_message + " (Status: " + std::to_string(static_cast<int>(resp_header.status)) + ")";
        } catch (const std::exception& e) {
            out_error = "Server error (CREATE_TOPIC), and failed to parse error message. Status: " + std::to_string(static_cast<int>(resp_header.status));
        }
        return false;
    }
}

bool TcpClient::list_topics(std::vector<std::string>& out_topics, std::string& out_error) {
    NetworkProtocol::RequestHeader req_header;
    req_header.type = NetworkProtocol::CommandType::LIST_TOPICS_REQUEST;
    req_header.payload_length = 0; // No payload for list topics request
    
    NetworkProtocol::ResponseHeader resp_header;
    std::vector<char> resp_payload_bytes;

    if (!send_request_receive_response(req_header, {}, resp_header, resp_payload_bytes, out_error)) {
        return false;
    }

    if (resp_header.status == NetworkProtocol::StatusCode::SUCCESS) {
        if (resp_header.type != NetworkProtocol::CommandType::LIST_TOPICS_RESPONSE) {
             out_error = "Unexpected response type for LIST_TOPICS."; return false;
        }
        try {
            size_t current_offset = 0;
            uint32_t num_topics = NetworkProtocol::read_uint32_from_buffer(resp_payload_bytes.data(), current_offset);
            out_topics.clear();
            out_topics.reserve(num_topics);
            for(uint32_t i = 0; i < num_topics; ++i) {
                out_topics.push_back(NetworkProtocol::read_string_from_buffer(resp_payload_bytes.data(), current_offset, resp_payload_bytes.size()));
            }
            return true;
        } catch (const std::exception& e) {
            out_error = "Failed to deserialize LIST_TOPICS response: " + std::string(e.what());
            return false;
        }
    } else {
         try {
            NetworkProtocol::ErrorResponsePayload err_resp = NetworkProtocol::ErrorResponsePayload::deserialize(resp_payload_bytes.data(), resp_payload_bytes.size());
            out_error = "Server error (LIST_TOPICS): " + err_resp.error_message + " (Status: " + std::to_string(static_cast<int>(resp_header.status)) + ")";
        } catch (const std::exception& e) {
            out_error = "Server error (LIST_TOPICS), and failed to parse error message. Status: " + std::to_string(static_cast<int>(resp_header.status));
        }
        return false;
    }
}
