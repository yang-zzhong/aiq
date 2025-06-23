// network/TcpServer.cpp
#include "TcpServer.h"
#include "TcpSession.h" // Include TcpSession
#include <iostream>

TcpServer::TcpServer(boost::asio::io_context& io_context, short port, EventQueue& event_queue)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), event_queue_(event_queue) {
    std::cout << "TCP Server listening on port " << port << std::endl;
    do_accept();
}

void TcpServer::do_accept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
        if (!ec) {
            // Create a new session and start it
            std::make_shared<TcpSession>(std::move(socket), event_queue_)->start();
        } else {
            std::cerr << "Server accept error: " << ec.message() << std::endl;
        }
        // Continue accepting new connections
        do_accept();
    });
}
