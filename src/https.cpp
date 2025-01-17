#include <iostream>

#include <protocols/h2.hpp>
#include <protocols/h2/connection.hpp>
#include <protocols/https.hpp>

#include <netdb.h>
#include <netinet/in.h>

connection<void> *
rite::server<https>::on_accept(connection<void>::native_handle socket, struct sockaddr_storage addr, socklen_t len) {
    std::cout << "HTTPS: Got new client" << std::endl;
    try {
        ::connection<tls> *connection = new ::connection<tls>(ctx_, socket, addr, len);
        const uint8_t     *alpn;
        uint32_t           alpn_len;
        SSL_get0_alpn_selected(connection->ssl(), &alpn, &alpn_len);

        // If alpn[0..2] == "h2", we move the TLS connection into
        // connection<h2>
        if (alpn_len >= 2 && memcmp(alpn, "h2", 2) == 0) {
            // Upgrade the connection to HTTP2
            auto *http2 = new ::connection<h2::protocol>(std::move(*connection));
            delete connection;
            return http2;
        } else {
            // Return a plain HTTP connection
        }
        return connection;
    } catch (std::exception &e) {
        std::cerr << "TLS handshake failed: " << e.what() << std::endl;
        return connection<void>::invalid;
    }
}

void
rite::server<https>::on_read(connection<void> *socket) {
    static thread_local std::unique_ptr<std::byte[]> buffer = std::make_unique<std::byte[]>(16384);
    auto                                             lock = socket->lock();
    ssize_t                                          bytes = socket->read(std::span<std::byte>(buffer.get(), 16384), 0);
    if (bytes < 1) {
        socket->release();
        socket->close();
        return;
    }

    connection<h2::protocol> *h2_sock = dynamic_cast<connection<h2::protocol> *>(socket);
    if (h2_sock) {
        try {
            uint32_t                    stream_id = 0;
            std::optional<http_request> request = h2_sock->process(std::span<std::byte>(buffer.get(), bytes));
            if (request) {
                stream_id = request->context<h2::stream_id>().value();

                // Take up a new reference for the handler
                socket->take();

                config_.behaviour_->handle(*request, [this, &request, h2_sock, stream_id](http_response &&response) {
                    std::vector<h2::hpack::header> headers_;
                    headers_.push_back(h2::hpack::header{ ":status", std::to_string(static_cast<int>(response.status_code())) });
                    for (auto const &[k, v] : response.headers()) {
                        headers_.push_back(h2::hpack::header{ k, v });
                    }
                    h2_sock->parameters_->hpack.tx.serialize(headers_);
                    auto preload = h2_sock->parameters_->hpack.tx.finish(stream_id);
                    h2_sock->write(preload);

                    rite::buffer                            buf;
                    std::shared_ptr<jt::mpsc<rite::buffer>> channel = response.channel;
                    jt::mpsc<rite::buffer>::consumer       &rx = channel->rx();
                    do {
                        response.trigger(http_response::event::chunk);
                        buf = rx.wait();
                        h2::frame frame;
                        frame.stream_identifier = stream_id;
                        frame.type = h2::frame::DATA;
                        frame.flags = buf.last ? h2::frame::characteristics<h2::frame::DATA>::END_STREAM : 0;
                        frame.length = buf.len;
                        frame.data = std::vector<std::byte>(buf.data.get(), buf.data.get() + buf.len);
                        h2_sock->write(frame);
                    } while (!buf.last);

                    // Release reference to allow the connection to drop
                    h2_sock->release();
                });
            }
        } catch (std::exception &e) {
            std::print("H2[process]: Failed: {}\n", e.what());
            h2_sock->terminate();
        } catch (rite::http::layer::error err) {
            // Probably eNoEndpoint (404)
        }
    }
    socket->release();
}

rite::server<https>::server(const config &server_config)
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
