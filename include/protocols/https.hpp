#pragma once
#include <iostream>
#include <memory>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdexcept>

#include "server.hpp"
#include "tls.hpp"
#include <http/behaviour.hpp>

struct https {};

template<typename T>
struct protocol {};

#pragma GCC diagnostic ignored "-Wunused-function"
static int
alpn_select_cb(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen, void *arg) {
    // Check the incoming protocols
    const unsigned char *protocol = NULL;
    unsigned int         protocol_len = 0;

    // Iterate through the provided protocols
    while (inlen > 0) {
        // The first byte is the length of the protocol name
        unsigned char len = *in;
        if (len > inlen - 1) {
            // Invalid length, return an error
            return SSL_TLSEXT_ERR_NOACK;
        }

        // Check if the protocol is "h2"
        if (len == 2 && memcmp(in + 1, "h2", 2) == 0) {
            // Found the HTTP/2 protocol
            protocol = in + 1;  // Point to the protocol name
            protocol_len = len; // Set the length
            break;
        }

        // Move to the next protocol
        in += len + 1;    // Move past the length byte and the protocol name
        inlen -= len + 1; // Decrease the remaining length
    }

    if (protocol) {
        // Set the output parameters to indicate the selected protocol
        *out = protocol;          // Set the output to the selected protocol
        *outlen = protocol_len;   // Set the length of the selected protocol
        return SSL_TLSEXT_ERR_OK; // Indicate success
    }

    // If we reach here, we didn't negotiate HTTP/2
    return SSL_TLSEXT_ERR_NOACK; // Indicate that no protocol was selected
}

template<>
class rite::server<https> : public rite::server<void> {
    public:
    struct config : public rite::server<void>::config {
        std::string                        private_key_file_;
        std::string                        certificate_file_;
        std::shared_ptr<rite::http::layer> behaviour_;

        public:
        config &private_key_file(std::string file) {
            private_key_file_ = file;
            return *this;
        }

        config &certificate_file(std::string file) {
            certificate_file_ = file;
            return *this;
        }

        config &behaviour(std::shared_ptr<rite::http::layer> impl) {
            behaviour_ = impl;
            return *this;
        }

        friend class server<https>;
    };

    protected:
    config   config_;
    SSL_CTX *ctx_;

    public:
    server(const config &server_config);

    connection<void> *on_accept(connection<void>::native_handle socket, struct sockaddr_storage addr, socklen_t len) override;

    void on_read(connection<void> *socket) override;
};
