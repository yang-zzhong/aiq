// main_server.cpp
#include "event_queue_core/EventQueue.h"
#include "network/TcpServer.h"
#include <boost/asio/io_context.hpp>
#include <iostream>
#include <vector>
#include <thread>

int main(int argc, char* argv[]) {
    try {
        short port = 12345;
        if (argc > 1) {
            port = static_cast<short>(std::atoi(argv[1]));
        }
        std::string data_dir = "./event_queue_server_data";
        if (argc > 2) {
            data_dir = argv[2];
        }

        std::cout << "Starting Event Queue Server..." << std::endl;
        std::cout << "Data directory: " << data_dir << std::endl;
        std::cout << "Listening on port: " << port << std::endl;

        EventQueue event_queue(data_dir); // The core queue logic

        boost::asio::io_context io_context;
        TcpServer server(io_context, port, event_queue);

        // Run the io_context in a pool of threads for handling multiple clients
        // The number of threads can be tuned.
        // std::thread::hardware_concurrency() is often a good starting point.
        unsigned int num_threads = std::max(1u, std::thread::hardware_concurrency());
        if (num_threads == 0) num_threads = 2; // Fallback if hardware_concurrency() returns 0
        std::cout << "Running IO context with " << num_threads << " threads." << std::endl;

        std::vector<std::thread> threads;
        for (unsigned int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&io_context]() {
                io_context.run();
            });
        }

        // Keep the main thread alive, or join the io_context threads
        // For a server, io_context.run() in the main thread (or one of the threads)
        // will block until io_context.stop() is called.
        // If all threads run io_context.run(), they will all block.
        // The current setup is fine, the threads will process work.
        // The main thread can do other things or just wait.
        
        // For a simple server, one thread running io_context might be enough initially,
        // or run it in the main thread:
        // io_context.run(); 
        // But for scalability, a thread pool is better.

        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }
        std::cout << "Server shut down." << std::endl;


    } catch (const std::exception& e) {
        std::cerr << "Server exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
