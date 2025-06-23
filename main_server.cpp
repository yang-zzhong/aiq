// main_server.cpp
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <memory>
#include <csignal>      // For signal handling
#include <atomic>
#include <algorithm>    // For std::max

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/beast/core.hpp>
#include <boost/program_options.hpp> // For command-line argument parsing

#include <yaml-cpp/yaml.h> // For YAML configuration
#include <fstream>         // For reading YAML file

#include "event_queue_core/EventQueue.h"
#include "event_queue_core/INewMessageListener.h" // Core interface
#include "network/SubscriptionManager.h"       // Network layer, implements listener
#include "network/TcpServer.h"
#include "network/HttpServer.h"       // Assumes this uses cpp-httplib
#include "network/WebSocketServer.h"  // Assumes this uses Boost.Beast

namespace po = boost::program_options;
namespace net = boost::asio;

// --- Configuration Structs ---
struct ServerConfig {
    std::string server_name = "PersistentEventQueue";
    std::string log_level = "info";
    std::string data_directory = "./event_queue_server_data";
    int thread_pool_size = 0; // 0 means std::thread::hardware_concurrency()

    struct TcpConfig {
        bool enabled = false;
        std::string host = "0.0.0.0";
        unsigned short port = 12345;
    } tcp;

    struct HttpConfig {
        bool enabled = false;
        std::string host = "0.0.0.0";
        unsigned short port = 8080;
        std::string ssl_cert_path;
        std::string ssl_key_path;
    } http;

    struct WebSocketConfig {
        bool enabled = false;
        std::string host = "0.0.0.0";
        unsigned short port = 9090;
    } websocket;
};

// --- Helper to load configuration from YAML ---
bool load_config_from_yaml(const std::string& filepath, ServerConfig& config) {
    try {
        YAML::Node yaml_config = YAML::LoadFile(filepath);
        std::cout << "Loading configuration from: " << filepath << std::endl;

        if (yaml_config["server_name"]) config.server_name = yaml_config["server_name"].as<std::string>();
        if (yaml_config["log_level"]) config.log_level = yaml_config["log_level"].as<std::string>();
        if (yaml_config["data_directory"]) config.data_directory = yaml_config["data_directory"].as<std::string>();
        if (yaml_config["thread_pool_size"]) config.thread_pool_size = yaml_config["thread_pool_size"].as<int>();

        if (yaml_config["tcp_server"]) {
            const auto& tcp_node = yaml_config["tcp_server"];
            if (tcp_node["enabled"]) config.tcp.enabled = tcp_node["enabled"].as<bool>();
            if (tcp_node["host"]) config.tcp.host = tcp_node["host"].as<std::string>();
            if (tcp_node["port"]) config.tcp.port = tcp_node["port"].as<unsigned short>();
        }

        if (yaml_config["http_server"]) {
            const auto& http_node = yaml_config["http_server"];
            if (http_node["enabled"]) config.http.enabled = http_node["enabled"].as<bool>();
            if (http_node["host"]) config.http.host = http_node["host"].as<std::string>();
            if (http_node["port"]) config.http.port = http_node["port"].as<unsigned short>();
            if (http_node["ssl_cert_path"]) config.http.ssl_cert_path = http_node["ssl_cert_path"].as<std::string>();
            if (http_node["ssl_key_path"]) config.http.ssl_key_path = http_node["ssl_key_path"].as<std::string>();
        }

        if (yaml_config["websocket_server"]) {
            const auto& ws_node = yaml_config["websocket_server"];
            if (ws_node["enabled"]) config.websocket.enabled = ws_node["enabled"].as<bool>();
            if (ws_node["host"]) config.websocket.host = ws_node["host"].as<std::string>();
            if (ws_node["port"]) config.websocket.port = ws_node["port"].as<unsigned short>();
        }
        return true;
    } catch (const YAML::Exception& e) {
        std::cerr << "Error loading/parsing YAML config file '" << filepath << "': " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error processing config data from '" << filepath << "': " << e.what() << std::endl;
        return false;
    }
}

// Global atomic for shutdown signal
std::atomic<bool> g_shutdown_requested(false);

void signal_handler(int /*signal*/) {
    g_shutdown_requested = true;
}

int main(int argc, char* argv[]) {
    ServerConfig config;
    std::string config_filepath = "config.yaml"; // Default config file name

    // --- Command Line Argument Parsing ---
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "Produce help message")
        ("config,c", po::value<std::string>(&config_filepath)->default_value("config.yaml"), "Path to YAML configuration file")
        ("data-dir", po::value<std::string>(), "Override data directory")
        ("tcp-port", po::value<unsigned short>(), "Override TCP server port")
        ("http-port", po::value<unsigned short>(), "Override HTTP server port")
        ("ws-port", po::value<unsigned short>(), "Override WebSocket server port");

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch (const po::error& e) {
        std::cerr << "Error parsing command line options: " << e.what() << std::endl;
        std::cout << desc << std::endl;
        return 1;
    }

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 0;
    }

    // --- Load Configuration ---
    if (!load_config_from_yaml(config_filepath, config)) {
        std::cerr << "Failed to load configuration. Using defaults or exiting." << std::endl;
        // Decide if to proceed with defaults or exit. For this example, we'll use defaults.
        // return 1; // Or proceed with hardcoded defaults
    }

    // Apply command-line overrides
    if (vm.count("data-dir")) config.data_directory = vm["data-dir"].as<std::string>();
    if (vm.count("tcp-port")) { config.tcp.port = vm["tcp-port"].as<unsigned short>(); config.tcp.enabled = true; }
    if (vm.count("http-port")) { config.http.port = vm["http-port"].as<unsigned short>(); config.http.enabled = true; }
    if (vm.count("ws-port")) { config.websocket.port = vm["ws-port"].as<unsigned short>(); config.websocket.enabled = true; }

    std::cout << "--- Server Configuration ---" << std::endl;
    std::cout << "Server Name: " << config.server_name << std::endl;
    std::cout << "Data Directory: " << config.data_directory << std::endl;
    std::cout << "Thread Pool Size: " << (config.thread_pool_size == 0 ? "Auto (Hardware Concurrency)" : std::to_string(config.thread_pool_size)) << std::endl;
    if(config.tcp.enabled) std::cout << "TCP Server: Enabled on " << config.tcp.host << ":" << config.tcp.port << std::endl;
    if(config.http.enabled) {
        std::cout << "HTTP(S) Server: Enabled on " << config.http.host << ":" << config.http.port;
        if(!config.http.ssl_cert_path.empty()) std::cout << " (HTTPS)";
        std::cout << std::endl;
    }
    if(config.websocket.enabled) std::cout << "WebSocket Server: Enabled on " << config.websocket.host << ":" << config.websocket.port << std::endl;
    std::cout << "----------------------------" << std::endl;


    // --- Initialize Core Event Queue ---
    std::unique_ptr<EventQueue> event_queue;
    try {
        event_queue = std::make_unique<EventQueue>(config.data_directory);
    } catch (const std::exception& e) {
        std::cerr << "FATAL: Failed to initialize EventQueue: " << e.what() << std::endl;
        return 1;
    }

    std::unique_ptr<SubscriptionManager> sub_manager = std::make_unique<SubscriptionManager>();

    // Register SubscriptionManager as a listener to the EventQueue (Core)
    if (event_queue && sub_manager) {
        event_queue->add_listener(sub_manager.get()); // <<< CORE IS NOTIFIED, REGISTERS LISTENER
    }

    // --- Initialize Boost.Asio io_context and Thread Pool ---
    net::io_context ioc;
    auto work_guard = net::make_work_guard(ioc); // Keep io_context::run() from returning prematurely

    unsigned int num_threads = (config.thread_pool_size == 0)
                             ? std::thread::hardware_concurrency()
                             : static_cast<unsigned int>(config.thread_pool_size);
    num_threads = std::max(1u, num_threads); // Ensure at least one thread

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    std::cout << "Starting " << num_threads << " I/O threads." << std::endl;
    for (unsigned int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&ioc]() {
            try {
                ioc.run();
            } catch (const std::exception& e) {
                std::cerr << "Exception in io_context thread: " << e.what() << std::endl;
            }
        });
    }

    // --- Signal Handling for Graceful Shutdown ---
    // std::signal(SIGINT, signal_handler); // POSIX C-style
    // std::signal(SIGTERM, signal_handler);
    // Using Asio's signal_set is more robust with Asio event loop
    net::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::beast::error_code& /*error*/, int /*signal_number*/) {
        std::cout << "\nSignal received. Initiating graceful shutdown..." << std::endl;
        g_shutdown_requested = true;
        
        // Stop accepting new connections and clean up servers (posted to ioc)
        // The actual stopping of io_context will happen after servers are done
        // No need to call ioc.stop() here directly, work_guard.reset() will do it.
    });


    // --- Initialize and Start Servers ---
    std::unique_ptr<TcpServer> tcp_server; // TcpServer is simpler, doesn't need enable_shared_from_this for basic start/stop
    std::unique_ptr<HttpServer> http_server; // HttpServer is also simple
    std::shared_ptr<WebSocketServer> ws_server; // WebSocketServer uses enable_shared_from_this

    try {
        if (config.tcp.enabled) {
            tcp_server = std::make_unique<TcpServer>(ioc, config.tcp.port, *event_queue);
            // TcpServer's constructor usually starts listening or has a run() method.
            // Assuming constructor starts it or we call a run method here.
            // For this example, assuming constructor of TcpServer starts listening.
             std::cout << "TCP Server setup initiated." << std::endl;
        }

        if (config.http.enabled) {
            http_server = std::make_unique<HttpServer>(*event_queue,
                                                       config.http.host,
                                                       config.http.port,
                                                       config.http.ssl_cert_path,
                                                       config.http.ssl_key_path);
            if (!http_server->start()) {
                std::cerr << "Failed to start HTTP(S) server. Check logs and config." << std::endl;
                // Potentially exit or disable this server
            } else {
                std::cout << "HTTP(S) Server setup initiated." << std::endl;
            }
        }

        if (config.websocket.enabled) {
            ws_server = std::make_shared<WebSocketServer>(ioc,
                                                          config.websocket.host,
                                                          config.websocket.port,
                                                          *sub_manager,
                                                          *event_queue);
            if (!ws_server->run()) {
                 std::cerr << "Failed to start WebSocket server." << std::endl;
            } else {
                std::cout << "WebSocket Server setup initiated." << std::endl;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "FATAL: Exception during server initialization: " << e.what() << std::endl;
        g_shutdown_requested = true; // Trigger shutdown
    }


    // --- Main Server Loop (effectively waiting for shutdown) ---
    std::cout << config.server_name << " started. Press Ctrl+C to exit." << std::endl;
    while (!g_shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // Can add health checks or other periodic tasks here if needed
    }
    std::cout << "Shutdown requested. Cleaning up..." << std::endl;

    // --- Graceful Shutdown ---
    // 1. Stop servers from accepting new connections / stop their specific logic
    if (ws_server) {
        std::cout << "Stopping WebSocket server..." << std::endl;
        ws_server->stop(); // WebSocketServer::stop() should post to ioc to close acceptor
    }
    if (http_server && http_server->is_running()) {
        std::cout << "Stopping HTTP(S) server..." << std::endl;
        http_server->stop(); // HttpServer::stop() should stop its internal thread
    }
    if (tcp_server) {
        std::cout << "Stopping TCP server..." << std::endl;
        // TcpServer might need a stop() method that closes its acceptor.
        // For simplicity, if TcpServer only uses its constructor to start,
        // its acceptor might close when ioc stops or when tcp_server is destroyed.
        // A proper stop method is better:
        // tcp_server->stop();
        // For now, rely on destructor or ioc shutdown.
    }
    
    // Give some time for servers to process their stop commands if they involve async ops
    std::this_thread::sleep_for(std::chrono::seconds(1));


    // 2. Allow io_context to stop by resetting the work guard
    // This will allow ioc.run() to return once all pending async operations are complete.
    std::cout << "Releasing io_context work guard..." << std::endl;
    work_guard.reset(); // Or use ioc.stop() if all ops are self-contained.
                        // work_guard.reset() is generally preferred with thread pools.

    // 3. Join I/O threads
    std::cout << "Waiting for I/O threads to finish..." << std::endl;
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    if (event_queue && sub_manager) { // Unregister listener during shutdown
        event_queue->remove_listener(sub_manager.get());
    }
    // 4. Destroy server objects (happens automatically with unique_ptr/shared_ptr going out of scope)
    // Ensure destructors are clean and release resources.
    std::cout << "EventQueue server shut down gracefully." << std::endl;
    return 0;
}