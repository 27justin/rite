#pragma once
#include <iostream>
#include <memory>

#include "plain.hpp"
#include "server.hpp"

struct http {};

template<>
class kana::server<http> : public kana::server<void> {
    public:
    server(const kana::server<void>::config &config)
      : server<void>(config) {}

    connection<void> *on_accept(connection<void>::native_handle socket, struct sockaddr_storage *addr, socklen_t len) override {
        std::cout << "HTTP: Got new client" << std::endl;
        return new connection<plain>(socket, *addr, len);
    }

    void on_read(connection<void> *socket) override {
        thread_local
            std::unique_ptr<std::byte[]> buffer = std::make_unique<std::byte[]>(16384);

        ssize_t bytes = socket->read(std::span<std::byte>(buffer.get(), 16384), 0);
        if (bytes < 1) {
            std::print("Connection died.\n");
            socket->release();
            socket->close();
            return;
        }
        std::print("Received {}b:\n{}\n", bytes, std::string_view((char*) buffer.get(), bytes));
        socket->release();
    }
};
