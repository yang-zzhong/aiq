// network/WebSocketServer.h
#pragma once

#include <boost/beast/core.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <memory>
#include <string>
#include <vector>
#include <thread> // For running io_context if not shared

#include "../../event_queue_core/EventQueue.h" // The core queue logic
#include "SubscriptionManager.h"
// WebSocketSession is included in the .cpp file to avoid circular dependencies if WebSocketSession
// were to ever need something from WebSocketServer (not typical for this structure).

namespace net = boost::asio;    // from <boost/asio.hpp>
using tcp = net::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

class WebSocketServer : public std::enable_shared_from_this<WebSocketServer> {
    net::io_context& ioc_; // Reference to an external io_context
    tcp::acceptor acceptor_;
    EventQueue& event_queue_;
    std::string address_;
    SubscriptionManager& sub_manager_;
    unsigned short port_;
    
    // If the server manages its own io_context threads (optional)
    // std::vector<std::thread> io_threads_;
    // bool own_io_context_threads_ = false;


public:
    // Constructor takes a reference to an io_context.
    // This allows multiple servers (TCP, HTTP, WS) to share the same io_context and thread pool.
    WebSocketServer(net::io_context& ioc,
                    const std::string& address,
                    unsigned short port,
                    SubscriptionManager& sub_mgr,
                    EventQueue& queue);
    
    // Alternative constructor if the server is to manage its own io_context and threads
    // WebSocketServer(const std::string& address,
    //                 unsigned short port,
    //                 EventQueue& queue,
    //                 int num_threads = 1);


    ~WebSocketServer();

    // Start accepting connections
    bool run();

    // Stop accepting connections and close down
    void stop();

private:
    void do_accept();
    void on_accept(boost::beast::error_code ec, tcp::socket socket);
};