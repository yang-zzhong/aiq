// network/TcpServer.h
#pragma once
#include <boost/asio.hpp>
#include <memory>
#include "../event_queue_core/EventQueue.h" // The actual queue

using boost::asio::ip::tcp;

class TcpServer {
public:
    TcpServer(boost::asio::io_context& io_context, short port, EventQueue& event_queue);

private:
    void do_accept();

    tcp::acceptor acceptor_;
    EventQueue& event_queue_; // Reference to the shared event queue
};
