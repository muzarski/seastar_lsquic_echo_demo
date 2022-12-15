cmake_minimum_required(VERSION 3.23)
project(seastar_echo_quic)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_COMPILER /usr/bin/clang++)
set(CMAKE_CXX_STANDARD 20)
#set(CMAKE_CXX_FLAGS "-O3")

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

add_executable(echo_server src/echo_server.cpp src/ssl_handler.cpp)
target_include_directories(echo_server PRIVATE ${INCLUDE_FILES_DIR})
target_link_libraries(echo_server ${LIBS})
target_compile_definitions(echo_server PRIVATE DATAGRAM_SIZE=${DATAGRAMSIZE})

# message(WARNING "Client is not working yet. Ignore it...")
add_executable(echo_client src/echo_client.cpp)
target_include_directories(echo_client PRIVATE ${INCLUDE_FILES_DIR})
target_link_libraries(echo_client ${LIBS})
target_compile_definitions(echo_client PRIVATE DATAGRAM_SIZE=${DATAGRAMSIZE})
