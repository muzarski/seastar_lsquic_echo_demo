#include <iostream>
#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/sleep.hh>

#include <logger.h>
#include <echo_client.h>

#include <lsquic/lsquic.h>

const size_t MAX_UDP_PAYLOAD_SIZE = 65507;

using namespace seastar::net;
using namespace zpp;

bool connection_closed = false;
bool first_stream_shutdown = false;

static int tut_log_buf (void *ctx, const char *buf, size_t len) {
    FILE *out = static_cast<FILE *>(ctx);
    fwrite(buf, 1, len, out);
    fflush(out);
    return 0;
}


static const struct lsquic_logger_if logger_if = { tut_log_buf, };

lsquic_conn_ctx_t* Client::tut_client_on_new_conn (void *stream_if_ctx, struct lsquic_conn *conn) {
    log("created connection");
    return nullptr;
}


void Client::tut_client_on_hsk_done (lsquic_conn_t *conn, enum lsquic_hsk_status status) {
    //struct tut *const tut = (void *) lsquic_conn_get_ctx(conn);

    switch (status) {
        case LSQ_HSK_OK:
        case LSQ_HSK_RESUMED_OK:
            log("handshake successful, start stdin watcher");
            break;
        default:
            log("handshake failed");
            break;
    }
    
    static const size_t n_streams = 1;
    std::cerr << "Creating " << n_streams << " streams" << std::endl;
    for (size_t i = 0; i < n_streams; ++i) {
        lsquic_conn_make_stream(conn);
    }
}


void Client::tut_client_on_conn_closed (struct lsquic_conn *conn) {
    log("client connection closed -- stop reading from socket");
    connection_closed = true;
}


struct lsquic_stream_ctx {
    Client *client;
    lsquic_stream *stream;
    char write_buf[0x100];
    size_t write_buf_off;
    char read_buf[0x100];
    size_t read_buf_off;
};


lsquic_stream_ctx_t* Client::tut_client_on_new_stream (void *stream_if_ctx, struct lsquic_stream *stream) {
    log("created new stream, we want to write");
    auto *client = reinterpret_cast<Client*>(stream_if_ctx);
    if (client == nullptr) {
        log("client is null");
    }

    auto *stream_ctx = new lsquic_stream_ctx{};

    stream_ctx->client = client;
    stream_ctx->stream = stream;
    stream_ctx->write_buf_off = 0;
    stream_ctx->read_buf_off = 0;

    client->stream = stream;

    lsquic_stream_shutdown(stream, 1); // don't write 
    lsquic_stream_wantread(stream, 1); // want to read
    std::cerr << "Returning stream context" << std::endl;
    return stream_ctx;
}


/* Echo whatever comes back from server, no verification */
void Client::tut_client_on_read (struct lsquic_stream *stream, lsquic_stream_ctx_t *h) {
    static char buf[MAX_UDP_PAYLOAD_SIZE];
    
    if (first_stream_shutdown) {
        lsquic_stream_wantread(stream, 0);
        lsquic_stream_shutdown(stream, 0);
        return;
    }

    ssize_t nread = ::lsquic_stream_read(stream, buf, sizeof(buf));

    if (nread > 0) {
        // std::cerr << "Read " << nread << " bytes from stream " << lsquic_stream_id(stream) << std::endl;
        h->client->bytes_read += nread;
    } else if (nread == 0) {
        log("Read EOF");
        lsquic_stream_wantread(stream, 0);
        ::lsquic_stream_shutdown(stream, 0);
    }
    else {
        /* This should not happen */
        log("Error reading from stream, abort connection");
        ::lsquic_conn_abort(::lsquic_stream_conn(stream));
    }
}


/* Write out the whole line to stream, shutdown write end, and switch
 * to reading the response.
 */
void Client::tut_client_on_write (struct lsquic_stream *stream, lsquic_stream_ctx_t *h) {
    // ssize_t nw;

    lsquic_stream_write(stream, h->write_buf, h->write_buf_off);
    h->write_buf_off = 0;

    lsquic_stream_flush(stream);
    lsquic_stream_wantwrite(stream, 0);
    lsquic_stream_wantread(stream, 1);
    h->read_buf_off = 0;
}


void Client::tut_client_on_close (struct lsquic_stream *stream, lsquic_stream_ctx_t *h) {
    std::cerr << "stream closed\n";
    first_stream_shutdown = true;
    free(h);
}


seastar::future<> Client::timer_expired() {
    // std::cerr << "Timer expired\n";
    return process_conns();
}


seastar::future<> Client::process_conns() {
    int diff;
    int64_t timeout;

    // std::cerr << "Ticking engine...\n";
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

        // std::cerr << "There will be tickable connections in " << timeout << " millis. Scheduling the timer.\n";
        timer.rearm(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout), std::nullopt);
    }
    else {
        // std::cerr << "There are NO tickable connections.\n";
    }

    return seastar::make_ready_future<>();
}


seastar::future<> Client::handle_receive(udp_datagram &&datagram) {
    // std::cerr << "Read " << datagram.get_data().len() << " bytes from udp channel\n";

    char buf[DATAGRAM_SIZE];
    memcpy(buf, datagram.get_data().fragment_array()->base, datagram.get_data().len());
    buf[datagram.get_data().len()] = '\0';

    int packet_in_res = lsquic_engine_packet_in(engine, reinterpret_cast<const unsigned char *>(buf),
                                                datagram.get_data().len(), &channel.local_address().as_posix_sockaddr(),
                                                &datagram.get_src().as_posix_sockaddr(), this, 0);

//    if (packet_in_res == 0) {
//        std::cerr << "Packet processed by a connection\n";
//    } else if (packet_in_res == 1) {
//        std::cerr << "Packet processed, but not by a connection\n";
//    }
//    else {
//        std::cerr << "Packet processing failed\n";
//    }

    return process_conns();
}


seastar::future<> Client::read_udp() {
    return seastar::do_until([this] { return connection_closed; }, [this] {
        return channel.receive().then([this](udp_datagram &&datagram) {
            return handle_receive(std::move(datagram));
        });
    }).then([this] {
        std::cerr << "Connection closed, closing the channel\n";
        timer.cancel();
        status_timer.cancel();
        return channel.close();
    });
}


int Client::tut_packets_out(void *packets_out_ctx, const lsquic_out_spec *specs, unsigned int count) {
    // std::cerr << "in packets out... - " << count << " packages to send\n";
    if (count == 0) {
        return 0;
    }

    for (size_t i = 0; i < count; ++i) {
        auto *client = reinterpret_cast<Client*>(specs[i].peer_ctx);
        std::unique_ptr<char> data(new char[specs[i].iov->iov_len]);
        std::memcpy(data.get(), specs[i].iov->iov_base, specs[i].iov->iov_len);
        client->udp_send_queue = client->udp_send_queue.then(
                [client, data = std::move(data), dst = *specs[i].dest_sa, data_len = specs[i].iov->iov_len] () {
                    seastar::socket_address addr(*(sockaddr_in *) &dst);
                    // std::cerr << "sending " << data_len << " bytes of data to " << addr << "\n";
                    return client->channel.send(client->server_address,
                                                seastar::temporary_buffer<char>(data.get(), data_len));
                });
    }

    return count;
}


Client::Client(const std::string &host, std::uint16_t port) :
    channel(seastar::make_udp_channel()),
    server_address(seastar::ipv4_addr(host, port)),
    timer(),
    status_timer(),
    udp_send_queue(seastar::make_ready_future<>()),
    read_udp_future(seastar::make_ready_future<>()),
    client_flow_loop_future(seastar::make_ready_future<>()) {
}


void Client::init_lsquic() {
    std::cout << "Initializing lsquic engine\n";
    if (0 != ::lsquic_global_init(LSQUIC_GLOBAL_CLIENT)) {
        fail("Initialization of the engine has failed");
    }

//    lsquic_set_log_level("debug");
//    lsquic_logger_init(&logger_if, stderr, LLTS_HHMMSSUS);

    ::lsquic_engine_settings settings{};
    ::lsquic_engine_init_settings(&settings, 0);

    char errbuf[0x100];

    settings.es_ql_bits = 0;
    if (0 != ::lsquic_engine_check_settings(&settings,
                                            0,
                                            errbuf, sizeof(errbuf))) {
        fail("Invalid settings");
    }

    static struct lsquic_stream_if tut_client_callbacks = {
            .on_new_conn        = tut_client_on_new_conn,
            .on_conn_closed     = tut_client_on_conn_closed,
            .on_new_stream      = tut_client_on_new_stream,
            .on_read            = tut_client_on_read,
            .on_write           = tut_client_on_write,
            .on_close           = tut_client_on_close,
            .on_hsk_done        = tut_client_on_hsk_done,
    };

    lsquic_engine_api eapi{};
    std::memset(&eapi, 0, sizeof(eapi));
    eapi.ea_packets_out = tut_packets_out;
    eapi.ea_packets_out_ctx = this;
    eapi.ea_stream_if = &tut_client_callbacks;
    eapi.ea_stream_if_ctx = this;
    eapi.ea_settings = &settings;
    eapi.ea_alpn = "echo";

    engine = ::lsquic_engine_new(0, &eapi);
    if (!engine) {
        fail("Creating an engine has failed");
    }
}


seastar::future<> Client::service_loop() {
    timer.set_callback([this] () {
        return timer_expired();
    });
    
    status_timer.set_callback([this] () {
        ++seconds_elapsed;
        std::cerr << "Download speed " << bytes_read / seconds_elapsed << " bytes/sec at core" << 
            seastar::this_shard_id() << "\n";
    });
    status_timer.arm_periodic(std::chrono::seconds(1));

    conn = lsquic_engine_connect(engine, N_LSQVER, &channel.local_address().as_posix_sockaddr(),
                                 &server_address.as_posix_sockaddr(), this, nullptr, nullptr, 0,
                                 nullptr, 0, nullptr, 0);

    if (!conn) {
        fail("Creating a connection has failed");
    }

    (void) process_conns();

    return read_udp();
}


static seastar::future<> submit_to_cores(std::string &host, std::uint16_t port) {
    return seastar::parallel_for_each(boost::irange<unsigned>(0, seastar::smp::count),
            [&host, &port] (unsigned core) {
        return seastar::smp::submit_to(core, [&host, &port] () {
            Client _client(host, port + seastar::this_shard_id());
            return seastar::do_with(std::move(_client), [] (Client &client) {
                client.init_lsquic();
                return client.service_loop();
            });
        });
    });
}


int main(int argc, char **argv) {
    seastar::app_template app;

    namespace po = boost::program_options;
    app.add_options()
            ("host", po::value<std::string>()->required(), "server address")
            ("port", po::value<std::uint16_t>()->required(), "listen port");

    try {
        app.run(argc, argv, [&] () {
            auto&& config = app.configuration();
            std::string server_address = config["host"].as<std::string>();
            std::uint16_t port = config["port"].as<std::uint16_t>();
            return seastar::do_with(std::move(server_address), port,
                                    [] (std::string &addr, std::uint16_t &port) {
               return submit_to_cores(addr, port);
            });
        });
    } catch(...) {
        std::cerr << "Couldn't start application: " << std::current_exception() << '\n';
        return 1;
    }
    return 0;
}
