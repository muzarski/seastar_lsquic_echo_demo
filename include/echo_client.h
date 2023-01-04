#ifndef SEASTAR_ECHO_QUIC_ECHO_CLIENT_H
#define SEASTAR_ECHO_QUIC_ECHO_CLIENT_H

#include <lsquic/lsquic.h>
#include "logger.h"
#include <seastar/core/future.hh>
#include <seastar/core/reactor.hh>

using namespace zpp;
using namespace seastar::net;

class Client {
private:
    udp_channel channel;
    seastar::socket_address server_address;
    seastar::timer<> timer;
    seastar::timer<> status_timer;
    seastar::future<> udp_send_queue;
    seastar::future<> read_udp_future;
    seastar::future<> client_flow_loop_future;
    lsquic_engine_t *engine{};
    lsquic_conn_t *conn{};
    lsquic_stream_t *stream{};
    size_t bytes_read = 0;
    size_t seconds_elapsed = 0;
    
    seastar::future<> timer_expired();
    seastar::future<> process_conns();

    seastar::future<> handle_receive(udp_datagram &&datagram);

    seastar::future<> read_udp();

    // Lsquic Callbacks...
    static int tut_packets_out(void *packets_out_ctx, const lsquic_out_spec *specs, unsigned int count);

    static lsquic_conn_ctx_t * tut_client_on_new_conn (void *stream_if_ctx, struct lsquic_conn *conn);
    static void tut_client_on_hsk_done (lsquic_conn_t *conn, enum lsquic_hsk_status status);
    static void tut_client_on_conn_closed (struct lsquic_conn *conn);
    static lsquic_stream_ctx_t *tut_client_on_new_stream (void *stream_if_ctx, struct lsquic_stream *stream);
    static void tut_client_on_read (struct lsquic_stream *stream, lsquic_stream_ctx_t *h);
    static void tut_client_on_write (struct lsquic_stream *stream, lsquic_stream_ctx_t *h);
    static void tut_client_on_close (struct lsquic_stream *stream, lsquic_stream_ctx_t *h);

public:
    Client(const std::string &host, std::uint16_t port);
    void init_lsquic();
    seastar::future<> service_loop();
};

#endif //SEASTAR_ECHO_QUIC_ECHO_CLIENT_H
