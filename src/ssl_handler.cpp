#include <ssl_handler.h>

#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/ssl.h>

static unsigned char server_alpns[] = {
        "\x0ahq-interop\x05h3-29\x05hq-28\x05hq-27\x08http/0.9\04echo"
};

static int select_alpn (SSL *ssl, const unsigned char **out, unsigned char *outlen,
             const unsigned char *in, unsigned int inlen, void *arg)
{
    std::cerr << "IN SELECT ALPN" << std::endl;
    int r;

    r = SSL_select_next_proto((unsigned char **) out, outlen, in, inlen,
                              (unsigned char *) server_alpns, sizeof(server_alpns) - 1);
    if (r == OPENSSL_NPN_NEGOTIATED)
        return SSL_TLSEXT_ERR_OK;
    else {
        fprintf(stderr, "no supported protocol can be selected from %.*s\n",
                 (int) inlen, (char *) in);
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
}

int zpp::tut_load_cert(const char *cert_file, const char *key_file, SSL_CTX **s_ssl_ctx) {
    int rv = -1;
    *s_ssl_ctx = SSL_CTX_new(TLS_method());

    auto end_routine = [&]() {
        if (rv != 0) {
            SSL_CTX_free(*s_ssl_ctx);
        }
        s_ssl_ctx = nullptr;
        return rv;
    };

    if (!s_ssl_ctx) {
        log("SSL_CTX_new has failed");
        return end_routine();
    }
    
    SSL_CTX_set_min_proto_version(*s_ssl_ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(*s_ssl_ctx, TLS1_3_VERSION);
    SSL_CTX_set_default_verify_paths(*s_ssl_ctx);
    SSL_CTX_set_alpn_select_cb(*s_ssl_ctx, select_alpn, NULL);

    if (1 != SSL_CTX_use_certificate_chain_file(*s_ssl_ctx, cert_file)) {
        log("SSL_CTX_use_certificate_chain_file has failed");
        return end_routine();
    }
    if (1 != SSL_CTX_use_PrivateKey_file(*s_ssl_ctx, key_file, SSL_FILETYPE_PEM)) {
        log("SSL_CTX_use_PrivateKey_file has failed");
        return end_routine();
    }
    
    return 0;
}
