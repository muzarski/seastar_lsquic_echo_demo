#ifndef __TUT_CLIENT_H__
#define __TUT_CLIENT_H__

#include <seastar/net/api.hh>
#include <seastar/net/socket_defs.hh>

#include <lsquic/lsquic.h>

#include <cstddef>      // std::uint16_t
#include <string>

namespace zpp {

class client {
private:
    seastar::net::udp_channel    m_channel;
    seastar::socket_address      m_server_address;
    seastar::future<>            m_send_queue;
    seastar::timer<>             m_timer;
    lsquic_engine_t             *m_engine           = nullptr;
    lsquic_conn_t               *m_connection       = nullptr;
    lsquic_stream_t             *m_stream           = nullptr;
    bool                         m_connected        = false;
    bool                         m_read_input       = false;

private:
    friend int                   client_packets_out(void*, const lsquic_out_spec*, unsigned int);
    friend lsquic_stream_ctx_t  *client_on_new_stream(void*, lsquic_stream_t*);
    friend void                  client_on_read(lsquic_stream_t*, lsquic_stream_ctx_t*);

public:
    explicit client(const std::string &address, std::uint16_t port);
    void init();
    seastar::future<> service_loop();

private:
    void connect();
    seastar::future<> timer_expired();
    seastar::future<> process_connections();
    seastar::future<> read_input();
    seastar::future<> handle_receive(seastar::net::udp_datagram &&datagram);
    seastar::future<> read_udp();
};

} // namespace zpp

#endif // __TUT_CLIENT_H__
