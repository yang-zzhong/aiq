# Persistent Event Queue Server - Documentation

## Table of Contents

1.  [Overview](#overview)
2.  [Core Concepts](#core-concepts)
    *   [Topics](#topics)
    *   [Messages](#messages)
    *   [Offsets](#offsets)
    *   [Persistence](#persistence)
3.  [Server Configuration](#server-configuration)
    *   [YAML Configuration File (`config.yaml`)](#yaml-configuration-file-configyaml)
    *   [Command-Line Arguments](#command-line-arguments)
4.  [Network Protocols](#network-protocols)
    *   [A. TCP Protocol](#a-tcp-protocol)
        *   [Connection](#connection)
        *   [Message Format](#message-format)
        *   [Commands & Payloads](#commands--payloads)
            *   [PRODUCE_REQUEST / PRODUCE_RESPONSE](#produce_request--produce_response)
            *   [CONSUME_REQUEST / CONSUME_RESPONSE](#consume_request--consume_response)
            *   [CREATE_TOPIC_REQUEST / CREATE_TOPIC_RESPONSE](#create_topic_request--create_topic_response)
            *   [LIST_TOPICS_REQUEST / LIST_TOPICS_RESPONSE](#list_topics_request--list_topics_response)
            *   [GET_TOPIC_OFFSET_REQUEST / GET_TOPIC_OFFSET_RESPONSE](#get_topic_offset_request--get_topic_offset_response)
            *   [ERROR_RESPONSE](#error_response)
        *   [Status Codes](#status-codes)
    *   [B. HTTP/HTTPS REST API & SSE](#b-httphttps-rest-api--sse)
        *   [Common Aspects](#common-aspects)
        *   [REST API Endpoints](#rest-api-endpoints)
            *   [Produce Message](#produce-message)
            *   [Consume Messages](#consume-messages)
            *   [Create Topic](#create-topic)
            *   [List Topics](#list-topics)
            *   [Get Next Topic Offset](#get-next-topic-offset)
        *   [Server-Sent Events (SSE)](#server-sent-events-sse)
            *   [Stream Topic Messages](#stream-topic-messages)
    *   [C. WebSocket Protocol](#c-websocket-protocol)
        *   [Connection](#connection-1)
        *   [Message Format (JSON)](#message-format-json)
        *   [Commands & Payloads (JSON)](#commands--payloads-json)
            *   [Client to Server Requests](#client-to-server-requests)
            *   [Server to Client Responses/Notifications](#server-to-client-responsesnotifications)
        *   [Example WebSocket Interaction Flow](#example-websocket-interaction-flow)
5.  [Building and Running](#building-and-running)
    *   [Prerequisites](#prerequisites)
    *   [Build Instructions](#build-instructions)
    *   [Running the Server](#running-the-server)
6.  [Client Examples](#client-examples)
7.  [Error Handling](#error-handling)
8.  [Future Enhancements](#future-enhancements)

---

## 1. Overview

The Persistent Event Queue Server is a C++ application designed to provide a reliable, topic-based messaging system. It allows producers to send messages to specific topics and consumers to read these messages starting from a desired offset. Key features include:

*   **Message Persistence:** All messages are durably stored on disk to prevent data loss.
*   **Topics:** Messages are organized into named topics, allowing for logical separation of data streams.
*   **Offset-based Consumption:** Consumers can specify a starting offset within a topic, enabling them to resume consumption or replay messages.
*   **Multiple Network Protocols:** Supports interaction via raw TCP, HTTP/HTTPS (REST API & SSE), and WebSockets.
*   **Configurable:** Server behavior, enabled protocols, ports, and data storage can be configured via a YAML file and command-line arguments.

---

## 2. Core Concepts

### Topics

A topic is a named category or feed to which messages are published. Each topic maintains its own independent sequence of messages and storage. Topics are created automatically when a message is first produced to them or can be explicitly created via API calls.

### Messages

A message consists of:

*   **Offset:** A unique, monotonically increasing 64-bit integer assigned by the server within a topic.
*   **Payload:** The actual content of the message, treated as a binary blob or string.

### Offsets

Offsets serve as unique identifiers for messages within a specific topic. They are sequential and start from 0 for the first message in a topic. Consumers use offsets to specify where they want to begin reading messages. The server also provides the "next offset" for a topic, which indicates the offset that will be assigned to the next message produced to that topic.

### Persistence

*   Each topic's data is stored in its own directory under a main data directory specified in the server configuration.
*   Inside a topic's directory:
    *   `data.log`: An append-only file storing messages: `[offset (8 bytes)][payload_length (4 bytes)][payload (N bytes)]...`
    *   `index.idx`: An index file mapping message offset to byte offset in `data.log` for fast seeking: `[message_offset (8 bytes)][file_byte_offset (8 bytes)]...`
    *   `metadata.meta`: Stores the `next_offset` for this topic.
*   The server attempts to recover topic state from these files on startup, including rebuilding parts of the index if inconsistencies are detected.

---

## 3. Server Configuration

The server's behavior is controlled by a YAML configuration file and can be overridden by command-line arguments.

### YAML Configuration File (`config.yaml`)

A default `config.yaml` can be used or a custom path can be specified.

**Example `config.yaml`:**

```yaml
server_name: "MyPersistentEventQueue"
log_level: "info"
data_directory: "./event_queue_server_data_from_yaml"
thread_pool_size: 0 # 0 for hardware_concurrency

tcp_server:
  enabled: true
  host: "0.0.0.0"
  port: 12345

http_server:
  enabled: true
  host: "0.0.0.0"
  port: 8080
  # ssl_cert_path: "./certs/server.crt" # For HTTPS
  # ssl_key_path: "./certs/server.key"  # For HTTPS

websocket_server:
  enabled: true
  host: "0.0.0.0"
  port: 9090
```

Fields:

* server_name: Informational name for the server.
* log_level: (Placeholder for future logging implementation).
* data_directory: Path to the root directory where topic data will be stored.
* thread_pool_size: Number of threads for the I/O context. 0 uses std::thread::hardware_concurrency().
 * tcp_server, http_server, websocket_server: Sections to configure each protocol.
 * enabled: true or false to enable/disable the server for that protocol.
 * host: The network interface to bind to (e.g., "0.0.0.0" for all interfaces, "127.0.0.1" for localhost).
 * port: The port number to listen on.
 * ssl_cert_path, ssl_key_path (for http_server): Paths to SSL certificate and private key files for enabling HTTPS. If omitted, HTTP is used.

### Command-Line Arguments

Command-line arguments can override settings from the YAML file.

* --help, -h: Display help message.
* --config <path>, -c <path>: Path to the YAML configuration file (default: config.yaml).
* --data-dir <path>: Override the data directory.
* --tcp-port <port>: Override TCP server port (implies enabling TCP).
* --http-port <port>: Override HTTP server port (implies enabling HTTP).
* --ws-port <port>: Override WebSocket server port (implies enabling WebSocket).

## 4. Network Protocols

### A. TCP Protocol

A custom binary protocol over TCP for low-level, efficient communication.

#### Connection

Establish a standard TCP connection to the host and port specified in the tcp_server configuration.

##### Message Format

All TCP messages (requests and responses) follow this structure:

Header (Fixed Size):

* Request Header:
  * CommandType (1 byte): The type of command (see NetworkProtocol::CommandType enum).
  * Payload Length (4 bytes, network byte order - big-endian): Length of the subsequent payload.

* Response Header:
  * CommandType (1 byte): The type of the original request or a specific response type.
  * StatusCode (1 byte): Result of the operation (see NetworkProtocol::StatusCode enum).
  * Payload Length (4 bytes, network byte order - big-endian): Length of the subsequent payload.

Payload (Variable Size):

* The content of the payload depends on the CommandType.
* Strings are typically prefixed with their length:
  * Topic names: 2-byte length (uint16_t, network byte order) followed by string data.
  * Message payloads/Error messages: 4-byte length (uint32_t, network byte order) followed by string data.
  * Numeric types (uint64_t, uint32_t) are sent in network byte order.

##### Commands & Payloads

Refer to network/NetworkProtocol.h for exact structure definitions.

* Client Sends PRODUCE_REQUEST (0x01):
  * Payload:
    * topic_name_length (uint16_t)
    * topic_name (string)
    * message_payload_length (uint32_t)
    * message_payload (string/bytes)
* Server Sends PRODUCE_RESPONSE (0x81):
  * StatusCode: SUCCESS (0x00)
  * Payload:
    * offset (uint64_t): Offset assigned to the produced message.
  * Or ERROR_RESPONSE (0xFF) on failure.
* Client Sends CONSUME_REQUEST (0x02):
  * Payload:
    * topic_name_length (uint16_t)
    * topic_name (string)
    * start_offset (uint64_t)
    * max_messages (uint32_t)
* Server Sends CONSUME_RESPONSE (0x82):
  * StatusCode: SUCCESS (0x00)
  * Payload:
    * num_messages (uint32_t): Number of messages in this response.
    * For each message (repeated num_messages times):
        * message_offset (uint64_t)
        * message_payload_length (uint32_t)
        * message_payload (string/bytes)
  * Or ERROR_RESPONSE (0xFF) on failure.
* Client Sends CREATE_TOPIC_REQUEST (0x04):
  * Payload:
    * topic_name_length (uint16_t)
    * topic_name (string)
* Server Sends CREATE_TOPIC_RESPONSE (0x84):
  * StatusCode: SUCCESS (0x00)
  * Payload: Empty.
  * Or ERROR_RESPONSE (0xFF) on failure.
* Client Sends LIST_TOPICS_REQUEST (0x05):
  * Payload: Empty.
* Server Sends LIST_TOPICS_RESPONSE (0x85):
  * StatusCode: SUCCESS (0x00)
  * Payload:
    * num_topics (uint32_t)
    * For each topic (repeated num_topics times):
      * topic_name_length (uint16_t)
      * topic_name (string)
  * Or ERROR_RESPONSE (0xFF) on failure.
* Client Sends GET_TOPIC_OFFSET_REQUEST (0x03):
  * Payload:
    * topic_name_length (uint16_t)
    * topic_name (string)
* Server Sends GET_TOPIC_OFFSET_RESPONSE (0x83):
  * StatusCode: SUCCESS (0x00)
  * Payload:
    * next_offset (uint64_t)
  * Or ERROR_RESPONSE (0xFF) on failure.
* Server Sends ERROR_RESPONSE (0xFF):
  * StatusCode: Specific error code (e.g., ERROR_TOPIC_NOT_FOUND).
  * Payload:
    * error_message_length (uint32_t)
    * error_message (string)
##### Status Codes

Defined in NetworkProtocol::StatusCode (e.g., SUCCESS, ERROR_TOPIC_NOT_FOUND).

### B. HTTP/HTTPS REST API & SSE
Provides a standard HTTP/HTTPS interface for interacting with the event queue. Payloads are primarily JSON. HTTPS is enabled if ssl_cert_path and ssl_key_path are configured.

#### Common Aspects

* Content-Type: application/json for requests and responses.
* Error Responses: JSON object like {"error": "Error message details"} with appropriate HTTP status codes (4xx for client errors, 5xx for server errors).

##### REST API Endpoints
(Assuming server host and port from http_server configuration)
* Endpoint: POST /topics/{topic_name}/produce
* Request Body (JSON):
```json
Generated json
{
  "payload": "Your message content here"
}
```
* Success Response (201 Created, JSON):
```json
{
  "topic": "{topic_name}",
  "offset": 123
}
```

* Endpoint: GET /topics/{topic_name}/consume
* Query Parameters (Optional):
  * offset=<uint64>: Starting offset (default: 0).
  * max_messages=<uint32>: Maximum number of messages to return (default: 100, max: 1000).
* Success Response (200 OK, JSON Array of Messages):
```json
[
  { "offset": 0, "topic": "{topic_name}", "payload": "Message 1" },
  { "offset": 1, "topic": "{topic_name}", "payload": "Message 2" }
]
```
(If topic doesn't exist or no messages at offset, returns an empty array.)

* Endpoint: POST /topics/{topic_name}
* Request Body: Empty.
* Success Response (201 Created, JSON):

```json
{
  "topic": "{topic_name}",
  "status": "created_or_exists"
}
```

* Endpoint: GET /topics
* Success Response (200 OK, JSON Array of Strings):
```json
[ "topic_A", "topic_B", "another_topic" ]
```

* Endpoint: GET /topics/{topic_name}/next_offset
* Success Response (200 OK, JSON):
```json
{
  "topic": "{topic_name}",
  "next_offset": 124
}
```
(If topic doesn't exist, next_offset will be 0.)

##### Server-Sent Events (SSE)

For streaming messages from a topic to a client in real-time.

* Endpoint: GET /topics/{topic_name}/stream
* Query Parameters (Optional):
  * offset=<uint64>: Start streaming messages from this offset.
* Headers (Client may send):
  * Last-Event-ID: <offset>: If client reconnects, server can resume from offset + 1.
* Response Headers (Server sends):
  * Content-Type: text/event-stream
  * Cache-Control: no-cache
  * Connection: keep-alive
* Event Stream Format:
```
id: <message_offset>
event: message
data: {"offset": <offset>, "topic": "{topic_name}", "payload": "Message content"}

id: <message_offset_2>
event: message
data: {"offset": <offset_2>, "topic": "{topic_name}", "payload": "Another message"}

: keep-alive comment (optional, periodically)
```

* Each message is sent as an event. The data field contains the JSON representation of the Message object.

### C. WebSocket Protocol

Provides a persistent, bi-directional communication channel. Messages are exchanged as JSON text frames.

#### Connection
Establish a WebSocket connection (ws:// or wss:// if SSL is configured for the underlying HTTP server that upgrades to WS) to the host and port specified in the websocket_server configuration.

#### Message Format (JSON)
All messages exchanged over WebSocket are JSON objects. They generally include a command field to indicate the action or event type, and an optional req_id for clients to correlate requests with server responses.
Refer to `network/WebSocketTypes.h` for detailed structure definitions.

#### Commands & Payloads (JSON)
* PRODUCE_REQUEST
```json
{
  "command": "produce_request",
  "req_id": 1, // Optional client-generated ID
  "topic": "my_updates",
  "message_payload": "This is a new update!"
}
```
* SUBSCRIBE_TOPIC_REQUEST
```json
{
  "command": "subscribe_topic_request",
  "req_id": 2,
  "topic": "live_feed",
  "start_offset": 0 // Or last known offset
}
```
* UNSUBSCRIBE_TOPIC_REQUEST
```json
{
  "command": "unsubscribe_topic_request",
  "req_id": 3,
  "topic": "live_feed"
}
```
* CREATE_TOPIC_REQUEST
```json
{
  "command": "create_topic_request",
  "req_id": 4,
  "topic": "new_logs_topic"
}
```
* LIST_TOPICS_REQUEST
```json
{
  "command": "list_topics_request",
  "req_id": 5
}
```
* GET_NEXT_OFFSET_REQUEST
```json
{
  "command": "get_next_offset_request",
  "req_id": 6,
  "topic": "my_updates"
}
```
* PRODUCE_RESPONSE
```json
{
  "command": "produce_response",
  "req_id": 1, // Echoes client's req_id
  "topic": "my_updates",
  "offset": 105,
  "success": true
}
```
* SUBSCRIBE_TOPIC_RESPONSE
```json
{
  "command": "subscribe_topic_response",
  "req_id": 2,
  "topic": "live_feed",
  "success": true
}
```
* MESSAGE_BATCH_NOTIFICATION (Pushed to subscribed clients)
```json
{
  "command": "message_batch_notification",
  // "req_id": null, // Or linked to a subscription ID if implemented
  "topic": "live_feed",
  "messages": [
    { "offset": 0, "topic": "live_feed", "payload": "Event A" },
    { "offset": 1, "topic": "live_feed", "payload": "Event B" }
  ]
}
```
* LIST_TOPICS_RESPONSE
```json
{
  "command": "list_topics_response",
  "req_id": 5,
  "topics": ["my_updates", "live_feed", "new_logs_topic"],
  "success": true
}
```
* ERROR_RESPONSE
```json
{
  "command": "error_response",
  "req_id": 2, // If related to a specific request
  "error_message": "Topic 'non_existent' not found for subscription.",
  "original_command_type": "subscribe_topic_request" // Optional
}
```
(Other response types like UNSUBSCRIBE_TOPIC_RESPONSE, CREATE_TOPIC_RESPONSE, GET_NEXT_OFFSET_RESPONSE follow a similar pattern with success and optional error_message fields.)

#### Example WebSocket Interaction Flow
* Client connects to WebSocket endpoint.
* Client sends SUBSCRIBE_TOPIC_REQUEST for "topic_A" from offset 0.
* Server responds with SUBSCRIBE_TOPIC_RESPONSE (success).
* Server starts polling "topic_A" for new messages for this client. When messages are available, it sends MESSAGE_BATCH_NOTIFICATION with the new messages.
* Client sends PRODUCE_REQUEST for "topic_B" with a payload.
* Server processes it, stores the message, and responds with PRODUCE_RESPONSE containing the new offset.
* Client sends UNSUBSCRIBE_TOPIC_REQUEST for "topic_A".
* Server responds with UNSUBSCRIBE_TOPIC_RESPONSE and stops sending messages for "topic_A" to this client.

## 5. Building and Running

### Prerequisites

* C++17 compliant compiler (GCC, Clang, MSVC).
* CMake (version 3.16+ recommended).
* Boost libraries (System, Thread, Filesystem, Program_options, Asio, Beast).
* OpenSSL development libraries (for HTTPS/WSS).
* yaml-cpp development libraries.
* nlohmann/json.hpp (header-only, included or fetched by CMake).
* cpp-httplib/httplib.h (header-only, included or fetched by CMake).
* Git (if using CMake's FetchContent for header-only libraries).

### Build Instructions

* Clone the repository (if applicable).
* Navigate to the project root.
* Create a build directory: mkdir build && cd build
* Run CMake: cmake .. [-DCMAKE_BUILD_TYPE=Release/Debug] [-DCMAKE_PREFIX_PATH=/path/to/deps;...]
* Set CMAKE_PREFIX_PATH if Boost, OpenSSL, or yaml-cpp are installed in non-standard locations.
* Compile: cmake --build . or make -j$(nproc) (Linux/macOS) or open the generated solution in your IDE (Windows).

### Running the Server

Execute the compiled binary:
```bash
./build/event_queue_server [options]
```
Bash

Common Options:

* ./build/event_queue_server (uses config.yaml in the current directory if it exists)
* ./build/event_queue_server -c /path/to/your/custom_config.yaml
* ./build/event_queue_server --http-port 8081 --data-dir /mnt/event_data
The server will log its startup information, enabled protocols, and listening ports to the console. Press Ctrl+C to initiate a graceful shutdown.

## 6. Client Examples
The project may include example client implementations:
* event_queue_tcp_client: Demonstrates interaction using the raw TCP protocol.
* event_queue_http_client: Demonstrates interaction with the HTTP REST API and SSE.
* (A WebSocket client example might be provided separately).
Refer to the source code of these clients for usage details.

## 7. Error Handling
* TCP Protocol: Errors are indicated by the StatusCode in the response header, with a descriptive message in the ERROR_RESPONSE payload.
* HTTP/HTTPS Protocol: Standard HTTP status codes (4xx for client errors, 5xx for server errors) are used. Response bodies for errors are JSON objects like {"error": "description"}.
* WebSocket Protocol: Errors are sent as ERROR_RESPONSE JSON messages, often including the req_id of the failed request and a descriptive error_message.
Server logs also provide information about internal errors or issues during request processing.

## 8. Future Enhancements
* Authentication and Authorization: Secure access to topics and operations.
* Advanced Subscription Management: More efficient push notifications for SSE/WebSockets instead of polling.
* Message Compaction: For topics where only the latest value for a key is important.
* Log Segmentation and Retention Policies: Manage large topic logs by splitting them and applying time/size-based retention.
* Consumer Groups: Allow multiple consumers to share the load of processing messages from a topic.
* Metrics and Monitoring: Expose server performance metrics.
* Admin API/UI: For managing topics and server configuration.
* Clustering/Replication: For high availability and fault tolerance (a significant undertaking).