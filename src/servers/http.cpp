#include <http/parser.hpp>
#include <http/serializer.hpp>
#include <protocols/http.hpp>
#include <sys/socket.h>

connection<void> *
rite::server<http>::on_accept(connection<void>::native_handle socket, struct sockaddr_storage addr, socklen_t len) {
    return new connection<plain>(socket, addr, len);
}

void
rite::server<http>::on_read(connection<void> *socket) {
    thread_local std::unique_ptr<std::byte[]> buffer = std::make_unique<std::byte[]>(16384);

    ssize_t bytes = socket->read(std::span<std::byte>(buffer.get(), 16384), 0);
    if (bytes < 1) {
        std::print("Connection died.\n");
        socket->release();
        socket->close();
        return;
    }

    http_request req;
    // TODO:
    // This fails for any request whose total size is bigger than the MTU.
    std::span<const std::byte> raw_data(buffer.get(), bytes);
    bool                       success = parser<http_request>{}.parse(socket, raw_data, req);

    if (success) {
        socket->take();
        config_.behaviour_->handle(req, [socket](http_response &&response) {
            auto ss = serializer<http_response>{ .serialize_body = false };
            {
                auto headers = ss(response);
                socket->write(headers, MSG_MORE);
            }

            {
                // Write body
                auto        &body = response.channel->rx();
                rite::buffer slice;
                do {
                    response.trigger(http_response::event::chunk);
                    slice = body.wait();
                    auto span = std::span<const std::byte>(slice.data.get(), slice.len);
                    socket->write(span, slice.last == false ? MSG_MORE : 0);
                } while (slice.last == false);
            }
            response.trigger(http_response::event::finish);
            socket->release();
        });
    } else {
        // TODO: We have to handle this.
        std::print("Invalid request\n");
    }

    socket->release();
}
