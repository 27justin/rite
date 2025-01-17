#pragma once
#include <iostream>
#include <memory>

#include "http/behaviour.hpp"
#include "plain.hpp"
#include "server.hpp"

struct http {
    struct client {};
};

template<>
class rite::server<http> : public rite::server<void> {
    public:
    struct config : public rite::server<void>::config {
        std::shared_ptr<rite::http::layer> behaviour_;

        public:
        config &behaviour(std::shared_ptr<rite::http::layer> impl) {
            behaviour_ = impl;
            return *this;
        }

        friend class rite::server<::http>;
    };

    protected:
    config config_;

    public:
    server(const config &server_config)
      : server<void>(server_config)
      , config_(server_config) {}

    connection<void> *on_accept(connection<void>::native_handle socket, struct sockaddr_storage addr, socklen_t len) override;

    void on_read(connection<void> *socket) override;
};
