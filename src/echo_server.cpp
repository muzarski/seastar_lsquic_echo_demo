#include <iostream>
#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/util/log.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/timer.hh>

#include <logger.h>
#include <ssl_handler.h>

#include <lsquic/lsquic.h>

using namespace seastar::net;
using namespace zpp;

static SSL_CTX *s_ssl_ctx;

static int
tut_log_buf (void *ctx, const char *buf, std::size_t len) {
    FILE *out = static_cast<FILE *>(ctx);
    fwrite(buf, 1, len, out);
    fflush(out);
    return 0;
}
static const struct lsquic_logger_if logger_if = { tut_log_buf, };

static lsquic_conn_ctx_t *tut_server_on_new_conn(void *stream_if_ctx, lsquic_conn *conn) {
    log("Created a new connection");
    return nullptr;
}


static void tut_server_on_conn_closed(lsquic_conn_t *conn) {
    log("Closed connection");
}


struct tut_server_stream_ctx {
    std::size_t     tssc_sz;            /* Number of bytes in tsc_buf */
    off_t           tssc_off;           /* Number of bytes written to stream */
    unsigned char   tssc_buf[0x100];    /* Bytes read in from client */
};


static lsquic_stream_ctx_t *tut_server_on_new_stream(void *stream_if_ctx, lsquic_stream *stream) {
    /* Allocate a new buffer per stream.  There is no reason why the echo
     * server could not process several echo streams at the same time.
     */
    auto *tssc = reinterpret_cast<tut_server_stream_ctx*>(malloc(sizeof(tut_server_stream_ctx)));

    if (!tssc) {
        log("Cannot allocate server stream context");
        ::lsquic_conn_abort(::lsquic_stream_conn(stream));
        return nullptr;
    }

    tssc->tssc_sz = 0;
    tssc->tssc_off = 0;
    ::lsquic_stream_wantread(stream, 1);
    log("Created a new echo stream -- want to read");
    return reinterpret_cast<lsquic_stream_ctx_t*>(tssc);
}

/* Read until newline and then echo it back */
static void tut_server_on_read(lsquic_stream *stream, lsquic_stream_ctx_t *h) {
    auto *const tssc = reinterpret_cast<tut_server_stream_ctx *const>(h);
    unsigned char buf[1];

    ssize_t nread = ::lsquic_stream_read(stream, buf, sizeof(buf));

    if (nread > 0) {
        tssc->tssc_buf[tssc->tssc_sz] = buf[0];
        ++tssc->tssc_sz;
        if (buf[0] == '\n' || tssc->tssc_sz == sizeof(tssc->tssc_buf)) {
            log("Read newline or filled buffer, switch to writing");
            tssc->tssc_buf[tssc->tssc_sz] = '\0';
            std::cerr << "Message from stream is " << tssc->tssc_buf << "\n";
            ::lsquic_stream_wantread(stream, 0);
            ::lsquic_stream_wantwrite(stream, 1);
        }
    } else if (nread == 0) {
        log("Read EOF");
        ::lsquic_stream_shutdown(stream, 0);
        if (tssc->tssc_sz) {
            ::lsquic_stream_wantwrite(stream, 1);
        }
    }
    else {
        /* This should not happen */
        log("Error reading from stream, abort connection");
        ::lsquic_conn_abort(::lsquic_stream_conn(stream));
    }
}


static void tut_server_on_write(lsquic_stream *stream, lsquic_stream_ctx_t *h) {
    using std::literals::string_literals::operator""s;

    auto *const tssc = reinterpret_cast<tut_server_stream_ctx *const>(h);
    ssize_t nw = ::lsquic_stream_write(stream, tssc->tssc_buf + tssc->tssc_off,
                                       tssc->tssc_sz - tssc->tssc_off);

    if (nw > 0) {
        tssc->tssc_off += nw;
        if (tssc->tssc_off == tssc->tssc_sz) {
            log("Wrote all "s + std::to_string(nw) + " bytes to the stream");
            std::cerr << "Wrote " << tssc->tssc_buf << "\n";

            // The exemplary client from https://github.com/litespeedtech/lsquic/blob/master/bin/echo_client.c
            // uses only one stream. If we closed the stream, the client would then close the connection.
            // We have to flush the stream and tell the engine that we want to switch to reading.
            lsquic_stream_flush(stream);
            ::lsquic_stream_wantwrite(stream, 0);
            ::lsquic_stream_wantread(stream, 1);

            // However, the client from https://github.com/dtikhonov/lsquic-tutorial uses one stream per message.
            // In this case we want to close the stream (send the `fin` byte).
            // ::lsquic_stream_close(stream);
        } else {
            log("Wrote all "s + std::to_string(nw) + " bytes to the stream; "
                "still have " + std::to_string(tssc->tssc_sz - tssc->tssc_off) + " bytes to write");
        }
    } else {
        /* When `on_write()' is called, the library guarantees that at least
         * something can be written.  If not, that's an error whether 0 or -1
         * is returned.
         */
        log("stream_write() returned "s + std::to_string(nw) + ". Aborting the connection");
        ::lsquic_conn_abort(::lsquic_stream_conn(stream));
    }
}

static void tut_server_on_close(lsquic_stream *stream, lsquic_stream_ctx_t *h) {
    auto *const tssc = reinterpret_cast<tut_server_stream_ctx *const>(h);
    free(tssc);
    log("Stream closed");
}

static lsquic_stream_if tut_server_callbacks = {
        .on_new_conn        = tut_server_on_new_conn,
        .on_conn_closed     = tut_server_on_conn_closed,
        .on_new_stream      = tut_server_on_new_stream,
        .on_read            = tut_server_on_read,
        .on_write           = tut_server_on_write,
        .on_close           = tut_server_on_close,
};

class Server {
private:
    udp_channel channel;
    lsquic_engine *engine{};
    seastar::future<> udp_send_queue;
    seastar::timer<> timer;

    seastar::future<> timer_expired() {
        std::cerr << "Timer expired\n";
        return process_conns();
    }

    seastar::future<> process_conns() {
        int diff;
        int64_t timeout;

        std::cerr << "Ticking engine...\n";
        lsquic_engine_process_conns(engine);

        if (lsquic_engine_earliest_adv_tick(engine, &diff)) {
            if (diff >= LSQUIC_DF_CLOCK_GRANULARITY) {
                timeout = diff / 1000;
            }
            else if (diff <= 0) {
                timeout = 0;
            }
            else {
                timeout = LSQUIC_DF_CLOCK_GRANULARITY / 1000;
            }

            std::cerr << "There will be tickable connections in " << timeout << " millis. Scheduling the timer.\n";
            timer.rearm(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout), std::nullopt);
        }
        else {
            std::cerr << "There are NO tickable connections.\n";
        }
        return seastar::make_ready_future<>();
    }

    seastar::future<> handle_receive(udp_datagram &&datagram) {
        std::cerr << "Read " << datagram.get_data().len() << " bytes from udp channel\n";

        char buf[DATAGRAM_SIZE];
        memcpy(buf, datagram.get_data().fragment_array()->base, datagram.get_data().len());
        buf[datagram.get_data().len()] = '\0';

        std::cerr << "l1 " << datagram.get_dst() << "\n";
        std::cerr << "l2 " << channel.local_address() << "\n";
        std::cerr << "r " << datagram.get_src() << "\n";

        int packet_in_res = lsquic_engine_packet_in(engine, reinterpret_cast<const unsigned char *>(buf),
                                                    datagram.get_data().len(), &channel.local_address().as_posix_sockaddr(),
                                                    &datagram.get_src().as_posix_sockaddr(), this, 0);

        if (packet_in_res == 0) {
            std::cerr << "Packet processed by a connection\n";
        } else if (packet_in_res == 1) {
            std::cerr << "Packet processed, but not by a connection\n";
        }
        else {
            std::cerr << "Packet processing failed\n";
        }

        return process_conns();
    }

    static int tut_packets_out(void *packets_out_ctx, const lsquic_out_spec *specs, unsigned int count) {
        std::cerr << "In packets_out callback... - " << count << " packages to send\n";
        if (count == 0) {
            return 0;
        }

        for (size_t i = 0; i < count; ++i) {
            auto *server = reinterpret_cast<Server*>(specs[i].peer_ctx);
            std::unique_ptr<char> data(new char[specs[i].iov->iov_len]);
            std::memcpy(data.get(), specs[i].iov->iov_base, specs[i].iov->iov_len);
            server->udp_send_queue = server->udp_send_queue.then(
                    [server, data = std::move(data), dst = *specs[i].dest_sa, data_len = specs[i].iov->iov_len] () {
                        seastar::socket_address addr(*(sockaddr_in *) &dst);
                        std::cerr << "sending " << data_len << " bytes of data to " << addr << " via udp channel.\n";
                        return server->channel.send(addr, seastar::temporary_buffer<char>(data.get(), data_len));
            });
        }

        return count;
    }

    static SSL_CTX *tut_get_ssl_ctx (void *peer_ctx, const struct sockaddr *local) {
        return s_ssl_ctx;
    }


public:
    explicit Server(std::uint16_t listen_port) :
        channel(seastar::make_udp_channel(listen_port)),
        udp_send_queue(seastar::make_ready_future<>()),
        timer() {
    };

    void init_lsquic() {
        std::cout << "Initializing lsquic engine\n";
        if (0 != ::lsquic_global_init(LSQUIC_GLOBAL_SERVER|LSQUIC_GLOBAL_CLIENT)) {
            fail("Initialization of the engine has failed");
        }

        lsquic_set_log_level("debug");
        lsquic_logger_init(&logger_if, stderr, LLTS_HHMMSSUS);

        const char *cert_file = "../ssl/mycert-cert.pem";
        const char *key_file = "../ssl/mycert-key.pem";
        if (0 != tut_load_cert(cert_file, key_file, &s_ssl_ctx)) {
            fail("Cannot load the certificates");
        }

        ::lsquic_engine_settings settings{};
        ::lsquic_engine_init_settings(&settings, LSENG_SERVER);

        char errbuf[0x100];

        settings.es_ql_bits = 0;
        if (0 != ::lsquic_engine_check_settings(&settings,
                                                LSENG_SERVER,
                                                errbuf, sizeof(errbuf))) {
            fail("Invalid settings");
        }

        lsquic_engine_api eapi{};
        std::memset(&eapi, 0, sizeof(eapi));
        eapi.ea_packets_out = tut_packets_out;
        eapi.ea_packets_out_ctx = this;
        eapi.ea_stream_if = &tut_server_callbacks;
        eapi.ea_stream_if_ctx = this;
        eapi.ea_get_ssl_ctx = tut_get_ssl_ctx;
        eapi.ea_settings = &settings;

        engine = ::lsquic_engine_new(LSENG_SERVER, &eapi);
        if (!engine) {
            fail("Creating an engine has failed");
        }

        if (!s_ssl_ctx) {
            std::cerr << "SSL CTX IS NULL!!!!!!!\n";
        }
    }

    seastar::future<> service_loop() {
        timer.set_callback([this] {
           return timer_expired();
        });
        return seastar::keep_doing([this] () {
            std::cerr << "Waiting for some input...\n";
            return channel.receive().then([this] (udp_datagram datagram) {
                return handle_receive(std::move(datagram));
            });
        });
    }
};

seastar::future<> submit_to_cores(uint16_t port) {
    return seastar::parallel_for_each(boost::irange<unsigned>(0, seastar::smp::count),
            [port] (unsigned core) {
        return seastar::smp::submit_to(core, [port] () {
            Server _server(port);
            return seastar::do_with(std::move(_server), [] (Server &server) {
                server.init_lsquic();
                return server.service_loop();
            });
        });
    });
}

int main(int argc, char **argv) {
    seastar::app_template app;

    namespace po = boost::program_options;
    app.add_options()
            ("port", po::value<std::uint16_t>()->required(), "listen port");


    try {
        app.run(argc, argv, [&] () {
            auto&& config = app.configuration();
            std::uint16_t port = config["port"].as<std::uint16_t>();
            return submit_to_cores(port);
        });
    } catch(...) {
        std::cerr << "Couldn't start application: " << std::current_exception() << '\n';
        return 1;
    }
    return 0;
}
