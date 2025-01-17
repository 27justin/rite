#pragma once

#include "connection.hpp"
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdexcept>
#include <utility>

struct tls {};

template<>
class connection<tls> : public connection<void> {
    private:
    SSL *ssl_;

    public:
    connection(SSL_CTX *ctx, sockfd socket, struct sockaddr_storage address, size_t addr_len)
      : connection<void>(socket, address, addr_len) {
        ssl_ = SSL_new(ctx);
        SSL_set_fd(ssl_, socket_);
        if (SSL_accept(ssl_) <= 0) {
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("ssl accept failed");
        }
    }

    connection(connection<tls> &&other)
      : connection<void>(std::move(other))
      , ssl_(std::exchange(other.ssl_, nullptr)) {
        other.socket_ = -1;
    }

    SSL *ssl() { return ssl_; }

    ssize_t read(std::span<std::byte> where, int) { return SSL_read(ssl_, where.data(), where.size_bytes()); }

    ssize_t write(std::span<const std::byte> what, int) { return SSL_write(ssl_, what.data(), what.size_bytes()); }
};
