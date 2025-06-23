// network/WebSocketTypes.h
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>        // For optional fields
#include <nlohmann/json.hpp>
#include "../event_queue_core/Message.h" // Assumes Message.h has NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE

using json = nlohmann::json;

namespace WebSocketProtocol {

    // --- Command Enum ---
    // Defines the type of WebSocket message
    enum class Command {
        // Client to Server
        PRODUCE_REQUEST,
        SUBSCRIBE_TOPIC_REQUEST,
        UNSUBSCRIBE_TOPIC_REQUEST,
        CREATE_TOPIC_REQUEST,
        LIST_TOPICS_REQUEST,
        GET_NEXT_OFFSET_REQUEST,

        // Server to Client (Responses & Notifications)
        PRODUCE_RESPONSE,
        SUBSCRIBE_TOPIC_RESPONSE,   // Confirms subscription
        UNSUBSCRIBE_TOPIC_RESPONSE, // Confirms unsubscription
        CREATE_TOPIC_RESPONSE,
        LIST_TOPICS_RESPONSE,
        GET_NEXT_OFFSET_RESPONSE,
        MESSAGE_BATCH_NOTIFICATION, // Pushed messages for a subscription
        ERROR_RESPONSE,

        // Generic/Unknown
        UNKNOWN
    };

    // Helper for JSON serialization of the Command enum
    NLOHMANN_JSON_SERIALIZE_ENUM(Command, {
        {Command::PRODUCE_REQUEST, "produce_request"},
        {Command::SUBSCRIBE_TOPIC_REQUEST, "subscribe_topic_request"},
        {Command::UNSUBSCRIBE_TOPIC_REQUEST, "unsubscribe_topic_request"},
        {Command::CREATE_TOPIC_REQUEST, "create_topic_request"},
        {Command::LIST_TOPICS_REQUEST, "list_topics_request"},
        {Command::GET_NEXT_OFFSET_REQUEST, "get_next_offset_request"},
        {Command::PRODUCE_RESPONSE, "produce_response"},
        {Command::SUBSCRIBE_TOPIC_RESPONSE, "subscribe_topic_response"},
        {Command::UNSUBSCRIBE_TOPIC_RESPONSE, "unsubscribe_topic_response"},
        {Command::CREATE_TOPIC_RESPONSE, "create_topic_response"},
        {Command::LIST_TOPICS_RESPONSE, "list_topics_response"},
        {Command::GET_NEXT_OFFSET_RESPONSE, "get_next_offset_response"},
        {Command::MESSAGE_BATCH_NOTIFICATION, "message_batch_notification"},
        {Command::ERROR_RESPONSE, "error_response"},
        {Command::UNKNOWN, nullptr} // Allows parsing to UNKNOWN if string doesn't match
    })

    // --- Base Message Structure ---
    // All WebSocket messages should at least contain a command.
    // `req_id` helps correlate requests and responses.
    struct BaseWsMessage {
        Command command = Command::UNKNOWN;
        std::optional<uint64_t> req_id; // Client can set this, server echoes it back in responses
    };
    // For parsing the command first, then the full message
    // NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BaseWsMessage, command, req_id)
    inline void to_json(json& j, const BaseWsMessage& p) {
        j["command"] = p.command;
        j["req_id"] = p.req_id;
    }

    inline void from_json(const json& j, BaseWsMessage& p) {
        j.at("command").get_to(p.command);
        if (p.req_id) {
            j.at("req_id").get_to(*p.req_id);
        }
    }



    // --- Client to Server Request Structures ---

    struct ProduceWsRequest : BaseWsMessage {
        std::string topic;
        std::string message_payload; // The actual data to be stored
    };

    // NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ProduceWsRequest, command, req_id, topic, message_payload)

    inline void to_json(json& j, const ProduceWsRequest& p) {
        // 1. Serialize the base class part
        j = static_cast<const BaseWsMessage&>(p); // This uses BaseWsMessage's to_json

        // 2. Serialize the derived class's own members
        j["topic"] = p.topic;
        j["message_payload"] = p.message_payload;
    }

    inline void from_json(const json& j, ProduceWsRequest& p) {
        // 1. Deserialize the base class part
        static_cast<BaseWsMessage&>(p) = j.get<BaseWsMessage>(); // Uses BaseWsMessage's from_json

        // 2. Deserialize the derived class's own members
        // Use .at("member") for checked access (throws if missing), or find and then value()
        j.at("topic").get_to(p.topic);
        j.at("message_payload").get_to(p.message_payload);
        // Or, if "topic" might be optional in the JSON (though not in the struct here):
        // if (j.contains("topic")) {
        //     j.at("topic").get_to(p.topic);
        // } else {
        //     // Handle missing "topic" field if necessary, e.g., throw or default
        // }
    }

    struct SubscribeTopicWsRequest : BaseWsMessage {
        std::string topic;
        std::string subscriber_id; 
        uint64_t start_offset = 0; // Offset from which to start receiving messages
    };
    // NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SubscribeTopicWsRequest, command, req_id, topic, start_offset)

    inline void to_json(json& j, const SubscribeTopicWsRequest& p) {
        // 1. Serialize the base class part
        j = static_cast<const BaseWsMessage&>(p); // This uses BaseWsMessage's to_json

        // 2. Serialize the derived class's own members
        j["topic"] = p.topic;
        j["subscriber_id"] = p.subscriber_id;
        j["start_offset"] = p.start_offset;
    }

    inline void from_json(const json& j, SubscribeTopicWsRequest& p) {
        // 1. Deserialize the base class part
        static_cast<BaseWsMessage&>(p) = j.get<BaseWsMessage>(); // Uses BaseWsMessage's from_json

        // 2. Deserialize the derived class's own members
        // Use .at("member") for checked access (throws if missing), or find and then value()
        j.at("topic").get_to(p.topic);
        j.at("start_offset").get_to(p.start_offset);
        j.at("subscriber_id").get_to(p.subscriber_id);
        // Or, if "topic" might be optional in the JSON (though not in the struct here):
        // if (j.contains("topic")) {
        //     j.at("topic").get_to(p.topic);
        // } else {
        //     // Handle missing "topic" field if necessary, e.g., throw or default
        // }
    }

    struct UnsubscribeTopicWsRequest : BaseWsMessage {
        std::string topic;
        std::string subscriber_id; 
        // Optional: could also include a subscription_id if server assigns one
    };
    // NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UnsubscribeTopicWsRequest, command, req_id, topic)

    inline void to_json(json& j, const UnsubscribeTopicWsRequest& p) {
        // 1. Serialize the base class part
        j = static_cast<const BaseWsMessage&>(p); // This uses BaseWsMessage's to_json

        j["topic"] = p.topic;
        j["subscriber_id"] = p.subscriber_id;
    }

    inline void from_json(const json& j, UnsubscribeTopicWsRequest& p) {
        static_cast<BaseWsMessage&>(p) = j.get<BaseWsMessage>(); // Uses BaseWsMessage's from_json
        j.at("topic").get_to(p.topic);
        j.at("subscriber_id").get_to(p.subscriber_id);
    }

    struct CreateTopicWsRequest : BaseWsMessage {
        std::string topic;
    };
    // NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CreateTopicWsRequest, command, req_id, topic)

    inline void to_json(json& j, const CreateTopicWsRequest& p) {
        // 1. Serialize the base class part
        j = static_cast<const BaseWsMessage&>(p); // This uses BaseWsMessage's to_json

        // 2. Serialize the derived class's own members
        j["topic"] = p.topic;
    }

    inline void from_json(const json& j, CreateTopicWsRequest& p) {
        // 1. Deserialize the base class part
        static_cast<BaseWsMessage&>(p) = j.get<BaseWsMessage>(); // Uses BaseWsMessage's from_json

        // 2. Deserialize the derived class's own members
        // Use .at("member") for checked access (throws if missing), or find and then value()
        j.at("topic").get_to(p.topic);
        // Or, if "topic" might be optional in the JSON (though not in the struct here):
        // if (j.contains("topic")) {
        //     j.at("topic").get_to(p.topic);
        // } else {
        //     // Handle missing "topic" field if necessary, e.g., throw or default
        // }
    }

    // ListTopicsWsRequest can just be a BaseWsMessage with command = LIST_TOPICS_REQUEST
    // GetNextOffsetWsRequest needs a topic name
    struct GetNextOffsetWsRequest : BaseWsMessage {
        std::string topic;
    };
    // NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GetNextOffsetWsRequest, command, req_id, topic)

    inline void to_json(json& j, const GetNextOffsetWsRequest& p) {
        // 1. Serialize the base class part
        j = static_cast<const BaseWsMessage&>(p); // This uses BaseWsMessage's to_json

        // 2. Serialize the derived class's own members
        j["topic"] = p.topic;
    }

    inline void from_json(const json& j, GetNextOffsetWsRequest& p) {
        // 1. Deserialize the base class part
        static_cast<BaseWsMessage&>(p) = j.get<BaseWsMessage>(); // Uses BaseWsMessage's from_json

        // 2. Deserialize the derived class's own members
        // Use .at("member") for checked access (throws if missing), or find and then value()
        j.at("topic").get_to(p.topic);
        // Or, if "topic" might be optional in the JSON (though not in the struct here):
        // if (j.contains("topic")) {
        //     j.at("topic").get_to(p.topic);
        // } else {
        //     // Handle missing "topic" field if necessary, e.g., throw or default
        // }
    }

    // --- Server to Client Response/Notification Structures ---

    struct ProduceWsResponse : BaseWsMessage {
        std::string topic;
        uint64_t offset; // Offset of the produced message
        bool success = true;
        std::optional<std::string> error_message;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ProduceWsResponse, command, req_id, topic, offset, success, error_message)

    struct SubscribeTopicWsResponse : BaseWsMessage {
        std::string topic;
        bool success = true;
        std::optional<std::string> error_message;
        // Optional: std::string subscription_id; // Server-generated ID for this subscription
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SubscribeTopicWsResponse, command, req_id, topic, success, error_message)

    struct UnsubscribeTopicWsResponse : BaseWsMessage {
        std::string topic;
        bool success = true;
        std::optional<std::string> error_message;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UnsubscribeTopicWsResponse, command, req_id, topic, success, error_message)


    struct CreateTopicWsResponse : BaseWsMessage {
        std::string topic;
        bool success = true;
        std::optional<std::string> error_message;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CreateTopicWsResponse, command, req_id, topic, success, error_message)

    struct ListTopicsWsResponse : BaseWsMessage {
        std::vector<std::string> topics;
        bool success = true;
        std::optional<std::string> error_message;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ListTopicsWsResponse, command, req_id, topics, success, error_message)

    struct GetNextOffsetWsResponse : BaseWsMessage {
        std::string topic;
        uint64_t next_offset;
        bool success = true;
        std::optional<std::string> error_message;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GetNextOffsetWsResponse, command, req_id, topic, next_offset, success, error_message)

    struct MessageBatchWsNotification : BaseWsMessage { // This is a push, so req_id might be less relevant or tied to subscription
        std::string topic;
        std::vector<Message> messages; // Uses the core Message struct
    };
    // Note: Message struct must have NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE for this to work.
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MessageBatchWsNotification, command, req_id, topic, messages)

    struct ErrorWsResponse : BaseWsMessage {
        std::string error_message;
        std::optional<Command> original_command_type; // The command type that caused the error
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ErrorWsResponse, command, req_id, error_message, original_command_type)



} // namespace WebSocketProtocol