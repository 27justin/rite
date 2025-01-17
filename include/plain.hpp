#pragma once

#include "connection.hpp"

struct plain {};

template<>
class connection<plain> : public connection<void> {
    public:
    connection(sockfd socket, struct sockaddr_storage address, size_t addr_len)
      : connection<void>(socket, address, addr_len) {};

    ssize_t read(std::span<std::byte> where, int flags) override { return recv(this->socket_, where.data(), where.size_bytes(), flags); }

    ssize_t write(std::span<const std::byte> what, int flags) override { return send(this->socket_, what.data(), what.size_bytes(), flags); }
};
