cmake_minimum_required(VERSION 3.16) # Increased for FetchContent if used, 3.14 is fine otherwise
project(PersistentEventQueueServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF) # Good practice

# --- Project Structure Variables (Optional, for clarity) ---
set(PROJECT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(CORE_DIR ${PROJECT_SOURCE_DIR}/event_queue_core)
set(NETWORK_DIR ${PROJECT_SOURCE_DIR}/network)
set(CLIENT_DIR ${PROJECT_SOURCE_DIR}/client) # Assuming you still have client code
set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party)

# --- FetchContent for Header-Only Libraries (Alternative to manual download) ---
# This makes the build more self-contained but requires internet on first configure.
# You can choose to download them manually and place them in third_party/ as before.
include(FetchContent)

# nlohmann/json
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3 # Use a specific version
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG master # Can be a tag (yaml-cpp-x.x.x), a commit hash, or a branch name (master)
)
FetchContent_MakeAvailable(yaml-cpp)

# cpp-httplib (using a specific commit for stability)
FetchContent_Declare(
  cpphttplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.15.3 # Or a specific commit hash if tags are infrequent
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cpphttplib) # This creates cpp-httplib target

# --- Find System-Installed Dependencies ---

# Boost
find_package(Boost 1.80.0 REQUIRED COMPONENTS
    system
    thread
    filesystem
    program_options
    # Beast is often header-only when used with Boost.Asio, but explicitly find if needed
    # Or ensure your Boost version includes it and Asio.
)
if(NOT Boost_FOUND)
    message(FATAL_ERROR "Boost not found. Please install Boost or set Boost_ROOT.")
endif()

# OpenSSL (for HTTPS in cpp-httplib and WSS in Boost.Beast)
find_package(OpenSSL REQUIRED)
if(NOT OpenSSL_FOUND)
    message(FATAL_ERROR "OpenSSL not found. Please install OpenSSL development libraries.")
endif()

# yaml-cpp (for HTTPS in cpp-httplib and WSS in Boost.Beast)
find_package(yaml-cpp REQUIRED)
if(NOT yaml-cpp_FOUND)
    message(FATAL_ERROR "yaml-cpp not found. Please install OpenSSL development libraries.")
endif()

# yaml-cpp
# find_package(yaml-cpp 0.8 REQUIRED) # Adjust version as needed
# if(NOT YAML_CPP_FOUND) # Note: find_package(yaml-cpp) sets YAML_CPP_FOUND
#     message(FATAL_ERROR "yaml-cpp not found. Please install libyaml-cpp-dev or equivalent.")
# endif()

# Threads (standard CMake module)
find_package(Threads REQUIRED)

# --- Global Include Directories ---
include_directories(
    ${CORE_DIR}
    ${NETWORK_DIR}
    ${CLIENT_DIR} # If client shares headers like Message.h
    # For FetchContent (these are added to the include path automatically by FetchContent_MakeAvailable)
    # ${nlohmann_json_SOURCE_DIR}/include # If FetchContent_MakeAvailable doesn't add it
    # ${cpphttplib_SOURCE_DIR}           # If FetchContent_MakeAvailable doesn't add it
    # For manual download:
    ${THIRD_PARTY_DIR}/nlohmann_json/include
    ${THIRD_PARTY_DIR}/cpp-httplib # httplib.h is directly in this folder
    # System dependencies (usually handled by find_package providing imported targets or variables)
    ${Boost_INCLUDE_DIRS}
    ${OpenSSL_INCLUDE_DIR}
    ${YAML_CPP_INCLUDE_DIRS} # Correct variable from Findyaml-cpp.cmake
)

# --- Event Queue Core Library ---
add_library(event_queue_core_lib STATIC
    ${CORE_DIR}/Topic.cpp
    ${CORE_DIR}/EventQueue.cpp
    ${CORE_DIR}/BinaryUtils.cpp
    # Message.h and BinaryUtils.h are assumed header-only or included where needed
)
target_include_directories(event_queue_core_lib PUBLIC ${CORE_DIR})
target_link_libraries(event_queue_core_lib PRIVATE
    Boost::filesystem # Topic.cpp uses std::filesystem which often comes from Boost
                      # or C++17 standard library. If pure C++17, this might not be needed.
                      # But your example used fs:: from <filesystem> which could be boost or std
    Threads::Threads  # If EventQueue uses std::thread directly
)

# --- Server Executable ---
add_executable(event_queue_server
    main_server.cpp
    ${NETWORK_DIR}/TcpSession.cpp
    ${NETWORK_DIR}/TcpServer.cpp
    ${NETWORK_DIR}/HttpServer.cpp
    ${NETWORK_DIR}/SubscriptionManager.cpp
    ${NETWORK_DIR}/WebSocketSession.cpp
    ${NETWORK_DIR}/WebSocketServer.cpp
)

target_link_libraries(event_queue_server PRIVATE
    event_queue_core_lib
    # Boost
    Boost::system
    Boost::thread
    Boost::program_options
    # OpenSSL
    OpenSSL::SSL
    OpenSSL::Crypto
    # yaml-cpp
    yaml-cpp # Target name from find_package(yaml-cpp)
    # Threads (standard CMake module)
    Threads::Threads
    # cpp-httplib (if FetchContent creates a target, otherwise it's header-only)
    # If cpp-httplib was not made available as a target, it's purely header-only.
    # If cpphttplib_SOURCE_DIR was used with add_subdirectory, then link cpp-httplib target.
    # For FetchContent_MakeAvailable, usually not needed for header-only.
)

# Platform-specific linker flags (e.g., for pthreads on Linux)
# Threads::Threads should handle this, but can be explicit if needed.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT APPLE) # Apple usually links pthreads automatically
    # target_link_libraries(event_queue_server PRIVATE pthread) # Often covered by Threads::Threads
endif()

# --- TCP Client Executable (Example) ---
if(EXISTS "${CLIENT_DIR}/TcpClient.cpp" AND EXISTS "${CLIENT_DIR}/main_tcp_client.cpp")
    add_executable(event_queue_tcp_client
        ${CLIENT_DIR}/main_tcp_client.cpp
        ${CLIENT_DIR}/TcpClient.cpp
    )
    # TcpClient.h includes NetworkProtocol.h and Message.h
    target_link_libraries(event_queue_tcp_client PRIVATE
        Boost::system
        Threads::Threads # If client uses threads
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT APPLE)
        # target_link_libraries(event_queue_tcp_client PRIVATE pthread)
    endif()
else()
    message(STATUS "TCP Client source files not found, skipping target.")
endif()

# --- HTTP Client Executable (Example) ---
if(EXISTS "${CLIENT_DIR}/main_http_client.cpp") # Assuming it's standalone or uses httplib.h
    add_executable(event_queue_http_client
        ${CLIENT_DIR}/main_http_client.cpp
    )
    # main_http_client.cpp likely includes httplib.h and nlohmann/json.hpp
    target_link_libraries(event_queue_http_client PRIVATE
        OpenSSL::SSL      # For HTTPS client
        OpenSSL::Crypto
        Threads::Threads  # cpp-httplib client might use threads
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT APPLE)
        # target_link_libraries(event_queue_http_client PRIVATE pthread)
    endif()
else()
    message(STATUS "HTTP Client source file not found, skipping target.")
endif()


# --- Installation (Optional but good practice) ---
# This defines where the executables and config file would be installed.
# include(GNUInstallDirs)
# install(TARGETS event_queue_server
#         RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# install(FILES ${PROJECT_SOURCE_DIR}/config.yaml
#         DESTINATION ${CMAKE_INSTALL_SYSCONFDIR} # Or CMAKE_INSTALL_DATADIR
#         RENAME event_queue_server.yaml) # Example rename


# --- Compiler Warnings (Good Practice) ---
if(MSVC)
    # /W4 is a good level, /Wall is often too noisy
    # /WX treats warnings as errors
    target_compile_options(event_queue_server PRIVATE /W4)
    # target_compile_options(event_queue_core_lib INTERFACE /W4) # For library users
else() # GCC/Clang
    target_compile_options(event_queue_server PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)
    # target_compile_options(event_queue_core_lib INTERFACE -Wall -Wextra -Wpedantic)
    # Consider adding -Werror for CI builds
endif()

# --- CTest (Optional, for adding tests later) ---
# enable_testing()
# add_test(NAME MyServerTest COMMAND event_queue_server --config ${CMAKE_CURRENT_SOURCE_DIR}/test_config.yaml)
# add_subdirectory(tests) # If you have a tests directory with its own CMakeLists.txt

message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost library dirs: ${Boost_LIBRARY_DIRS}") # Might be empty if using imported targets
message(STATUS "OpenSSL include dir: ${OpenSSL_INCLUDE_DIR}")
message(STATUS "OpenSSL libraries: ${OpenSSL_LIBRARIES}") # SSL;Crypto
message(STATUS "yaml-cpp include dirs: ${YAML_CPP_INCLUDE_DIRS}")
message(STATUS "yaml-cpp libraries: ${YAML_CPP_LIBRARIES}") # Should be 'yaml-cpp' target

# --- Output build type ---
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "No build type specified; defaulting to Release for some compilers or Debug for others. Consider -DCMAKE_BUILD_TYPE=Release or Debug.")
endif()