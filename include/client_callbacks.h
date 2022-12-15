#ifndef __TUT_CLIENT_CALLBACKS_H__
#define __TUT_CLIENT_CALLBACKS_H__

#include <lsquic/lsquic.h>

namespace zpp {

lsquic_conn_ctx_t   *client_on_new_connection(void *stream_if_ctx, lsquic_conn_t *connection);
void                 client_on_connection_closed(lsquic_conn_t *connection);
lsquic_stream_ctx_t *client_on_new_stream(void *stream_if_ctx, lsquic_stream *stream);
void                 client_on_read(lsquic_stream_t *stream, lsquic_stream_ctx_t *stream_ctx);
void                 client_on_write(lsquic_stream_t *stream, lsquic_stream_ctx_t *stream_ctx);
void                 client_on_close(lsquic_stream_t *stream, lsquic_stream_ctx_t *stream_ctx);
void                 client_on_handshake_done(lsquic_conn_t *connection, lsquic_hsk_status handshake_status);
int                  client_packets_out(void *packets_out_ctx, const lsquic_out_spec *specs, unsigned int count);

} // namespace zpp

#endif // __TUT_CLIENT_CALLBACKS_H__
