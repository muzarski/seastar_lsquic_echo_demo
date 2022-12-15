#include <client_callbacks.h>

#include <lsquic/lsquic.h>

#include <client.h>
#include <client_stream_ctx.h>
#include <logger.h>

#include <utility>

namespace zpp {

lsquic_conn_ctx_t *client_on_new_connection([[maybe_unused]] void *stream_if_ctx, lsquic_conn_t *connection) {
    flog("Created a new connection");
    return nullptr;
}

void client_on_connection_closed([[maybe_unused]] lsquic_conn_t *connection) {
    flog("Closed a connection.");
}

lsquic_stream_ctx_t *client_on_new_stream(void *stream_if_ctx, lsquic_stream *stream) {
    try {
        client_stream_ctx *cs_ctx = new client_stream_ctx;

        cs_ctx->m_client = reinterpret_cast<client*>(stream_if_ctx);
        cs_ctx->m_client->m_read_input = true;
        cs_ctx->m_client->m_stream = stream;

        return reinterpret_cast<lsquic_stream_ctx_t*>(cs_ctx);
    } catch (const std::bad_alloc&) {
        eflog("Cannot allocate client stream context.");
        lsquic_conn_abort(lsquic_stream_conn(stream));
        return nullptr;
    }
}

void client_on_read(lsquic_stream_t *stream, lsquic_stream_ctx_t *stream_ctx) {
    client_stream_ctx *cs_ctx = reinterpret_cast<client_stream_ctx*>(stream_ctx);
    char buffer[1];

    auto read_count = lsquic_stream_read(stream, buffer, sizeof(buffer));

    if (read_count > 0) {
        cs_ctx->m_rcv_buffer[cs_ctx->m_rcv_msg_size++] = buffer[0];
        if (buffer[0] == '\n' || cs_ctx->m_rcv_msg_size == sizeof(cs_ctx->m_rcv_buffer)) {
            flog("Read a newline character of filled the buffer.");
            cs_ctx->m_rcv_buffer[cs_ctx->m_rcv_msg_size] = '\0';
            flog("\tMessage from the stream is: ", cs_ctx->m_rcv_buffer);

            // Reset the buffer
            cs_ctx->m_rcv_msg_size = 0;
            cs_ctx->m_rcv_offset   = 0;

            lsquic_stream_wantread(stream, 0);
            
            cs_ctx->m_client->m_read_input = true;
        }
    } else if (read_count == 0) {
        flog("Read an EOF.");
        lsquic_stream_shutdown(stream, 0);
        if (cs_ctx->m_rcv_msg_size) {
            lsquic_stream_wantwrite(stream, 1);
        }
    } else {
        eflog("Error while reading from a stream. Aborting the connection.");
        lsquic_conn_abort(lsquic_stream_conn(stream));
    }
}

// TODO: Make the function safer
void client_on_write(lsquic_stream_t *stream, lsquic_stream_ctx_t *stream_ctx) {
    client_stream_ctx *cs_ctx = reinterpret_cast<client_stream_ctx*>(stream_ctx);
    lsquic_stream_write(stream, cs_ctx->m_send_buffer, cs_ctx->m_send_msg_size);
    cs_ctx->m_send_msg_size = 0;
    cs_ctx->m_send_offset = 0;

    lsquic_stream_flush(stream);
    lsquic_stream_wantwrite(stream, 0);
    lsquic_stream_wantread(stream, 1);
}

void client_on_close([[maybe_unused]] lsquic_stream_t *stream, lsquic_stream_ctx_t *stream_ctx) {
    client_stream_ctx *cs_ctx = reinterpret_cast<client_stream_ctx*>(stream_ctx);
    delete cs_ctx;
    flog("A stream has been closed and memory freed.");
}

void client_on_handshake_done(lsquic_conn_t *connection, lsquic_hsk_status handshake_status) {
    switch (handshake_status) {
        case LSQ_HSK_OK:
        case LSQ_HSK_RESUMED_OK:
            flog("Handshake successful.");
            break;
        default:
            ffail("Handshake has failed. Exitting the program.");
    }

    lsquic_conn_make_stream(connection);
}

int client_packets_out(void *packets_out_ctx, const lsquic_out_spec *specs, unsigned int count) {
    flog("In packets_out callback... -- ", count, " packages to send.");
    if (count == 0) {
        return 0;
    }

    client *client_instance = reinterpret_cast<client*>(packets_out_ctx);

    for (std::size_t i = 0; i < count; ++i) {
        flog("Loop:)");
        std::unique_ptr<char> data(new char[specs[i].iov->iov_len]);
        std::memcpy(data.get(), specs[i].iov->iov_base, specs[i].iov->iov_len);
        client_instance->m_send_queue = client_instance->m_send_queue.then(
            [client_instance, data = std::move(data), dst = *specs[i].dest_sa, data_len = specs[i].iov->iov_len]() {
                flog("Sending ", data_len, " bytes of data via a UDP channel.");
                seastar::socket_address addr(*reinterpret_cast<const sockaddr_in*>(&dst));
                return client_instance->m_channel.send(addr, seastar::temporary_buffer<char>(data.get(), data_len));
            }
        );
    }

    return count;
}

} // namespace zpp
