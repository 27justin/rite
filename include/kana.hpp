#pragma once
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <map>
#include <memory>
#include <netinet/in.h>
#include <regex>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "connection.hpp"
#include "controller.hpp"
#include "extension.hpp"
#include "http/method.hpp"
#include "http/request.hpp"
#include "http/response.hpp"
#include "middleware.hpp"
#include "protocol.hpp"
#include <atomic>

namespace kana {

class server {
    public:
    enum class event {
        on_request, // On request, before any path-mapping takes place
        pre_send,   // After handler & middlewares ran, but before headers are flushed
        post_send,  // After flushing headers & body
    };
    //clang-format off
    using event_callback_type = std::variant<std::function<void(http_request &)>, std::function<void(http_request &, http_response &)>>;
    //clang-format on
    private:
    std::map<std::string, std::unique_ptr<kana::middleware>> middlewares_;
    std::list<std::unique_ptr<kana::controller>>             controllers_;
    std::list<std::unique_ptr<kana::extension>>              extensions_;

    std::vector<std::atomic<uintptr_t>> connections_;
    mutable std::mutex                            connection_mtx_;

    struct {
        std::vector<std::tuple<in_addr_t, uint16_t, kana::protocol>> bind;
        size_t                                                       worker_threads = 8;
        size_t                                                       max_connections = 24;
        size_t                                                       worker_buffer_size = 32768;
    } server_config;

    // Usage:
    // Each connection gets allocated on the heap and then the pointer to it gets inserted here
    std::vector<connection **> connection_map_;

    std::map<event, std::list<event_callback_type>> events_;
    // std::list<std::unique_ptr<kana::filter>>             filters_;

    // TODO: Thread unsafe, should a new event callback be inserted on
    // the same type that is currently being called. We may have to
    // introduce a read-only lock here.

    // Trigger an event with http_request
    void trigger(event evt, http_request &req) {
        auto &list = events_[evt];
        for (auto const &callback : list) {
            auto lambda = std::get<std::function<void(http_request &)>>(callback);
            lambda(req);
        }
    }

    // Trigger an event with http_request and http_response
    void trigger(event evt, http_request &req, http_response &response) {
        auto &list = events_[evt];
        for (auto const &callback : list) {
            auto lambda = std::get<std::function<void(http_request &, http_response &)>>(callback);
            lambda(req, response);
        }
    }

    void finish_http_request(http_request &request, http_response &response);

    // void connection_sentinel(std::shared_ptr<connection> con);
    void connection_sentinel(size_t);

    public:
    server(){}

    void event(enum event event, event_callback_type &&callback) { events_[event].push_front(callback); }
    template<class T, typename... Args>
        requires std::derived_from<T, kana::middleware>
    server &register_middleware(Args... args) {
        middlewares_[T::name] = std::make_unique<T>(std::forward<Args>(args)...);
        return *this;
    }

    template<class T, typename... Args>
        requires std::derived_from<T, kana::controller>
    server &register_controller(Args... args) {
        std::unique_ptr<T> controller = std::make_unique<T>(std::forward<Args>(args)...);
        controller->setup(controller->config);
        controllers_.emplace_back(std::move(controller));
        return *this;
    }

    kana::middleware *middleware(const std::string &name) {
        if (middlewares_.contains(name))
            return middlewares_[name].get();
        return nullptr;
    }

    kana::endpoint *find_endpoint(const http_request &request) {
        struct path_check {
            std::string_view path;
            path_check(std::string_view p)
              : path(p) {}
            bool operator()(const std::string &endpoint_path) { return endpoint_path == path; }
            bool operator()(const std::regex &regex) { return std::regex_search(std::string(path), regex); }
        };
        for (auto &controller : controllers_) {
            for (endpoint &e : controller->config.endpoints_) {
                if ((e.method & static_cast<std::underlying_type<http_method>::type>(request.method())) != 0 && std::visit(path_check(request.path()), e.path)) {
                    return &e;
                }
            }
        }
        return nullptr;
    }

    template<typename T, typename... Args>
        requires std::derived_from<T, kana::extension>
    server &load_extension(Args... args) {
        extensions_.emplace_back(std::make_unique<T>(std::forward<Args>(args)...));
        extensions_.back()->on_load(*this);
        return *this;
    }

    server &worker_threads(ssize_t num) {
        server_config.worker_threads = num;
        server_config.max_connections = num * 2.5;
        return *this;
    }

    server &bind(typename decltype(server_config.bind)::value_type address) {
        server_config.bind.emplace_back(address);
        return *this;
    }

    [[noreturn]]
    void start();
};
}
