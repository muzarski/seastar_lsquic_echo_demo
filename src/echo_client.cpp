#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/sleep.hh>

#include <lsquic/lsquic.h>

#include <logger.h>
#include <client.h>

#include <iostream> // For lsquic's logging

using namespace seastar::net;
using namespace zpp;

namespace {

int tut_log_buf(void *ctx, const char *buf, std::size_t len) {
    std::FILE *out = static_cast<std::FILE*>(ctx);
    std::fwrite(buf, 1, len, out);
    std::fflush(out);
    return 0;
}

constinit lsquic_logger_if logger_if = { tut_log_buf };

void init_lsquic() {
    flog("Initializing lsquic.");
    if (lsquic_global_init(LSQUIC_GLOBAL_CLIENT) != 0) {
        ffail("Initialization of lsquic has failed.");
    }

    lsquic_set_log_level("debug");
    lsquic_logger_init(&logger_if, stderr, LLTS_HHMMSSUS);
}

seastar::future<> submit_to_cores(const std::string &host, std::uint16_t port) {
    return seastar::parallel_for_each(boost::irange<unsigned>(0, seastar::smp::count),
        [&host, &port](unsigned int core) {
            return seastar::smp::submit_to(core, [&host, &port] () {
                client client_instance(host, port);
                return seastar::do_with(std::move(client_instance), [](client &client_instance) {
                    init_lsquic();
                    client_instance.init();
                    return client_instance.service_loop();
                });
            });
        }
    );
}

} // anonymous namespace

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
                [](const std::string &addr, std::uint16_t &port) {
                    return submit_to_cores(addr, port);
                }
            );
        });
    } catch(...) {
        ffail("Couldn't start the application: ", std::current_exception());
    }
    return 0;
}
