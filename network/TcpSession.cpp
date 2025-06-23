// network/TcpSession.cpp
#include "TcpSession.h"
#include <iostream>
#include <boost/asio/read.hpp> // For boost::asio::async_read
#include <boost/asio/write.hpp> // For boost::asio::async_write

TcpSession::TcpSession(tcp::socket socket, EventQueue& event_queue)
    : socket_(std::move(socket)), event_queue_(event_queue), read_buffer_(NetworkProtocol::RequestHeader::SIZE) {}

void TcpSession::start() {
    std::cout << "New session started with " << socket_.remote_endpoint() << std::endl;
    do_read_header();
}

void TcpSession::do_read_header() {
    auto self = shared_from_this();
    boost::asio::async_read(socket_, boost::asio::buffer(read_buffer_),
        [this, self](boost::system::error_code ec, std::size_t length) {
        if (!ec) {
            if (length != NetworkProtocol::RequestHeader::SIZE) {
                 std::cerr << "Session " << socket_.remote_endpoint() << ": Read incomplete header. Expected " 
                           << NetworkProtocol::RequestHeader::SIZE << " got " << length << std::endl;
                // Consider closing socket
                return;
            }
            NetworkProtocol::RequestHeader req_header = NetworkProtocol::RequestHeader::deserialize(read_buffer_.data());
            
            if (req_header.payload_length > NetworkProtocol::MAX_PAYLOAD_SIZE) {
                std::cerr << "Session " << socket_.remote_endpoint() << ": Payload too large: " << req_header.payload_length << std::endl;
                send_error_response(req_header.type, NetworkProtocol::StatusCode::ERROR_PAYLOAD_TOO_LARGE, "Request payload too large.");
                // Optionally close connection or just wait for next request
                // For now, let's try to read next header to be resilient if client sends bad data then recovers
                // do_read_header(); 
                // Or more robust: close the connection
                return; // This will drop the session
            }
            
            if (req_header.payload_length == 0) { // No payload to read
                handle_request(req_header, {});
            } else {
                do_read_payload(req_header);
            }
        } else {
            if (ec == boost::asio::error::eof) {
                std::cout << "Session " << socket_.remote_endpoint() << ": Client closed connection." << std::endl;
            } else if (ec) {
                std::cerr << "Session " << socket_.remote_endpoint() << ": Read header error: " << ec.message() << std::endl;
            }
            // Connection closed or error, session ends.
        }
    });
}

void TcpSession::do_read_payload(NetworkProtocol::RequestHeader req_header) {
    payload_read_buffer_.resize(req_header.payload_length); // Resize buffer for payload
    auto self = shared_from_this();
    boost::asio::async_read(socket_, boost::asio::buffer(payload_read_buffer_),
        [this, self, req_header](boost::system::error_code ec, std::size_t length) {
        if (!ec) {
            if (length != req_header.payload_length) {
                std::cerr << "Session " << socket_.remote_endpoint() << ": Read incomplete payload. Expected " 
                          << req_header.payload_length << " got " << length << std::endl;
                return; // Connection error, session ends
            }
            handle_request(req_header, payload_read_buffer_);
        } else {
             if (ec == boost::asio::error::eof) {
                std::cout << "Session " << socket_.remote_endpoint() << ": Client closed connection during payload read." << std::endl;
            } else {
                std::cerr << "Session " << socket_.remote_endpoint() << ": Read payload error: " << ec.message() << std::endl;
            }
            // Connection closed or error, session ends.
        }
    });
}

void TcpSession::handle_request(NetworkProtocol::RequestHeader req_header, const std::vector<char>& payload_data) {
    // Process the request based on req_header.type
    // Call event_queue_ methods, then send response
    // Example for PRODUCE:
    try {
        switch (req_header.type) {
            case NetworkProtocol::CommandType::PRODUCE_REQUEST: {
                NetworkProtocol::ProduceRequest req = NetworkProtocol::ProduceRequest::deserialize(payload_data.data(), payload_data.size());
                uint64_t offset = event_queue_.produce(req.topic_name, req.message_payload);
                
                NetworkProtocol::ProduceResponse resp_payload_struct;
                resp_payload_struct.offset = offset;
                std::vector<char> resp_payload = resp_payload_struct.serialize();
                
                send_response(NetworkProtocol::CommandType::PRODUCE_RESPONSE, NetworkProtocol::StatusCode::SUCCESS, resp_payload);
                break;
            }
            case NetworkProtocol::CommandType::CONSUME_REQUEST: {
                NetworkProtocol::ConsumeRequest req = NetworkProtocol::ConsumeRequest::deserialize(payload_data.data(), payload_data.size());
                std::vector<Message> messages = event_queue_.consume(req.topic_name, req.start_offset, req.max_messages);
                
                NetworkProtocol::ConsumeResponse resp_payload_struct;
                resp_payload_struct.messages = std::move(messages); // Assuming Message struct is compatible
                std::vector<char> resp_payload = resp_payload_struct.serialize();

                send_response(NetworkProtocol::CommandType::CONSUME_RESPONSE, NetworkProtocol::StatusCode::SUCCESS, resp_payload);
                break;
            }
            case NetworkProtocol::CommandType::GET_TOPIC_OFFSET_REQUEST: {
                 // Similar deserialization for topic name
                 size_t offset = 0;
                 std::string topic_name = NetworkProtocol::read_string_from_buffer(payload_data.data(), offset, payload_data.size());

                 uint64_t next_offset = event_queue_.get_next_topic_offset(topic_name);
                 std::vector<char> resp_payload;
                 NetworkProtocol::write_uint64_to_buffer(resp_payload, next_offset);
                 send_response(NetworkProtocol::CommandType::GET_TOPIC_OFFSET_RESPONSE, NetworkProtocol::StatusCode::SUCCESS, resp_payload);
                break;
            }
             case NetworkProtocol::CommandType::CREATE_TOPIC_REQUEST: {
                 size_t offset = 0;
                 std::string topic_name = NetworkProtocol::read_string_from_buffer(payload_data.data(), offset, payload_data.size());
                 bool success = event_queue_.create_topic(topic_name);
                 if(success) {
                    send_response(NetworkProtocol::CommandType::CREATE_TOPIC_RESPONSE, NetworkProtocol::StatusCode::SUCCESS);
                 } else {
                    send_error_response(req_header.type, NetworkProtocol::StatusCode::ERROR_INTERNAL_SERVER, "Failed to create topic.");
                 }
                break;
            }
            case NetworkProtocol::CommandType::LIST_TOPICS_REQUEST: {
                std::vector<std::string> topics = event_queue_.list_topics();
                std::vector<char> resp_payload;
                NetworkProtocol::write_uint32_to_buffer(resp_payload, static_cast<uint32_t>(topics.size()));
                for(const auto& t_name : topics) {
                    NetworkProtocol::write_string_to_buffer(resp_payload, t_name);
                }
                send_response(NetworkProtocol::CommandType::LIST_TOPICS_RESPONSE, NetworkProtocol::StatusCode::SUCCESS, resp_payload);
                break;
            }
            // ... other command types
            default:
                std::cerr << "Session " << socket_.remote_endpoint() << ": Unknown command type: " << static_cast<int>(req_header.type) << std::endl;
                send_error_response(req_header.type, NetworkProtocol::StatusCode::ERROR_UNKNOWN_COMMAND, "Unknown command type.");
                break;
        }
    } catch (const std::invalid_argument& e) {
        std::cerr << "Session " << socket_.remote_endpoint() << ": Invalid argument: " << e.what() << std::endl;
        send_error_response(req_header.type, NetworkProtocol::StatusCode::ERROR_INVALID_REQUEST, e.what());
    } catch (const std::runtime_error& e) { // Catch serialization/deserialization errors or other EQ errors
        std::cerr << "Session " << socket_.remote_endpoint() << ": Runtime error: " << e.what() << std::endl;
        // Determine if it's a client-side (serialization) or server-side error
        // For now, generic internal server error, or could be more specific.
        if (std::string(e.what()).find("consume entire payload") != std::string::npos ||
            std::string(e.what()).find("String length exceeds") != std::string::npos ||
            std::string(e.what()).find("Reported string length") != std::string::npos) {
            send_error_response(req_header.type, NetworkProtocol::StatusCode::ERROR_SERIALIZATION, e.what());
        } else if (std::string(e.what()).find("Topic not found") != std::string::npos) {
             send_error_response(req_header.type, NetworkProtocol::StatusCode::ERROR_TOPIC_NOT_FOUND, e.what());
        }
        else {
            send_error_response(req_header.type, NetworkProtocol::StatusCode::ERROR_INTERNAL_SERVER, e.what());
        }
    } catch (const std::exception& e) {
        std::cerr << "Session " << socket_.remote_endpoint() << ": Unhandled exception: " << e.what() << std::endl;
        send_error_response(req_header.type, NetworkProtocol::StatusCode::ERROR_INTERNAL_SERVER, "An unexpected error occurred.");
    }
    // After handling, read the next header
    // do_read_header(); // This is crucial to keep the session alive for more requests
}


void TcpSession::send_response(NetworkProtocol::CommandType response_cmd_type,
                               NetworkProtocol::StatusCode status,
                               const std::vector<char>& payload) {
    NetworkProtocol::ResponseHeader resp_header;
    resp_header.type = response_cmd_type;
    resp_header.status = status;
    resp_header.payload_length = static_cast<uint32_t>(payload.size());

    std::vector<char> header_bytes = resp_header.serialize();
    
    std::vector<boost::asio::const_buffer> buffers_to_send;
    buffers_to_send.push_back(boost::asio::buffer(header_bytes));
    if (!payload.empty()) {
        buffers_to_send.push_back(boost::asio::buffer(payload));
    }

    auto self = shared_from_this();
    boost::asio::async_write(socket_, buffers_to_send,
        [this, self, response_cmd_type, status](boost::system::error_code ec, std::size_t /*length*/) {
        if (!ec) {
            // Successfully sent response, now wait for the next request from the client
            if (status == NetworkProtocol::StatusCode::SUCCESS) {
                 std::cout << "Session " << socket_.remote_endpoint() << ": Sent response type " << static_cast<int>(response_cmd_type) << ", status SUCCESS." << std::endl;
            } else {
                 std::cout << "Session " << socket_.remote_endpoint() << ": Sent response type " << static_cast<int>(response_cmd_type) << ", status ERROR " << static_cast<int>(status) << "." << std::endl;
            }
            do_read_header();
        } else {
            std::cerr << "Session " << socket_.remote_endpoint() << ": Write response error: " << ec.message() << std::endl;
            // Connection error, session ends.
        }
    });
}

void TcpSession::send_error_response(NetworkProtocol::CommandType original_request_type,
                                     NetworkProtocol::StatusCode status_code,
                                     const std::string& error_message) {
    NetworkProtocol::ErrorResponsePayload err_payload_struct;
    err_payload_struct.error_message = error_message;
    std::vector<char> err_payload_bytes = err_payload_struct.serialize();

    send_response(NetworkProtocol::CommandType::ERROR_RESPONSE, status_code, err_payload_bytes);
    // Note: The send_response itself will call do_read_header() if successful,
    // so the session might continue or close depending on the error severity and client behavior.
    // If the error is critical (e.g., payload too large, unknown command), you might want to explicitly close the socket.
}
