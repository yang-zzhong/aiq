// network/NetworkProtocol.h
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept> // For runtime_error
#include <iostream>  // For potential debug/error prints
#include <boost/asio/buffer.hpp>
#include "../event_queue_core/BinaryUtils.h" // Re-use for basic types if possible
#include "../event_queue_core/Message.h"     // For Message struct

// Forward declare to avoid circular dependency if BinaryUtils needs NetworkProtocol later
// (not the case here but good practice)

namespace NetworkProtocol {

    // Max payload size for a single network message (e.g., 64MB)
    const uint32_t MAX_PAYLOAD_SIZE = 64 * 1024 * 1024;

    enum class CommandType : uint8_t {
        PRODUCE_REQUEST = 0x01,
        CONSUME_REQUEST = 0x02,
        GET_TOPIC_OFFSET_REQUEST = 0x03,
        CREATE_TOPIC_REQUEST = 0x04,
        LIST_TOPICS_REQUEST = 0x05,
        // Responses will implicitly match request types or use a generic response type
        PRODUCE_RESPONSE = 0x81,
        CONSUME_RESPONSE = 0x82,
        GET_TOPIC_OFFSET_RESPONSE = 0x83,
        CREATE_TOPIC_RESPONSE = 0x84,
        LIST_TOPICS_RESPONSE = 0x85,
        ERROR_RESPONSE = 0xFF
    };

    enum class StatusCode : uint8_t {
        SUCCESS = 0x00,
        ERROR_TOPIC_NOT_FOUND = 0x01,
        ERROR_INVALID_OFFSET = 0x02,
        ERROR_SERIALIZATION = 0x03, // Problem forming/parsing message
        ERROR_PRODUCE_FAILED = 0x04,
        ERROR_INTERNAL_SERVER = 0x05,
        ERROR_INVALID_REQUEST = 0x06,
        ERROR_PAYLOAD_TOO_LARGE = 0x07,
        ERROR_UNKNOWN_COMMAND = 0x08
    };

    struct RequestHeader {
        CommandType type;
        uint32_t payload_length;

        static const size_t SIZE = sizeof(CommandType) + sizeof(uint32_t);

        std::vector<char> serialize() const {
            std::vector<char> buffer(SIZE);
            size_t offset = 0;
            buffer[offset] = static_cast<uint8_t>(type);
            offset += sizeof(CommandType);
            uint32_t net_payload_length = htonl(payload_length); // Network byte order
            std::memcpy(buffer.data() + offset, &net_payload_length, sizeof(uint32_t));
            return buffer;
        }

        static RequestHeader deserialize(const char* data) {
            RequestHeader header;
            size_t offset = 0;
            header.type = static_cast<CommandType>(data[offset]);
            offset += sizeof(CommandType);
            uint32_t net_payload_length;
            std::memcpy(&net_payload_length, data + offset, sizeof(uint32_t));
            header.payload_length = ntohl(net_payload_length); // Host byte order
            return header;
        }
    };

    struct ResponseHeader {
        CommandType type; // Echo request type or specific response type
        StatusCode status;
        uint32_t payload_length;

        static const size_t SIZE = sizeof(CommandType) + sizeof(StatusCode) + sizeof(uint32_t);

        std::vector<char> serialize() const {
            std::vector<char> buffer(SIZE);
            size_t offset = 0;
            buffer[offset] = static_cast<uint8_t>(type);
            offset += sizeof(CommandType);
            buffer[offset] = static_cast<uint8_t>(status);
            offset += sizeof(StatusCode);
            uint32_t net_payload_length = htonl(payload_length);
            std::memcpy(buffer.data() + offset, &net_payload_length, sizeof(uint32_t));
            return buffer;
        }

        static ResponseHeader deserialize(const char* data) {
            ResponseHeader header;
            size_t offset = 0;
            header.type = static_cast<CommandType>(data[offset]);
            offset += sizeof(CommandType);
            header.status = static_cast<StatusCode>(data[offset]);
            offset += sizeof(StatusCode);
            uint32_t net_payload_length;
            std::memcpy(&net_payload_length, data + offset, sizeof(uint32_t));
            header.payload_length = ntohl(net_payload_length);
            return header;
        }
    };

    // --- Serialization Helpers for common types ---
    // (Could be moved to a separate NetworkBinaryUtils or extend BinaryUtils)

    inline void write_uint16_to_buffer(std::vector<char>& buffer, uint16_t val) {
        uint16_t net_val = htons(val);
        const char* p = reinterpret_cast<const char*>(&net_val);
        buffer.insert(buffer.end(), p, p + sizeof(uint16_t));
    }

    inline uint16_t read_uint16_from_buffer(const char* data, size_t& offset) {
        uint16_t net_val;
        std::memcpy(&net_val, data + offset, sizeof(uint16_t));
        offset += sizeof(uint16_t);
        return ntohs(net_val);
    }
    
    inline void write_uint32_to_buffer(std::vector<char>& buffer, uint32_t val) {
        uint32_t net_val = htonl(val);
        const char* p = reinterpret_cast<const char*>(&net_val);
        buffer.insert(buffer.end(), p, p + sizeof(uint32_t));
    }

    inline uint32_t read_uint32_from_buffer(const char* data, size_t& offset) {
        uint32_t net_val;
        std::memcpy(&net_val, data + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        return ntohl(net_val);
    }

    inline void write_uint64_to_buffer(std::vector<char>& buffer, uint64_t val) {
        // Convert to network byte order (manual for 64-bit for portability if needed, or use htobe64/be64toh if available)
        // For simplicity, assuming host and network might be same or using a library that handles it.
        // Boost.Asio typically doesn't do this automatically for custom protocols.
        // Let's do manual byte swapping for big-endian (network standard)
        uint64_t net_val = 0;
        if (htons(1) == 1) { // Host is big-endian
            net_val = val;
        } else { // Host is little-endian, swap
            net_val = (((uint64_t)htonl(val & 0xFFFFFFFF)) << 32) | htonl(val >> 32);
        }
        const char* p = reinterpret_cast<const char*>(&net_val);
        buffer.insert(buffer.end(), p, p + sizeof(uint64_t));
    }

    inline uint64_t read_uint64_from_buffer(const char* data, size_t& offset) {
        uint64_t net_val;
        std::memcpy(&net_val, data + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        uint64_t host_val = 0;
         if (htons(1) == 1) { // Host is big-endian
            host_val = net_val;
        } else { // Host is little-endian, swap
            host_val = (((uint64_t)ntohl(net_val & 0xFFFFFFFF)) << 32) | ntohl(net_val >> 32);
        }
        return host_val;
    }

    inline void write_string_to_buffer(std::vector<char>& buffer, const std::string& str, bool use_uint16_len = true) {
        if (use_uint16_len) {
            if (str.length() > UINT16_MAX) throw std::runtime_error("String too long for uint16_t length.");
            write_uint16_to_buffer(buffer, static_cast<uint16_t>(str.length()));
        } else {
            if (str.length() > UINT32_MAX) throw std::runtime_error("String too long for uint32_t length.");
            write_uint32_to_buffer(buffer, static_cast<uint32_t>(str.length()));
        }
        buffer.insert(buffer.end(), str.begin(), str.end());
    }

    inline std::string read_string_from_buffer(const char* data, size_t& offset, size_t total_payload_size, bool use_uint16_len = true) {
        uint32_t len; // Use uint32_t to hold length read from either uint16 or uint32
        if (use_uint16_len) {
            len = read_uint16_from_buffer(data, offset);
        } else {
            len = read_uint32_from_buffer(data, offset);
        }
        if (offset + len > total_payload_size) { // Boundary check
             throw std::runtime_error("String length exceeds payload boundary.");
        }
        if (len > MAX_PAYLOAD_SIZE) { // Sanity check, though payload_length itself should be checked first
            throw std::runtime_error("Reported string length is too large.");
        }
        std::string str(data + offset, len);
        offset += len;
        return str;
    }

    // Specific request/response structures (Payloads)

    // PRODUCE
    struct ProduceRequest {
        std::string topic_name;
        std::string message_payload;

        std::vector<char> serialize() const {
            std::vector<char> payload_buffer;
            write_string_to_buffer(payload_buffer, topic_name); // default uint16_t length
            write_string_to_buffer(payload_buffer, message_payload, false); // uint32_t length for payload
            return payload_buffer;
        }
        static ProduceRequest deserialize(const char* data, size_t payload_len) {
            ProduceRequest req;
            size_t offset = 0;
            req.topic_name = read_string_from_buffer(data, offset, payload_len);
            req.message_payload = read_string_from_buffer(data, offset, payload_len, false);
            if (offset != payload_len) throw std::runtime_error("ProduceRequest: Did not consume entire payload.");
            return req;
        }
    };
    struct ProduceResponse { // Payload for success
        uint64_t offset;
        std::vector<char> serialize() const {
            std::vector<char> payload_buffer;
            write_uint64_to_buffer(payload_buffer, offset);
            return payload_buffer;
        }
        static ProduceResponse deserialize(const char* data, size_t payload_len) {
            ProduceResponse res;
            size_t offset = 0;
            res.offset = read_uint64_from_buffer(data, offset);
             if (offset != payload_len) throw std::runtime_error("ProduceResponse: Did not consume entire payload.");
            return res;
        }
    };

    // CONSUME
    struct ConsumeRequest {
        std::string topic_name;
        uint64_t start_offset;
        uint32_t max_messages;
        std::vector<char> serialize() const {
            std::vector<char> payload_buffer;
            write_string_to_buffer(payload_buffer, topic_name);
            write_uint64_to_buffer(payload_buffer, start_offset);
            write_uint32_to_buffer(payload_buffer, max_messages);
            return payload_buffer;
        }
        static ConsumeRequest deserialize(const char* data, size_t payload_len) {
            ConsumeRequest req;
            size_t offset = 0;
            req.topic_name = read_string_from_buffer(data, offset, payload_len);
            req.start_offset = read_uint64_from_buffer(data, offset);
            req.max_messages = read_uint32_from_buffer(data, offset);
            if (offset != payload_len) throw std::runtime_error("ConsumeRequest: Did not consume entire payload.");
            return req;
        }
    };
    struct ConsumeResponse { // Payload for success
        std::vector<Message> messages; // Original Message struct from event_queue_core
        std::vector<char> serialize() const {
            std::vector<char> payload_buffer;
            write_uint32_to_buffer(payload_buffer, static_cast<uint32_t>(messages.size()));
            for (const auto& msg : messages) {
                write_uint64_to_buffer(payload_buffer, msg.offset);
                // Topic name is context, not part of individual message payload in this response
                write_string_to_buffer(payload_buffer, msg.payload, false); // uint32_t length for payload
            }
            return payload_buffer;
        }
        static ConsumeResponse deserialize(const char* data, size_t payload_len, const std::string& topic_name_context) {
            ConsumeResponse res;
            size_t offset = 0;
            uint32_t num_messages = read_uint32_from_buffer(data, offset);
            res.messages.reserve(num_messages);
            for (uint32_t i = 0; i < num_messages; ++i) {
                uint64_t msg_offset = read_uint64_from_buffer(data, offset);
                std::string msg_payload = read_string_from_buffer(data, offset, payload_len, false);
                res.messages.emplace_back(msg_offset, topic_name_context, msg_payload);
            }
            if (offset != payload_len && num_messages > 0) { // If num_messages is 0, offset will be just after num_messages read
                 if (offset != payload_len) throw std::runtime_error("ConsumeResponse: Did not consume entire payload.");
            } else if (num_messages == 0 && offset != sizeof(uint32_t)) {
                 throw std::runtime_error("ConsumeResponse: Payload mismatch for zero messages.");
            }
            return res;
        }
    };

    // Generic Error Response
    struct ErrorResponsePayload {
        std::string error_message;
        std::vector<char> serialize() const {
            std::vector<char> payload_buffer;
            write_string_to_buffer(payload_buffer, error_message, false); // uint32_t for error message
            return payload_buffer;
        }
        static ErrorResponsePayload deserialize(const char* data, size_t payload_len) {
            ErrorResponsePayload err_res;
            size_t offset = 0;
            err_res.error_message = read_string_from_buffer(data, offset, payload_len, false);
            if (offset != payload_len) throw std::runtime_error("ErrorResponse: Did not consume entire payload.");
            return err_res;
        }
    };


} // namespace NetworkProtocol
