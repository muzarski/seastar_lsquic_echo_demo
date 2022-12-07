#ifndef __ZPP_SSL_HANDLER__
#define __ZPP_SSL_HANDLER__

#include <openssl/ssl.h>

#include <logger.h>

namespace zpp {

int tut_load_cert(const char *cert_file, const char *key_file, SSL_CTX **s_ssl_ctx);

}

#endif // __ZPP_SSL_HANDLER__
