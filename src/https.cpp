#include <iostream>

#include <protocols/https.hpp>
#include <protocols/h2.hpp>
#include <protocols/h2/connection.hpp>

connection<void> *
kana::server<https>::on_accept(connection<void>::native_handle socket, struct sockaddr_storage *addr, socklen_t len) {
    std::cout << "HTTPS: Got new client" << std::endl;
    try{
        ::connection<tls> *connection = new ::connection<tls>(ctx_, socket, *addr, len);
        const uint8_t *alpn;
        uint32_t alpn_len;
        SSL_get0_alpn_selected(connection->ssl(), &alpn, &alpn_len);

        // If alpn[0..2] == "h2", we move the TLS connection into
        // connection<h2>
        if (alpn_len >= 2 && memcmp(alpn, "h2", 2) == 0) {
            std::print("Got H2 connection!\n");
            // Upgrade the connection to HTTP2
            auto *http2 = new ::connection<h2::protocol>(std::move(*connection));
            delete connection;
            return http2;
        } else {
            // Return a plain HTTP connection
        }
        return connection;
    }catch(std::exception &e) {
        std::cerr << "TLS handshake failed: " << e.what() << std::endl;
        return connection<void>::invalid;
    }
}

void
kana::server<https>::on_read(connection<void> *socket) {
    static thread_local std::unique_ptr<std::byte[]> buffer = std::make_unique<std::byte[]>(16384);
    auto lock = socket->lock();

    std::print("on_read: "); std::cout << socket << "\n";

    ssize_t bytes = socket->read(std::span<std::byte>(buffer.get(), 16384), 0);
    if (bytes < 1) {
        std::print("Connection died.\n");
        socket->release();
        socket->close();
        return;
    }

    connection<h2::protocol> *h2_sock = dynamic_cast<connection<h2::protocol> *>(socket);
    if (h2_sock) {
        try {
            h2_sock->process(std::span<std::byte>(buffer.get(), bytes));
        } catch (std::exception &e) {
            std::print("H2[process]: Failed: {}\n", e.what());
            h2_sock->terminate();
        }
    }
    socket->release();
}

kana::server<https>::server(const config &server_config)
  : server<void>(server_config)
  , config_(server_config) {

    const SSL_METHOD *method = TLS_server_method();
    ctx_ = SSL_CTX_new(method);

    // Load private key & certificate
    if (SSL_CTX_use_certificate_file(ctx_, config_.certificate_file_.c_str(), SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx_);
        throw std::runtime_error("Failed to read TLS private key");
    }
    if (SSL_CTX_use_PrivateKey_file(ctx_, config_.private_key_file_.c_str(), SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx_);
        throw std::runtime_error("Failed to read TLS private key");
    }
    const unsigned char alpn[] = "\x02\x68\x32"; // H2 (HTTP/2) ALPN identifier
    SSL_CTX_set_alpn_protos(ctx_, alpn, sizeof(alpn) - 1);
    SSL_CTX_set_alpn_select_cb(ctx_, &alpn_select_cb, NULL);
}
