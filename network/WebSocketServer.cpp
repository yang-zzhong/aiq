// network/WebSocketServer.cpp
#include "WebSocketServer.h"
#include "WebSocketSession.h" // Include the session handler
#include <iostream>

WebSocketServer::WebSocketServer(
    net::io_context& ioc,
    const std::string& address,
    unsigned short port,
    SubscriptionManager & sub_mgr,
    EventQueue& queue)
    : ioc_(ioc),
      acceptor_(net::make_strand(ioc)), // Create acceptor on a strand of the provided io_context
      event_queue_(queue),
      address_(address),
      sub_manager_(sub_mgr),
      port_(port)
{
    std::cout << "WebSocketServer: Initializing on io_context " << &ioc_ << std::endl;
}

/*
// --- Alternative constructor if server runs its own io_context threads ---
// (This is generally less flexible if you want to combine servers)
WebSocketServer::WebSocketServer(
    const std::string& address,
    unsigned short port,
    EventQueue& queue,
    int num_threads)
    : ioc_(*new net::io_context(num_threads > 0 ? num_threads : 1)), // Create new io_context
      acceptor_(net::make_strand(ioc_)),
      event_queue_(queue),
      address_(address),
      port_(port),
      own_io_context_threads_(true) // Mark that we own the threads
{
    std::cout << "WebSocketServer: Initializing with its own io_context and " << num_threads << " threads." << std::endl;
    if (num_threads <= 0) num_threads = 1;
    io_threads_.reserve(num_threads);
    for(int i = 0; i < num_threads; ++i) {
        io_threads_.emplace_back([this] {
            std::cout << "WebSocketServer: io_context thread " << std::this_thread::get_id() << " starting." << std::endl;
            this->ioc_.run();
            std::cout << "WebSocketServer: io_context thread " << std::this_thread::get_id() << " finished." << std::endl;
        });
    }
}
*/

WebSocketServer::~WebSocketServer() {
    std::cout << "WebSocketServer: Destructor called." << std::endl;
    // `stop()` should ideally be called before destruction,
    // but as a fallback, ensure acceptor is closed.
    if (acceptor_.is_open()) {
        beast::error_code ec;
        acceptor_.close(ec); // Close synchronously
        if (ec) {
            std::cerr << "WebSocketServer: Error closing acceptor in destructor: " << ec.message() << std::endl;
        }
    }

    /*
    // If owning io_context threads:
    if (own_io_context_threads_) {
        std::cout << "WebSocketServer: Stopping own io_context." << std::endl;
        ioc_.stop(); // Signal io_context to stop
        for (auto& t : io_threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        std::cout << "WebSocketServer: Own io_context threads joined." << std::endl;
        delete &ioc_; // Delete the io_context we created
    }
    */
}

bool WebSocketServer::run() {
    try {
        auto const ep_address = net::ip::make_address(address_);
        tcp::endpoint endpoint(ep_address, port_);

        // Open the acceptor
        beast::error_code ec;
        acceptor_.open(endpoint.protocol(), ec);
        if (ec) {
            std::cerr << "WebSocketServer: Failed to open acceptor: " << ec.message() << std::endl;
            return false;
        }

        // Allow address reuse
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if (ec) {
            std::cerr << "WebSocketServer: Failed to set reuse_address: " << ec.message() << std::endl;
            // Not necessarily fatal, but log it
        }

        // Bind to the server address
        acceptor_.bind(endpoint, ec);
        if (ec) {
            std::cerr << "WebSocketServer: Failed to bind to " << address_ << ":" << port_ << " - " << ec.message() << std::endl;
            return false;
        }

        // Start listening for connections
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if (ec) {
            std::cerr << "WebSocketServer: Failed to listen: " << ec.message() << std::endl;
            return false;
        }

        std::cout << "WebSocketServer: Listening on " << address_ << ":" << port_ << std::endl;
        
        // Start accepting connections
        // We post this to ensure it runs on the acceptor's strand.
        net::post(acceptor_.get_executor(),
            beast::bind_front_handler(
                &WebSocketServer::do_accept,
                shared_from_this())); // Use shared_from_this if server itself is managed by shared_ptr
                                     // Or `this` if server lifetime is managed otherwise and outlives async ops.
                                     // For a top-level server object, `this` is often okay, but shared_from_this is safer for general components.
                                     // Here, assuming WebSocketServer might be managed by a shared_ptr if it's part of a larger system.

    } catch (const std::exception& e) {
        std::cerr << "WebSocketServer: Exception in run(): " << e.what() << std::endl;
        return false;
    }
    return true;
}

void WebSocketServer::stop() {
    // Post the stop logic to the io_context to ensure thread safety with acceptor operations.
    // The strand of the acceptor is appropriate here.
    net::post(acceptor_.get_executor(), [self = shared_from_this()]() { // Or `this`
        if (self->acceptor_.is_open()) {
            std::cout << "WebSocketServer: Stopping. Closing acceptor." << std::endl;
            beast::error_code ec;
            self->acceptor_.close(ec); // This will cause any pending async_accept to complete with an error.
            if (ec) {
                std::cerr << "WebSocketServer: Error closing acceptor: " << ec.message() << std::endl;
            }
        } else {
            std::cout << "WebSocketServer: Stop called, but acceptor was not open." << std::endl;
        }
    });

    // If this server manages its own io_context and threads, you would stop the io_context here.
    // However, the provided constructor uses an external io_context.
    /*
    if (own_io_context_threads_) {
        ioc_.stop();
        // Joining threads would typically happen in destructor or a dedicated shutdown sequence
    }
    */
}


void WebSocketServer::do_accept() {
    // The new connection gets its own strand
    acceptor_.async_accept(
        net::make_strand(ioc_), // Each session will run on its own strand, but share the ioc_
        beast::bind_front_handler(
            &WebSocketServer::on_accept,
            shared_from_this())); // Or `this`
}

void WebSocketServer::on_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) {
        // operation_aborted typically means the acceptor was closed (e.g., server stopping)
        if (ec == net::error::operation_aborted) {
            std::cout << "WebSocketServer: Accept operation aborted (server likely stopping)." << std::endl;
        } else {
            std::cerr << "WebSocketServer: Accept error: " << ec.message() << std::endl;
        }
        // Don't call do_accept() again if there was a critical error or if server is stopping
        // If acceptor is closed, future do_accept calls won't be scheduled effectively.
        return;
    }

    // Create a new WebSocketSession to handle this connection
    // The session will take ownership of the socket
    std::cout << "WebSocketServer: New connection from " << socket.remote_endpoint() << std::endl;
    // std::make_shared<WebSocketSession>(std::move(socket), event_queue_)->run();
    std::make_shared<WebSocketSession>(std::move(socket), event_queue_, sub_manager_, ioc_)->run();

    // Continue accepting new connections if acceptor is still open
    if (acceptor_.is_open()) {
        do_accept();
    } else {
        std::cout << "WebSocketServer: Acceptor closed, not accepting more connections." << std::endl;
    }
}