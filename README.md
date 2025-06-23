# Persistent Event Queue Server

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
<!-- Add other badges if you have them, e.g., build status, code coverage -->

A C++ application providing a reliable, topic-based, persistent messaging system with support for TCP, HTTP/HTTPS (REST & SSE), and WebSocket protocols.

## Table of Contents

- [Features](#features)
- [Core Concepts](#core-concepts)
- [Supported Protocols](#supported-protocols)
- [Prerequisites](#prerequisites)
- [Building the Server](#building-the-server)
  - [Using CMake](#using-cmake)
- [Configuration](#configuration)
  - [YAML Configuration (`config.yaml`)](#yaml-configuration-configyaml)
  - [Command-Line Arguments](#command-line-arguments)
- [Running the Server](#running-the-server)
- [API Documentation](#api-documentation)
- [Client Examples](#client-examples)
- [Development](#development)
  - [Directory Structure](#directory-structure)
  - [Dependencies](#dependencies)
- [Contributing](#contributing)
- [License](#license)

---

## Features

*   **Persistent Storage:** Messages are durably stored on disk for each topic, ensuring data is not lost on server restart.
*   **Topic-Based Messaging:** Organize message streams into logical topics.
*   **Offset-Based Consumption:** Consumers can read messages starting from any valid offset within a topic, allowing for replay or resumption.
*   **Multi-Protocol Support:**
    *   **Raw TCP:** For low-latency, high-performance binary communication.
    *   **HTTP/HTTPS:** Standard RESTful API for producing/consuming messages and managing topics. Includes Server-Sent Events (SSE) for real-time message streaming.
    *   **WebSockets:** Persistent, bi-directional communication channel using JSON messages for interactive clients and real-time updates.
*   **Configurable:** Server behavior, enabled protocols, ports, data storage, and SSL settings are configurable via a YAML file and command-line arguments.
*   **Cross-Platform:** Built with C++ and standard libraries like Boost, designed to be compilable on Linux, macOS, and Windows (with appropriate toolchains).

---

## Core Concepts

*   **Topics:** Named channels for message streams.
*   **Messages:** Data payloads with an assigned offset within a topic.
*   **Offsets:** Unique, sequential identifiers for messages in a topic, used by consumers to track their position.
*   **Persistence:** Each topic's messages, index, and metadata are stored in separate files for durability and recovery.

For more details, see the [Full API and Protocol Documentation](DOCUMENTATION.md).

---

## Supported Protocols

1.  **TCP Protocol:** A custom binary protocol for efficient, low-level access. (Default Port: `12345`)
2.  **HTTP/HTTPS (REST & SSE):** Standard HTTP interface using JSON. Supports HTTPS if SSL certificates are configured. Server-Sent Events (SSE) are available for streaming. (Default Port: `8080`)
3.  **WebSocket Protocol:** JSON-based messaging over a persistent WebSocket connection. (Default Port: `9090`)

Each protocol can be enabled/disabled and configured independently.

---

## Prerequisites

*   **C++17 Compiler:** GCC, Clang, or MSVC.
*   **CMake:** Version 3.16 or higher.
*   **Boost Libraries:** (Typically 1.70+). Required components:
    *   System
    *   Thread
    *   Filesystem
    *   Program_options
    *   Asio (for TCP & WebSocket server foundation)
    *   Beast (for WebSocket server implementation)
*   **OpenSSL Development Libraries:** For HTTPS and WebSocket Secure (WSS) support.
*   **yaml-cpp Development Libraries:** For parsing the YAML configuration file.
*   **Git:** If fetching header-only dependencies (nlohmann/json, cpp-httplib) via CMake's `FetchContent`.
*   **(Optional) Header-Only Libraries:**
    *   `nlohmann/json.hpp`: For JSON processing.
    *   `cpp-httplib/httplib.h`: For the HTTP/HTTPS server.
    (These can be included in a `third_party` directory or fetched by CMake).

---

## Building the Server

### Using CMake

1.  **Clone the repository (if applicable):**
    ```bash
    git clone <your-repository-url>
    cd persistent-event-queue-server
    ```

2.  **Create a build directory:**
    ```bash
    mkdir build
    cd build
    ```

3.  **Run CMake to configure the project:**
    ```bash
    # For a release build:
    cmake .. -DCMAKE_BUILD_TYPE=Release

    # For a debug build:
    # cmake .. -DCMAKE_BUILD_TYPE=Debug

    # If dependencies are in non-standard locations, you might need to specify paths:
    # cmake .. -DCMAKE_PREFIX_PATH="/path/to/boost;/path/to/openssl;/path/to/yaml-cpp"
    ```

4.  **Compile the project:**
    ```bash
    cmake --build . -j$(nproc)  # For Linux/macOS, adjust -j for number of cores
    # Or on Linux/macOS: make -j$(nproc)
    # On Windows with MSVC, open the generated .sln file in Visual Studio and build.
    ```
    The executable `event_queue_server` (and client examples if enabled) will be created in the `build` directory (or a subdirectory like `build/bin` depending on CMake setup).

---

## Configuration

The server is configured using a `config.yaml` file and command-line arguments.

### YAML Configuration (`config.yaml`)

Place a `config.yaml` file in the directory where you run the server, or specify its path using the `-c` option.

**Example `config.yaml` snippet:**
```yaml
data_directory: "./server_event_data" # Where message data is stored

thread_pool_size: 4 # Number of I/O threads

tcp_server:
  enabled: true
  host: "0.0.0.0"
  port: 12345

http_server:
  enabled: true
  host: "0.0.0.0"
  port: 8080
  # For HTTPS:
  # ssl_cert_path: "path/to/server.crt"
  # ssl_key_path: "path/to/server.key"

websocket_server:
  enabled: true
  host: "0.0.0.0"
  port: 9090
```
See DOC.md for all available options.

### Command-Line Arguments

Override YAML settings or specify common options:

* ./event_queue_server --help # Show all options
* ./event_queue_server -c my_custom_config.yaml
* ./event_queue_server --http-port 8081 --data-dir /mnt/queue_data

### Running the Server

Once built, you can run the server from your build directory:
```bash
./event_queue_server [options]
```

## Example:

```bash
# Using default config.yaml (if present in current directory)
./event_queue_server

# Specifying a config file
./event_queue_server -c ../config.yaml

# Overriding ports
./event_queue_server --tcp-port 12300 --http-port 8001 --ws-port 9001
```

## Client Examples

Example client applications demonstrating how to interact with the server via different protocols may be included in the client/ directory and built alongside the server (e.g., event_queue_tcp_client, event_queue_http_client).
Check the build directory for these executables after compilation.

## Development

Directory Structure (Simplified)
```
.
├── CMakeLists.txt
├── config.yaml          # Example configuration
├── DOCUMENTATION.md
├── LICENSE
├── README.md
├── certs/               # Example SSL certificates
│   ├── server.crt
│   └── server.key
├── client/              # Client example code
│   ├── main_http_client.cpp
│   └── main_tcp_client.cpp
├── event_queue_core/    # Core event queue logic (persistence, topics)
│   ├── EventQueue.h/cpp
│   ├── Message.h
│   ├── Topic.h/cpp
│   └── BinaryUtils.h
├── network/             # Network protocol handlers
│   ├── HttpServer.h/cpp
│   ├── NetworkProtocol.h
│   ├── TcpServer.h/cpp
│   ├── TcpSession.h/cpp
│   ├── WebSocketServer.h/cpp
│   ├── WebSocketSession.h/cpp
│   └── WebSocketTypes.h
├── third_party/         # Header-only dependencies (if not using FetchContent)
│   ├── cpp-httplib/httplib.h
│   └── nlohmann_json/include/nlohmann/json.hpp
└── main_server.cpp      # Main server application entry point
```

## Dependencies
* Core Logic: C++17 Standard Library, Boost.Filesystem (potentially).
* Networking:
* TCP & WebSocket Server: Boost.Asio, Boost.Beast.
* HTTP/HTTPS Server: cpp-httplib.
* Configuration: yaml-cpp.
* Command-Line Parsing: Boost.Program_options.
* JSON Handling: nlohmann/json.
* SSL/TLS: OpenSSL (via cpp-httplib and potentially Boost.Beast for WSS).

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues for bugs, feature requests, or improvements.

Before contributing, please consider:

* Discussing significant changes by opening an issue first.
* Adhering to the existing coding style (if one is established).
* Writing or updating tests for new features or bug fixes.
* Updating documentation as needed.
