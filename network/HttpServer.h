// network/HttpServer.h
#pragma once

#include "../event_queue_core/EventQueue.h"
#include <httplib.h> // cpp-httplib
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>
#include "SubscriptionManager.h"

using json = nlohmann::json;

class HttpServer {
public:
    HttpServer(EventQueue& queue, const std::string& host, int port,
               const std::string& cert_path = "", const std::string& key_path = "");
    ~HttpServer();

    bool start();
    void stop();
    bool is_running() const;

private:
    void setup_routes();
    // --- REST API Handlers ---
    void handle_produce(const httplib::Request& req, httplib::Response& res);
    void handle_consume(const httplib::Request& req, httplib::Response& res);
    void handle_create_topic(const httplib::Request& req, httplib::Response& res);
    void handle_list_topics(const httplib::Request& req, httplib::Response& res);
    void handle_get_next_offset(const httplib::Request& req, httplib::Response& res);
    
    // --- SSE Handler ---
    void handle_stream_topic(const httplib::Request& req, httplib::Response& res);

    EventQueue& event_queue_;
    std::string host_;
    int port_;
    std::string cert_path_;
    std::string key_path_;

    std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;
    std::atomic<bool> running_{false};
};