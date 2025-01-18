#pragma once

#include <concepts>
#include <functional>
#include <future>
#include <string>

#include "endpoint.hpp"
#include "request.hpp"
#include "response.hpp"
#include "status_code.hpp"

namespace rite::http {

class layer;
class extension {
    public:
    virtual void on_request(http_request &) = 0;
    virtual void pre_send(http_request &, http_response &) = 0;
    virtual void post_send(http_request &, http_response &) = 0;
    virtual void on_hook(layer &) = 0;
};

class layer {
private:
    http_response default_not_found(http_request &) const {
        // TODO: Store version someplace to use here.
        return http_response(http_status_code::eNotFound, std::format("404 Not Found<hr>rite/{}", "0.0.1"));
    }

    public:
    enum class error { eNoEndpoint };

    layer()
        : not_found_handler_(std::bind(&layer::default_not_found, this, std::placeholders::_1)){
    }

    void add_endpoint(rite::http::endpoint endpoint) {
        // Convert the path to a regex pattern
        endpoints_.emplace_back(endpoint);
    }

    std::pair<endpoint *, rite::http::path::result> find_endpoint(const http_request &request) {
        for (auto &endpoint : endpoints_) {
            // Skip endpoints whose methods do not match our request.
            if ((endpoint.method & static_cast<int>(request.method())) == 0)
                continue;

            rite::http::path path = endpoint.path;
            auto             result = path.match(std::string(request.path()));
            if (result) {
                return std::make_pair<rite::http::endpoint *, rite::http::path::result>(&endpoint, std::move(result.value()));
            }
        }
        throw error::eNoEndpoint;
    }

    template<typename T, typename... Args>
        requires std::derived_from<T, extension>
    void attach(Args... arguments) {
        extensions_.emplace_back(std::make_unique<T>(std::forward<Args>(arguments)...));
        extensions_.back()->on_hook(*this);
    }

    /// Perform path mapping for `req` and hand off the response into
    /// `finish`.  Usage: This function should only be used by
    /// server<T>'s, the finish function is specifically intended to
    /// write the response to the client, this is needed as each
    /// revision of the HTTP specification requires custom
    /// serialization that can't be generically represented without
    /// blocking the worker thread.
    void handle(http_request &req, std::function<void(http_response &&)> &&finish) {
        for (auto &ext : extensions_)
            ext->on_request(req);

        rite::http::endpoint    *endpoint = nullptr;
        rite::http::path::result mapping;
        std::tie(endpoint, mapping) = find_endpoint(req);

        if (endpoint) {
            // Determine the launch policy for executing the handler based on the endpoint's configuration.
            // - If a thread pool is provided, the handler must run in the thread that picks up the task.
            // - If there is no thread pool and the endpoint is not asynchronous, the handler runs in the current worker thread.
            // - If there is no thread pool and the endpoint is asynchronous, a new thread is spawned using std::launch::async.
            std::launch policy = std::launch::deferred; // Default to deferred execution (i.e. current thread)
            if (endpoint->asynchronous && !endpoint->thread_pool.has_value()) {
                // Asynchronous without a thread pool: spawn a new thread
                policy = std::launch::async;
            } else if (endpoint->thread_pool.has_value()) {
                // Thread pool is available: use deferred execution
                policy = std::launch::deferred;
            }

            std::future<void> handler = std::async(policy, [this, finish, endpoint, mapping, req]() mutable {
                http_response response = endpoint->handler(req, mapping);
                // TODO: This is not a nice design.
                // but we definitely need the hooks...
                for (auto &ext : extensions_)
                    ext->pre_send(req, response);

                finish(std::move(response));

                for (auto &ext : extensions_)
                    ext->post_send(req, response);
            });

            if (endpoint->thread_pool) {
                endpoint->thread_pool.value().dispatch([&handler]() mutable { handler.get(); });
            } else {
                // Run on this thread
                if (policy == std::launch::deferred)
                    handler.get();
                // Async automatically runs. Do not wait.
            }
        } else {
            finish(not_found_handler_(req));
        }
    }

    http_response not_found(http_request &req) {
        return not_found_handler_(req);
    }

    void set_custom_404(std::function<http_response(http_request &)> &&handler) {
        this->not_found_handler_ = handler;
    }

    private:
    std::vector<rite::http::endpoint>       endpoints_;
    std::vector<std::unique_ptr<extension>> extensions_;
    std::function<http_response(http_request &)> not_found_handler_;
};

}
