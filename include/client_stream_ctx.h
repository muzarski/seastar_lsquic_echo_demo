#ifndef __TUT_CLIENT_CTX_H__
#define __TUT_CLIENT_CTX_H__

#include <client.h>

#include <cstddef>  // std::size_t
#include <cstring>  // std::memset

namespace zpp {

struct client_stream_ctx {
public:
    using type = client_stream_ctx;

public:
    char         m_send_buffer[0x100];
    std::size_t  m_send_msg_size    = 0;
    std::size_t  m_send_offset      = 0;
    char         m_rcv_buffer[0x100];
    std::size_t  m_rcv_msg_size     = 0;
    std::size_t  m_rcv_offset       = 0;
    client      *m_client           = nullptr;

public:
    constexpr static std::size_t SEND_BUFFER_SIZE = sizeof(m_send_buffer);
    constexpr static std::size_t  RCV_BUFFER_SIZE = sizeof(m_rcv_buffer);

public:
    client_stream_ctx() {
        std::memset(&m_send_buffer, 0, type::SEND_BUFFER_SIZE);
        std::memset(&m_rcv_buffer, 0, type::RCV_BUFFER_SIZE);
    }
};

} // namespace zpp

#endif // __TUT_CLIENT_CTX_H__
