#include <iostream>
#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/sleep.hh>

#include <logger.h>

#include <lsquic/lsquic.h>

using namespace seastar::net;
using namespace zpp;

static lsquic_conn_ctx_t *
tut_client_on_new_conn (void *stream_if_ctx, struct lsquic_conn *conn)
{
    struct tut *const tut = stream_if_ctx;
    tut->tut_u.c.conn = conn;
    LOG("created connection");
    return (void *) tut;
}


static void
tut_client_on_hsk_done (lsquic_conn_t *conn, enum lsquic_hsk_status status)
{
    struct tut *const tut = (void *) lsquic_conn_get_ctx(conn);

    switch (status)
    {
        case LSQ_HSK_OK:
        case LSQ_HSK_RESUMED_OK:
            LOG("handshake successful, start stdin watcher");
            ev_io_start(tut->tut_loop, &tut->tut_u.c.stdin_w);
            break;
        default:
            LOG("handshake failed");
            break;
    }
}


static void
tut_client_on_conn_closed (struct lsquic_conn *conn)
{
    struct tut *const tut = (void *) lsquic_conn_get_ctx(conn);

    LOG("client connection closed -- stop reading from socket");
    ev_io_stop(tut->tut_loop, &tut->tut_sock_w);
}


static lsquic_stream_ctx_t *
tut_client_on_new_stream (void *stream_if_ctx, struct lsquic_stream *stream)
{
    struct tut *tut = stream_if_ctx;
    LOG("created new stream, we want to write");
    lsquic_stream_wantwrite(stream, 1);
    /* return tut: we don't have any stream-specific context */
    return (void *) tut;
}


/* Echo whatever comes back from server, no verification */
static void
tut_client_on_read (struct lsquic_stream *stream, lsquic_stream_ctx_t *h)
{
    struct tut *tut = (struct tut *) h;
    ssize_t nread;
    unsigned char buf[3];

    nread = lsquic_stream_read(stream, buf, sizeof(buf));
    if (nread > 0) {
        fwrite(buf, 1, nread, stdout);
        fflush(stdout);
    }
    else if (nread == 0) {
        log("read to end-of-stream: close and read from stdin again");
        lsquic_stream_shutdown(stream, 0);
        ev_io_start(tut->tut_loop, &tut->tut_u.c.stdin_w);
    }
    else {
        std::cerr << "error reading from stream -- exit loop\n";
    }
}


/* Write out the whole line to stream, shutdown write end, and switch
 * to reading the response.
 */
static void
tut_client_on_write (struct lsquic_stream *stream, lsquic_stream_ctx_t *h)
{
    lsquic_conn_t *conn;
    struct tut *tut;
    ssize_t nw;

    conn = lsquic_stream_conn(stream);
    tut = (void *) lsquic_conn_get_ctx(conn);

    nw = lsquic_stream_write(stream, tut->tut_u.c.buf, tut->tut_u.c.sz);
    if (nw > 0)
    {
        tut->tut_u.c.sz -= (size_t) nw;
        if (tut->tut_u.c.sz == 0)
        {
            LOG("wrote all %zd bytes to stream, switch to reading",
                (size_t) nw);
            lsquic_stream_shutdown(stream, 1);  /* This flushes as well */
            lsquic_stream_wantread(stream, 1);
        }
        else
        {
            memmove(tut->tut_u.c.buf, tut->tut_u.c.buf + nw, tut->tut_u.c.sz);
            LOG("wrote %zd bytes to stream, still have %zd bytes to write",
                (size_t) nw, tut->tut_u.c.sz);
        }
    }
    else {
        std::cerr << "stream_write() returned " << nw << ", abort connection.\n";
        lsquic_conn_abort(lsquic_stream_conn(stream));
    }
}


static void
tut_client_on_close (struct lsquic_stream *stream, lsquic_stream_ctx_t *h) {
    std::cerr << "stream closed\n";
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

class Client {
private:
    udp_channel channel;
    seastar::ipv4_addr server_address;
    bool is_timer_active;
    seastar::future<> udp_send_queue;
    lsquic_engine_t *engine{};

    seastar::future<> read_stdin_and_send() {
        std::string s;
        std::cin >> s;
        return seastar::do_with(std::move(s), [this] (std::string &to_send) {
            return channel.send(server_address, to_send.c_str());
        });
    }

    seastar::future<> timer_expired() {
        std::cerr << "Timer expired\n";
        is_timer_active = false;
        return process_conns();
    }

    seastar::future<> process_conns() {
        int diff;
        int64_t timeout;

        lsquic_engine_process_conns(engine);

        if (lsquic_engine_earliest_adv_tick(engine, &diff)) {
            std::cerr << "There are tickable connections.\n";
            if (diff >= LSQUIC_DF_CLOCK_GRANULARITY) {
                timeout = diff / 1000;
            }
            else if (diff <= 0)
                timeout = 0;
            else {
                timeout = LSQUIC_DF_CLOCK_GRANULARITY / 1000;
            }

            std::cerr << "timeout received: " << timeout << std::endl;
            if (!is_timer_active) {
                std::cerr << "Setting the timer for " << timeout << " millis\n";
                (void) seastar::sleep(std::chrono::milliseconds(timeout)).then([this] {
                    return timer_expired();
                });
                is_timer_active = true;
            }
            else {
                std::cerr << "But timer is already active. Skipping\n";
            }
        }
        else {
            std::cerr << "There are NO tickable connections.\n";
        }
        return seastar::make_ready_future<>();
    }

    seastar::future<> handle_receive(udp_datagram &&datagram) {
        std::cerr << "Read " << datagram.get_data().len() << " bytes  from udp channel\n";

        char buf[DATAGRAM_SIZE];
        memcpy(buf, datagram.get_data().fragment_array()->base, datagram.get_data().len());
        buf[datagram.get_data().len()] = '\0';

        int packet_in_res = lsquic_engine_packet_in(engine, reinterpret_cast<const unsigned char *>(buf),
                                                    datagram.get_data().len(), &datagram.get_dst().as_posix_sockaddr(),
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

    seastar::future<> receive() {
        return channel.receive().then([] (udp_datagram datagram) {
            char buf[DATAGRAM_SIZE];
            memcpy(buf, datagram.get_data().fragment_array()->base, datagram.get_data().len());
            buf[datagram.get_data().len()] = '\0';
            std::cout << buf << "\n";
        });
    }

    static int tut_packets_out(void *packets_out_ctx, const lsquic_out_spec *specs, unsigned int count) {
        std::cerr << "in packets out... - " << count << " packages to send\n";
        if (count == 0) {
            return 0;
        }

        for (size_t i = 0; i < count; ++i) {
            auto *client = reinterpret_cast<Client*>(specs[i].peer_ctx);
            std::unique_ptr<char> data(new char[specs[i].iov->iov_len]);
            std::memcpy(data.get(), specs[i].iov->iov_base, specs[i].iov->iov_len);
            client->udp_send_queue = client->udp_send_queue.then(
                    [client, data = std::move(data), dst = *specs[i].dest_sa, data_len = specs[i].iov->iov_len] () {
                        std::cerr << "sending " << data_len << " bytes of data.\n";
                        seastar::socket_address addr(*(sockaddr_in *) &dst);
                        return client->channel.send(addr, seastar::temporary_buffer<char>(data.get(), data_len));
                    });
        }

        return count;
    }

public:
    Client(const std::string &host, std::uint16_t port) :
        channel(seastar::make_udp_channel()),
        server_address(seastar::ipv4_addr(host, port)),
        is_timer_active(false),
        udp_send_queue(seastar::make_ready_future<>()) {
    };

    void init_lsquic() {
        std::cout << "Initializing lsquic engine\n";
        if (0 != ::lsquic_global_init(LSQUIC_GLOBAL_SERVER|LSQUIC_GLOBAL_CLIENT)) {
            fail("Initialization of the engine has failed");
        }

        ::lsquic_engine_settings settings{};
        ::lsquic_engine_init_settings(&settings, 0);

        char errbuf[0x100];

        settings.es_ql_bits = 0;
        if (0 != ::lsquic_engine_check_settings(&settings,
                                                0,
                                                errbuf, sizeof(errbuf))) {
            fail("Invalid settings");
        }

        lsquic_engine_api eapi{};
        std::memset(&eapi, 0, sizeof(eapi));
        eapi.ea_packets_out = tut_packets_out;
        eapi.ea_packets_out_ctx = this;
        eapi.ea_stream_if = &tut_client_callbacks;
        eapi.ea_stream_if_ctx = this;
        eapi.ea_settings = &settings;

        engine = ::lsquic_engine_new(0, &eapi);
        if (!engine) {
            fail("Creating an engine has failed");
        }
    }

    seastar::future<> service_loop() {
        return seastar::keep_doing([this] () {
            return read_stdin_and_send().then([this] () {
                return receive();
            });
        });
    }
};

static seastar::future<> submit_to_cores(std::string &host, std::uint16_t port) {
    return seastar::parallel_for_each(boost::irange<unsigned>(0, seastar::smp::count),
            [&host, &port] (unsigned core) {
        return seastar::smp::submit_to(core, [&host, &port] () {
            Client _client(host, port);
            return seastar::do_with(std::move(_client), [] (Client &client) {
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
