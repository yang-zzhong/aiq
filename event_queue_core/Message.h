// event_queue_core/Message.h
#pragma once
#include <string>
#include <cstdint>
#include <vector> // For std::vector itself, not directly for json
#include <nlohmann/json.hpp> // Make sure this is included

// Your Message struct
struct Message {
    uint64_t offset;
    std::string topic;
    std::string payload;

    Message(uint64_t off, std::string t, std::string p)
        : offset(off), topic(std::move(t)), payload(std::move(p)) {}

    // This is the crucial part for nlohmann/json
    // Or if you prefer intrusive (member functions to_json/from_json):
    // void to_json(nlohmann::json& j) const { ... }
    // void from_json(const nlohmann::json& j) { ... }
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Message, offset, topic, payload)