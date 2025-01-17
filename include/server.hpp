#pragma once

#include "connection.hpp"
#include "runtime.hpp"
#include <asm-generic/socket.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rite {

template<typename T>
class server {
    protected:
    static constexpr int PROTOCOL = IPPROTO_TCP;
    static constexpr int DOMAIN = AF_INET;
    static constexpr int SOCKET_TYPE = SOCK_STREAM | SOCK_NONBLOCK;

    rite::runtime                  *runtime = nullptr;
    std::vector<connection<void> *> connections_;
    struct {
        int server;
        int epoll;
    } fd;
    friend class runtime;

    public:
    struct config {
        private:
        ssize_t  max_connections_ = -1; // -1 defaults to SOMAX
        uint16_t port_;
        uint64_t ip_;
        friend class server<T>;

        public:
        config() {};
        config &port(uint16_t port) {
            port_ = port;
            return *this;
        }

        config &ip(uint32_t ip) {
            ip_ = ip;
            return *this;
        }

        config &max_connections(ssize_t max) {
            max_connections_ = max;
            return *this;
        }
    };

    private:
    config base_config_;

    public:
    server(config conf)
      : fd(decltype(fd){ 0, 0 })
      , base_config_(conf) {}

    virtual connection<void> *on_accept(connection<void>::native_handle socket, struct sockaddr_storage, socklen_t) = 0;
    virtual void              on_read(connection<void> *) = 0;

    [[noreturn]]
    virtual void operator()();

    virtual void connection_sentinel(size_t, server *);
};

};

template<typename T>
void
rite::server<T>::operator()() {
    fd.server = socket(rite::server<T>::DOMAIN, rite::server<T>::SOCKET_TYPE, rite::server<T>::PROTOCOL);
    if (fd.server < 0) {
        throw std::runtime_error("Failed to create socket");
    }

    int enable = 1;
    // Some common sock opts
    setsockopt(fd.server, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    setsockopt(fd.server, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int));
    if (rite::server<T>::PROTOCOL == IPPROTO_TCP) {
        setsockopt(fd.server, SOL_SOCKET, TCP_NODELAY, &enable, sizeof(int));
    }

    // Construct sockaddr
    struct sockaddr_in address {
        .sin_family = AF_INET, .sin_port = ntohs(base_config_.port_), .sin_addr = { .s_addr = ntohl(base_config_.ip_) }, .sin_zero = { 0 }
    };

    // Bind & listen to the socket
    int result = bind(fd.server, (struct sockaddr *)&address, sizeof(address));
    if (result != 0) {
        throw std::runtime_error("Failed to bind socket");
    }
    result = listen(fd.server, base_config_.max_connections_);

    // Refer to docs/connections.org
    connections_.resize(base_config_.max_connections_, (connection<void> *)((uintptr_t)1 << 63));

    // Create epoll socket
    fd.epoll = epoll_create1(0);
    if (fd.epoll < 1) {
        throw std::runtime_error("Failed create epoll socket");
    }
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = fd.server;
    if (epoll_ctl(fd.epoll, EPOLL_CTL_ADD, fd.server, &event) != 0) {
        perror("Failed to add epoll sock");
        throw std::runtime_error("Failed to add server socket to epoll set");
    }

    std::unique_ptr<struct epoll_event[]> events = std::make_unique_for_overwrite<struct epoll_event[]>(base_config_.max_connections_);
    struct sockaddr_storage               client_address;
    socklen_t                             client_address_len = sizeof(client_address);
    for (;;) {
        size_t ready = epoll_wait(fd.epoll, events.get(), base_config_.max_connections_, -1);
        for (size_t i = 0; i < ready; ++i) {
            struct epoll_event &event = events[i];
            if (event.data.fd == fd.server) { // Server socket
                int client_socket = accept(fd.server, (struct sockaddr *)&client_address, &client_address_len);
                if (client_socket < 1) {
                    perror("Failed to accept client");
                    continue;
                }

                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLET;
                {
                    connection<void> *con = on_accept(client_socket, client_address, client_address_len);
                    auto              next_it = std::find_if(connections_.begin(), connections_.end(), [](auto ptr) {
                        // Find inactive connection
                        return (reinterpret_cast<uintptr_t>(ptr) & ((uintptr_t)1 << 63)) != 0;
                    });
                    if (next_it == connections_.end()) {
                        delete con;
                        continue;
                    }
                    auto next_idx = std::distance(connections_.begin(), next_it);
                    connections_[next_idx] = con;

                    ev.data.u64 = next_idx;
                    std::thread(std::bind(&server::connection_sentinel, this, std::placeholders::_1, std::placeholders::_2), next_idx, this).detach();
                }
                // Add client socket epoll set
                if (epoll_ctl(fd.epoll, EPOLL_CTL_ADD, client_socket, &ev) == -1) {
                    perror("Failed to add epoll socket");
                }
            } else { // Client event
                if (event.events & EPOLLIN) {
                    connection<void> *client = reinterpret_cast<connection<void> *>(connections_[event.data.u64]);
                    if (((uintptr_t)client & ((uintptr_t)1 << 63)) != 0) {
                        // Event was dispatched for client that has already been deallocated.
                        std::cout << "Skipping dead client" << std::endl;
                        continue;
                    }
                    // TODO: Check if connection is locked,
                    // if it is, remove the EPOLLET behaviour
                    // and retry adding the task to the thread pool.

                    client = reinterpret_cast<connection<void> *>(((uintptr_t)client) & ((~0ULL) >> 16));
                    client->take();
                    client->was_active();
                    runtime->dispatch(std::bind(&server::on_read, this, client));
                }
            }
        }
    }
}

template<typename T>
void
rite::server<T>::connection_sentinel(size_t connection_idx, rite::server<T> *server) {
    connection<void> *con = reinterpret_cast<connection<void> *>(server->connections_[connection_idx]);
    // Remove application specific information from the pointer
    uintptr_t mask = (~(0ULL) >> 16);
    con = reinterpret_cast<connection<void> *>(((uintptr_t)con) & mask);

    for (;;) {
        std::unique_lock<std::mutex> lk(con->mutex());
        auto                         last_active = con->last_active();
        auto                         keep_alive = con->get_keep_alive();
        auto                         next_wakeup = last_active + keep_alive;

        con->cv().wait_until(lk, next_wakeup, [&con]() { return (con->idle() && con->use_count() <= 0) || con->is_closed(); });
        if ((con->idle() && con->use_count() <= 0) || con->is_closed())
            break;
    }
    connections_[connection_idx] = (connection<void> *)((uintptr_t)con | (((uintptr_t)1) << 63));
    delete con;
}
