// network/HttpServer.cpp
#include "HttpServer.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <vector>
#include <httplib.h>
#include <nlohmann/json.hpp> 

// Helper to send JSON response
void send_json_response(httplib::Response& res, int status_code, const json& body) {
    res.status = status_code;
    res.set_content(body.dump(2), "application/json"); // dump(2) for pretty print
}

// Helper to send error response
void send_error_response(httplib::Response& res, int status_code, const std::string& error_message) {
    json err_json = {{"error", error_message}};
    send_json_response(res, status_code, err_json);
}

HttpServer::HttpServer(EventQueue& queue, const std::string& host, int port,
                       const std::string& cert_path, const std::string& key_path)
    : event_queue_(queue), host_(host), port_(port), cert_path_(cert_path), key_path_(key_path) {
    if (!cert_path_.empty() && !key_path_.empty()) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        try {
            server_ = std::make_unique<httplib::SSLServer>(cert_path_.c_str(), key_path_.c_str());
            std::cout << "HTTPS Server configured for " << host_ << ":" << port_ << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to initialize SSLServer: " << e.what() 
                      << ". Check cert/key paths and OpenSSL linkage." << std::endl;
            // Fallback to HTTP or rethrow, here we just won't have a server_
             server_ = nullptr; // Ensure server is null if SSL init fails
        }
#endif

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
        std::cout << "HTTP SSL Server not supported, start None SSL Server for " << host_ << ":" << port_ << std::endl;
#endif
    } else {
        server_ = std::make_unique<httplib::Server>();
        std::cout << "HTTP Server configured for " << host_ << ":" << port_ << std::endl;
    }
    if (server_) { // Only setup routes if server was initialized
        setup_routes();
    }
}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::is_running() const {
    return running_.load();
}

void HttpServer::setup_routes() {
    if (!server_) return;

    // --- REST API Routes ---
    server_->Post(R"(/topics/([^/]+)/produce)", [this](const httplib::Request& req, httplib::Response& res) {
        handle_produce(req, res);
    });
    server_->Get(R"(/topics/([^/]+)/consume)", [this](const httplib::Request& req, httplib::Response& res) {
        handle_consume(req, res);
    });
    server_->Post(R"(/topics/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_create_topic(req, res);
    });
    server_->Get("/topics", [this](const httplib::Request& req, httplib::Response& res) {
        handle_list_topics(req, res);
    });
    // --- SSE Route ---
    server_->Get(R"(/topics/([^/]+)/stream)", [this](const httplib::Request& req, httplib::Response& res) {
        handle_stream_topic(req, res);
    });

    // --- Error Handling ---
    server_->set_error_handler([](const httplib::Request& /*req*/, httplib::Response& res) {
        send_error_response(res, res.status, "Resource not found or method not allowed (Status: " + std::to_string(res.status) + ")");
    });
    server_->set_exception_handler([](const httplib::Request& /*req*/, httplib::Response& res, std::exception_ptr ep) {
        res.status = 500;
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception &e) {
            send_error_response(res, 500, "Internal Server Error: " + std::string(e.what()));
        } catch (...) {
            send_error_response(res, 500, "Internal Server Error: Unknown exception.");
        }
    });
}

bool HttpServer::start() {
    if (running_.load() || !server_) return false;
    
    server_thread_ = std::thread([this]() {
        running_ = true;
        std::cout << "HTTP(S) server starting listen on " << host_ << ":" << port_ << "..." << std::endl;
        if (!server_->listen(host_.c_str(), port_)) {
            std::cerr << "HTTP(S) server failed to listen on " << host_ << ":" << port_ << std::endl;
            running_ = false;
        } else {
            // This block is reached after server_->stop() is called and listen() returns.
            std::cout << "HTTP(S) server finished listening." << std::endl;
        }
        running_ = false; // Ensure it's false when thread exits
    });

    // Give the server a moment to actually start listening or fail
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return running_.load(); // Check if listen() is active
}

void HttpServer::stop() {
    if (server_ && running_.load()) {
        std::cout << "Stopping HTTP(S) server..." << std::endl;
        server_->stop(); // Signal the server to stop listening
        // running_ will be set to false by the server_thread_ upon exit
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    std::cout << "HTTP(S) Server fully stopped." << std::endl;
}

// --- Route Handlers Implementation (REST & SSE) ---

void HttpServer::handle_produce(const httplib::Request& req, httplib::Response& res) {
    std::string topic_name = req.matches[1].str();
    if (topic_name.empty()) return send_error_response(res, 400, "Topic name missing.");

    json req_body;
    try {
        req_body = json::parse(req.body);
    } catch (json::parse_error& e) {
        return send_error_response(res, 400, "Invalid JSON: " + std::string(e.what()));
    }

    if (!req_body.contains("payload") || !req_body["payload"].is_string()) {
        return send_error_response(res, 400, "Missing 'payload' string in JSON.");
    }
    std::string message_payload = req_body["payload"];

    try {
        uint64_t offset = event_queue_.produce(topic_name, message_payload);
        send_json_response(res, 201, {{"topic", topic_name}, {"offset", offset}});
    } catch (const std::exception& e) {
        send_error_response(res, 500, e.what());
    }
}

void HttpServer::handle_consume(const httplib::Request& req, httplib::Response& res) {
    std::string topic_name = req.matches[1].str();
    if (topic_name.empty()) return send_error_response(res, 400, "Topic name missing.");

    uint64_t start_offset = req.has_param("offset") ? std::stoull(req.get_param_value("offset")) : 0;
    uint32_t max_messages = req.has_param("max_messages") ? std::stoi(req.get_param_value("max_messages")) : 100;
    max_messages = std::min(max_messages, (uint32_t)1000); // Cap max messages

    try {
        std::vector<Message> messages = event_queue_.consume(topic_name, start_offset, max_messages);
        send_json_response(res, 200, messages); // Uses Message's NLOHMANN_DEFINE
    } catch (const std::exception& e) {
        send_error_response(res, 500, e.what());
    }
}

void HttpServer::handle_create_topic(const httplib::Request& req, httplib::Response& res) {
    std::string topic_name = req.matches[1].str();
    if (topic_name.empty()) return send_error_response(res, 400, "Topic name missing.");
    try {
        event_queue_.create_topic(topic_name);
        send_json_response(res, 201, {{"topic", topic_name}, {"status", "created_or_exists"}});
    } catch (const std::exception& e) {
        send_error_response(res, 500, e.what());
    }
}

void HttpServer::handle_list_topics(const httplib::Request& req, httplib::Response& res) {
    try {
        send_json_response(res, 200, event_queue_.list_topics());
    } catch (const std::exception& e) {
        send_error_response(res, 500, e.what());
    }
}

// SSE Handler (Simple Polling - needs improvement for production)
void HttpServer::handle_stream_topic(const httplib::Request& req, httplib::Response& res) {
    std::string topic_name = req.matches[1].str();
    if (topic_name.empty()) return send_error_response(res, 400, "Topic name missing.");

    uint64_t current_offset = 0;
    if (req.has_param("offset")) {
        try { current_offset = std::stoull(req.get_param_value("offset")); }
        catch (...) { return send_error_response(res, 400, "Invalid 'offset'."); }
    } else if (req.has_header("Last-Event-ID")) {
        try { current_offset = std::stoull(req.get_header_value("Last-Event-ID")) + 1; }
        catch (...) { /* use default */ }
    }

    // Generate a unique subscriber ID for this SSE stream
    // This could be simpler for SSE as it's one request-response cycle potentially.
    // Or could use a session cookie if you have HTTP sessions.
    static std::atomic<uint64_t> sse_client_id_counter{0};
    std::string sse_subscriber_id = "sse_client_" + topic_name + "_" + std::to_string(sse_client_id_counter++);

    std::cout << "SSE stream [" << sse_subscriber_id << "]: topic='" << topic_name
              << "', start_offset=" << current_offset << std::endl;

    res.set_chunked_content_provider(
        "text/event-stream",
        // on_producer lambda
        [this, topic_name, initial_offset = current_offset, sse_subscriber_id ]
        (size_t /*user_offset*/, httplib::DataSink& sink) mutable -> bool {
            uint64_t& stream_offset = initial_offset; // Effectively capture by reference

            std::vector<Message> messages;
            try {
                messages = event_queue_.consume(topic_name, stream_offset, 10); // Poll for up to 10 messages
            } catch (const std::exception& e) {
                std::cerr << "SSE consume error for topic " << topic_name << ": " << e.what() << std::endl;
                return false; // Stop streaming
            }

            if (!messages.empty()) {
                std::stringstream ss;
                for (const auto& msg : messages) {
                    ss << "id: " << msg.offset << "\n";
                    ss << "event: message\n";
                    ss << "data: " << json(msg).dump() << "\n\n"; // Serialize Message to JSON
                    stream_offset = msg.offset + 1;
                }
                std::string data_chunk = ss.str();
                if (!sink.write(data_chunk.data(), data_chunk.length())) return false; // Client disconnected
            } else {
                // No new messages, wait a bit before polling again
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                // Optionally send a keep-alive comment:
                // std::string keep_alive = ": keep-alive\n\n";
                // if (!sink.write(keep_alive.data(), keep_alive.length())) return false;
            }
            return sink.is_writable(); // Continue if client is connected
        },
        // on_cleanup (optional)
        [topic_name, sse_subscriber_id](bool success) {
            std::cout << "SSE stream for topic '" << topic_name << "' with ID '" << sse_subscriber_id
                      << (success ? "' completed/closed." : "' failed/aborted.") << std::endl;
        }
    );
    // Set necessary headers for SSE
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    // res.set_header("Access-Control-Allow-Origin", "*"); // If CORS is needed
}