cmake_minimum_required(VERSION 3.23)
project(seastar_echo_quic)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_COMPILER /usr/bin/clang++)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "-g -Wall -Wextra -Wshadow -Wpedantic")

find_package(Seastar REQUIRED)

find_library(FMT_LIB NAMES fmt HINTS ENV FMT_V8_LIB_HOME REQUIRED)
if(FMT_LIB)
    message(STATUS "Found fmt library v8 - ${FMT_LIB}")
endif()

set(BORINGSSL_DIR $ENV{BORINGSSL_LIB_HOME}) # Adjust the path.
set(BORINGSSL_INCLUDE_DIR ${BORINGSSL_DIR}/include)

# Find boringssl crypto library
set(BORINGSSL_CRYPTO_LIB_NAME libcrypto.a) # One can change to search for dynamic library (.so)
set(BORINGSSL_CRYPTO_LIB_DIR ${BORINGSSL_DIR}/crypto) # Adjust the path.
message(STATUS "Searching for boringssl crypto library (${BORINGSSL_CRYPTO_LIB_NAME}) in ${BORINGSSL_CRYPTO_LIB_DIR}...")
find_library(
        BORINGSSL_CRYPTO_LIB
        NAMES ${BORINGSSL_CRYPTO_LIB_NAME}
        HINTS ${BORINGSSL_CRYPTO_LIB_DIR}
        REQUIRED
)
if(BORINGSSL_CRYPTO_LIB)
    message(STATUS "boringssl crypto library found - ${BORINGSSL_CRYPTO_LIB}")
endif()

# Find boringssl ssl library
set(BORINGSSL_SSL_LIB_NAME libssl.a) # One can change to search for dynamic library (.so)
set(BORINGSSL_SSL_LIB_DIR ${BORINGSSL_DIR}/ssl)
message(STATUS "Searching for boringssl ssl library (${BORINGSSL_SSL_LIB_NAME}) in ${BORINGSSL_SSL_LIB_DIR}...")
find_library(
        BORINGSSL_SSL_LIB
        NAMES ${BORINGSSL_SSL_LIB_NAME}
        HINTS ${BORINGSSL_SSL_LIB_DIR}
        REQUIRED
)
if(BORINGSSL_SSL_LIB)
    message(STATUS "boringssl ssl library found - ${BORINGSSL_SSL_LIB}")
endif()

message(STATUS "Searching for zlib package...")
find_package(ZLIB REQUIRED)
if(ZLIB_FOUND)
    message(STATUS "zlib package found - ${ZLIB_DIR}")
endif()

message(STATUS "Searching for lsquic library...")
find_library(LSQUIC_LIB lsquic REQUIRED)
if(LSQUIC_LIB)
    message(STATUS "lsquic library found - ${LSQUIC_LIB}")
endif()

list(APPEND INCLUDE_FILES_DIR include ${BORINGSSL_INCLUDE_DIR})
list(APPEND LIBS ${LSQUIC_LIB} ${BORINGSSL_SSL_LIB} ${BORINGSSL_CRYPTO_LIB} Seastar::seastar ${FMT_LIB} ZLIB::ZLIB)

set(DATAGRAMSIZE 3000)

set(SERVER_EXEC_NAME echo_server)
set(CLIENT_EXEC_NAME echo_client)

set(SERVER_SRC
    src/echo_server.cpp
    src/ssl_handler.cpp
)
set(CLIENT_SRC
    src/echo_client.cpp
    src/client.cpp
    src/client_callbacks.cpp
)

add_executable(${SERVER_EXEC_NAME} ${SERVER_SRC})
target_include_directories(${SERVER_EXEC_NAME} PRIVATE ${INCLUDE_FILES_DIR})
target_link_libraries(${SERVER_EXEC_NAME} ${LIBS})
target_compile_definitions(${SERVER_EXEC_NAME} PRIVATE DATAGRAM_SIZE=${DATAGRAMSIZE})
target_compile_definitions(${SERVER_EXEC_NAME} PRIVATE PROJECT_ROOT_PATH="${PROJECT_SOURCE_DIR}")

add_executable(${CLIENT_EXEC_NAME} ${CLIENT_SRC})
target_include_directories(${CLIENT_EXEC_NAME} PRIVATE ${INCLUDE_FILES_DIR})
target_link_libraries(${CLIENT_EXEC_NAME} ${LIBS})
target_compile_definitions(${CLIENT_EXEC_NAME} PRIVATE DATAGRAM_SIZE=${DATAGRAMSIZE})
target_compile_definitions(${CLIENT_EXEC_NAME} PRIVATE PROJECT_ROOT_PATH="${PROJECT_SOURCE_DIR}")
