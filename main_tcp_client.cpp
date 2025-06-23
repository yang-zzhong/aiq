// main_client.cpp
#include "client/TcpClient.h"
#include <boost/asio/io_context.hpp>
#include <iostream>
#include <thread> // For std::this_thread::sleep_for
#include <chrono> // For std::chrono::milliseconds

void run_client_tests(const std::string& host, short port) {
    boost::asio::io_context client_io_context;
    TcpClient client(client_io_context, host, port);

    if (!client.connect()) {
        std::cerr << "Client: Failed to connect to server." << std::endl;
        return;
    }

    std::string error_msg;
    uint64_t returned_offset;
    std::vector<Message> messages;
    std::vector<std::string> topics_list;

    std::cout << "\n--- Client Test: Create Topics ---" << std::endl;
    if (client.create_topic("orders_topic_net", error_msg)) {
        std::cout << "Client: Successfully created topic 'orders_topic_net'." << std::endl;
    } else {
        std::cout << "Client: Failed to create topic 'orders_topic_net': " << error_msg << std::endl;
    }
    client.create_topic("logs_topic_net", error_msg); // Ignore result for brevity

    std::cout << "\n--- Client Test: List Topics ---" << std::endl;
    if (client.list_topics(topics_list, error_msg)) {
        std::cout << "Client: Available topics:" << std::endl;
        for(const auto& t : topics_list) {
            std::cout << " - " << t << std::endl;
        }
    } else {
        std::cout << "Client: Failed to list topics: " << error_msg << std::endl;
    }


    std::cout << "\n--- Client Test: Produce ---" << std::endl;
    if (client.produce("orders_topic_net", "Network Order A1", returned_offset, error_msg)) {
        std::cout << "Client: Produced to 'orders_topic_net' at offset " << returned_offset << std::endl;
    } else {
        std::cout << "Client: Produce failed: " << error_msg << std::endl;
    }

    if (client.produce("orders_topic_net", "Network Order B2", returned_offset, error_msg)) {
        std::cout << "Client: Produced to 'orders_topic_net' at offset " << returned_offset << std::endl;
    } else {
        std::cout << "Client: Produce failed: " << error_msg << std::endl;
    }
    
    if (client.produce("logs_topic_net", "System Event XYZ", returned_offset, error_msg)) {
        std::cout << "Client: Produced to 'logs_topic_net' at offset " << returned_offset << std::endl;
    } else {
        std::cout << "Client: Produce failed: " << error_msg << std::endl;
    }


    std::cout << "\n--- Client Test: Get Topic Offset ---" << std::endl;
    if (client.get_topic_offset("orders_topic_net", returned_offset, error_msg)) {
        std::cout << "Client: Next offset for 'orders_topic_net' is " << returned_offset << std::endl;
    } else {
        std::cout << "Client: Get topic offset failed: " << error_msg << std::endl;
    }


    std::cout << "\n--- Client Test: Consume (all from 'orders_topic_net') ---" << std::endl;
    if (client.consume("orders_topic_net", 0, 10, messages, error_msg)) {
        std::cout << "Client: Consumed from 'orders_topic_net':" << std::endl;
        for (const auto& msg : messages) {
            std::cout << "  Offset " << msg.offset << ": " << msg.payload << std::endl;
        }
    } else {
        std::cout << "Client: Consume failed: " << error_msg << std::endl;
    }

    std::cout << "\n--- Client Test: Consume (from offset 1 in 'orders_topic_net') ---" << std::endl;
    if (client.consume("orders_topic_net", 1, 10, messages, error_msg)) {
        std::cout << "Client: Consumed from 'orders_topic_net' (offset 1+):" << std::endl;
        for (const auto& msg : messages) {
            std::cout << "  Offset " << msg.offset << ": " << msg.payload << std::endl;
        }
    } else {
        std::cout << "Client: Consume failed: " << error_msg << std::endl;
    }

    std::cout << "\n--- Client Test: Consume from non-existent topic ---" << std::endl;
    if (client.consume("no_such_topic_net", 0, 10, messages, error_msg)) {
        if (messages.empty()) {
            std::cout << "Client: Correctly received no messages for 'no_such_topic_net'." << std::endl;
        } else {
             std::cout << "Client: ERROR! Received messages for 'no_such_topic_net'." << std::endl;
        }
    } else {
        std::cout << "Client: Consume for 'no_such_topic_net' correctly resulted in error: " << error_msg << std::endl;
    }
    
    std::cout << "\n--- Client Test: Produce to topic with special chars (ensure string handling is robust) ---" << std::endl;
    std::string special_topic = "topic with spaces & symbols !@#$%^&*()_+";
    std::string special_payload = "payload with newline\nand tabs\t\t and null char\0 in middle (will be truncated by string).";
    // Note: std::string will truncate at first null. For binary data, use std::vector<char> and handle length explicitly.
    // Our string serialization handles length, so it should be fine up to the server.
    
    client.create_topic(special_topic, error_msg); // Create it first

    if (client.produce(special_topic, special_payload.substr(0, special_payload.find('\0')), returned_offset, error_msg)) {
        std::cout << "Client: Produced to '" << special_topic << "' at offset " << returned_offset << std::endl;
        if (client.consume(special_topic, returned_offset, 1, messages, error_msg)) {
             for (const auto& msg : messages) {
                std::cout << "  Consumed back: Offset " << msg.offset << ", Payload: \"" << msg.payload << "\"" << std::endl;
             }
        } else {
            std::cout << "Client: Consume failed for special topic: " << error_msg << std::endl;
        }
    } else {
        std::cout << "Client: Produce failed for special topic: " << error_msg << std::endl;
    }


    client.disconnect();
}


int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    short port = 12345;

    if (argc > 1) host = argv[1];
    if (argc > 2) port = static_cast<short>(std::atoi(argv[2]));

    try {
        run_client_tests(host, port);
    } catch (const std::exception& e) {
        std::cerr << "Client main exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
