#include <iostream>
#include <cassert>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <protocols/h2.hpp>
#include <protocols/h2/connection.hpp>
#include <protocols/https.hpp>

#include <netdb.h>
#include <netinet/in.h>
#include <string>

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
        return nullptr;
    }
}

void
rite::server<https>::on_read(connection<void> *socket) {
    static thread_local std::unique_ptr<std::byte[]> buffer = std::make_unique<std::byte[]>(65535);
    while(true) {
        ssize_t bytes = 0;
        {
            auto lock_ = socket->lock();
            bytes = socket->read(std::span<std::byte>(buffer.get(), 16384), 0);
            if (bytes < 1) {
                socket->release();
                return;
            }
        }

        connection<h2::protocol> *h2_sock = dynamic_cast<connection<h2::protocol> *>(socket);
        if (h2_sock) {
            try {
                uint32_t                         stream_id = 0;
                std::span<const std::byte>       packet(buffer.get(), bytes);
                auto                             pos = packet.begin(), end = packet.end();

                connection<h2::protocol>::result result;
                while ((result = h2_sock->process(pos, end)) != connection<h2::protocol>::result::eEof) {
                    // OK, we reached the end of the frame
                    if (result == connection<h2::protocol>::result::eNewRequest) {
                        http_request request = std::move(h2_sock->request);

                        stream_id = request.context<h2::stream_id>().value();

                        // Take up a new reference for the handler
                        socket->take();
                        config_.behaviour_->handle(request, [this, &request, h2_sock, stream_id](http_response &&response) {
                            std::vector<h2::hpack::header> headers_;
                            headers_.push_back(h2::hpack::header{ ":status", std::to_string(static_cast<int>(response.status_code())) });
                            for (auto const &[k, v] : response.headers()) {
                                headers_.push_back(h2::hpack::header{ k, v });
                            }
                            h2_sock->parameters_->hpack.tx.serialize(headers_);
                            auto preload = h2_sock->parameters_->hpack.tx.finish(stream_id);

                            {
                                auto lock_ = h2_sock->lock();
                                h2_sock->write(preload);
                            }
                            rite::buffer                            buf;
                            std::shared_ptr<jt::mpsc<rite::buffer>> channel = response.channel;
                            jt::mpsc<rite::buffer>::consumer       &rx = channel->rx();
                            do {
                                response.trigger(http_response::event::chunk);
                                buf = rx.wait();

                                // Further slice up the user's chunks
                                // to satisfy the HTTP/2 streams max size.
                                // TODO: Get actual max size from HTTP/2 stream
                                ssize_t total_length = buf.len;
                                ssize_t offset = 0;

                                while (offset < total_length) {
                                    // Calculate the size of the current slice
                                    ssize_t slice_size = std::min(static_cast<ssize_t>(16384), total_length - offset);

                                    // Create a frame for the current slice
                                    h2::frame frame;
                                    frame.stream_identifier = stream_id;
                                    frame.type = h2::frame::DATA;
                                    // Set END_STREAM when we're on the last buffer and reached the last slice.
                                    frame.flags = (offset + slice_size >= total_length && buf.last) ? h2::frame::characteristics<h2::frame::DATA>::END_STREAM : 0;
                                    frame.length = slice_size;

                                    // Copy the slice data into the frame
                                    // TODO: Urgh. I'm not a fan of copying
                                    // here. We could to some pointer
                                    // gymnastics to be copy-free.
                                    frame.data = std::vector<std::byte>(buf.data.get() + offset, buf.data.get() + offset + slice_size);

                                    // Write the frame to the socket
                                    auto lock_ = h2_sock->lock();
                                    auto result = h2_sock->write(frame);
                                    if (result < 0) {
                                        // TODO: Handle properly.
                                        SSL_get_error(h2_sock->ssl(), result);
                                        throw std::runtime_error("failed to write data to sock");
                                    }
                                    // Update the offset for the next slice
                                    offset += slice_size;
                                }
                            } while (!buf.last);
                            response.trigger(http_response::event::finish);

                            // Release reference to allow the connection to drop
                            h2_sock->release();
                            h2_sock->streams_[stream_id].state = h2::stream::closed;
                        });
                    }
                }
                assert(pos == end);
            } catch (std::exception &e) {
                std::print("H2[process]: Failed: {}\n", e.what());
                h2_sock->terminate();
            } catch (rite::http::layer::error err) {
                // Probably eNoEndpoint (404)
            }
        } else {
            std::print("HTTPS socket that is not HTTP/2. Not supported\n");
            std::exit(1);
        }
        socket->release();
    }
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
