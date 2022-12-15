#include <client.h>

#include <cstdio>
#include <lsquic/lsquic.h>

#include <seastar/core/future.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/seastar.hh>
#include <seastar/net/api.hh>
#include <seastar/net/socket_defs.hh>

#include <logger.h>
#include <client_callbacks.h>
#include <client_stream_ctx.h>

#include <chrono>
#include <cstring>  // std::memset, std::memcpy
#include <iostream>
#include <stdexcept>

namespace zpp {

namespace {

constinit lsquic_stream_if client_callbacks = {
    .on_new_conn    = client_on_new_connection,
    .on_conn_closed = client_on_connection_closed,
    .on_new_stream  = client_on_new_stream,
    .on_read        = client_on_read,
    .on_write       = client_on_write,
    .on_close       = client_on_close,
    .on_hsk_done    = client_on_handshake_done
};

} // anonymous namespace

client::client(const std::string &address, std::uint16_t port)
: m_channel(seastar::make_udp_channel())
, m_server_address(seastar::ipv4_addr(address, port))
, m_send_queue(seastar::make_ready_future<>())
, m_timer() {}

void client::init() {
    lsquic_engine_settings settings;
    lsquic_engine_init_settings(&settings, 0);
    
    char error_buffer[0x100];
    if (lsquic_engine_check_settings(&settings, 0, error_buffer, sizeof(error_buffer))) {
        ffail(error_buffer);
    }

    lsquic_engine_api eapi;
    std::memset(&eapi, 0, sizeof(eapi));

    eapi.ea_packets_out     =  client_packets_out;
    eapi.ea_packets_out_ctx =  this;
    eapi.ea_stream_if       = &client_callbacks;
    eapi.ea_stream_if_ctx   =  this;
    eapi.ea_settings        = &settings;
    eapi.ea_alpn            =  "echo";

    m_engine = lsquic_engine_new(0, &eapi);
    if (!m_engine) {
        ffail("Creating an lsquic engine has failed.");
    }
}

seastar::future<> client::service_loop() {
    m_timer.set_callback([this]() {
        return timer_expired();
    });

    this->connect();

    if (!m_connection) {
        ffail("Creating a connection has failed.");
    }

    (void) process_connections();
    return read_udp();
}

void client::connect() {
    if (m_connected) {
        throw std::runtime_error("The client is already connected to the server.");
    }

    m_connection = lsquic_engine_connect(
        m_engine,
        N_LSQVER,
        &m_channel.local_address().as_posix_sockaddr(),
        &m_server_address.as_posix_sockaddr(),
        nullptr,
        nullptr,
        nullptr,
        0,
        nullptr,
        0,
        nullptr,
        0
    );

    if (!m_connection) {
        eflog("Cannot connect to the server.");
        throw std::runtime_error("Cannot connect to the server.");
    }

    m_connected = true;
}

seastar::future<> client::timer_expired() {
    flog("The timer has expired.");
    return process_connections();
}

seastar::future<> client::process_connections() {
    flog("Ticking the engine.");
    lsquic_engine_process_conns(m_engine);

    int diff;
    std::int64_t timeout;

    if (lsquic_engine_earliest_adv_tick(m_engine, &diff)) {
        if (diff >= LSQUIC_DF_CLOCK_GRANULARITY) {
            timeout = diff / 1000;
        } else if (diff <= 0) {
            timeout = 0;
        } else {
            timeout = LSQUIC_DF_CLOCK_GRANULARITY / 1000;
        }

        flog("There will be tickable connections in ", timeout, " ms. Scheduling the timer.");
        m_timer.rearm(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout));
    } else {
        flog("There are no tickable connections.");
    }

    return seastar::make_ready_future<>();
}

seastar::future<> client::read_input() {
    flog("Reading input.");
    
    client_stream_ctx *cs_ctx = reinterpret_cast<client_stream_ctx*>(lsquic_stream_get_ctx(m_stream));

    std::string s;
    std::cin >> s;

    std::memcpy(cs_ctx->m_send_buffer, s.c_str(), s.length());
    cs_ctx->m_send_buffer[s.length()] = '\n';
    cs_ctx->m_send_msg_size = s.length() + 1;

    lsquic_stream_wantwrite(m_stream, 1);
    return process_connections();
}

seastar::future<> client::handle_receive(seastar::net::udp_datagram &&datagram) {
    flog("Read ", datagram.get_data().len(), " bytes from a UDP channel.");

    char buffer[DATAGRAM_SIZE];
    std::memcpy(buffer, datagram.get_data().fragment_array()->base, datagram.get_data().len());
    buffer[datagram.get_data().len()] = '\0';

    auto result = lsquic_engine_packet_in(
        m_engine,
        reinterpret_cast<const unsigned char*>(buffer),
        datagram.get_data().len(),
        &m_channel.local_address().as_posix_sockaddr(),
        &datagram.get_src().as_posix_sockaddr(),
        this,
        0
    );

    switch (result) {
        case 0:
            flog("Packet processed by a connection.");
            break;
        case 1:
            eflog("Packet processed, but not by a connection.");
            break;
        default:
            eflog("Packet processing failed.");
    }

    if (m_read_input) {
        m_read_input = false;
        return read_input();
    }

    return process_connections();
}

// Change the type to R-value?
seastar::future<> client::read_udp() {
    return seastar::keep_doing([this] {
        return m_channel.receive().then([this](seastar::net::udp_datagram datagram) {
            return handle_receive(std::move(datagram));
        });
    });
}

} // namespace zpp
