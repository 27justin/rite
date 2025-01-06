#pragma once

#include <functional>
#include <future>
#include <list>
#include <optional>
#include <regex>
#include <variant>

#include "http/method.hpp"
#include "http/request.hpp"
#include "http/response.hpp"

namespace kana {
struct endpoint {
    public:
    int                                                       method; // A bit-set representing the HTTP methods (e.g., GET, POST) that this endpoint supports.
    std::variant<std::string, std::regex>                     path;

    // The handler that runs when the endpoint is called.
    std::function<http_response(http_request &)> handler;

    // The maximum number of concurrent requests is limited by the
    // number of `worker_threads` in `kana::server`. Setting this
    // flag to `true` allows the endpoint to bypass this limit by
    // spawning a new thread for each incoming request, enabling
    // greater concurrency at the cost of increased resource usage.
    bool asynchronous = false;

    // The `thread_pool` field allows you to specify a custom thread pool for processing requests
    // associated with this endpoint. Instead of spawning a new thread for each request, the handler
    // function will be dispatched into the provided `jt::mpsc` channel, where it can be picked up
    // and executed by your own thread pool.
    //
    // This is particularly useful for endpoints that may have long-running operations or for
    // handling server-sent events (SSE) that keep a connection open for streaming data. By using
    // your own thread pool, you can limit the number of concurrent threads to a fixed size (N),
    // preventing the potential for resource exhaustion that could occur with the default `asynchronous`
    // behavior, which may spawn an unbounded number of threads.
    std::optional<jt::mpsc<std::function<void()>>::producer> thread_pool;
    // A list of middleware functions that will be applied to requests
    // before reaching the handler, allowing for pre-processing,
    // authentication, logging, etc.
    std::vector<std::string> middlewares;
};

class controller_config {
    private:
    std::list<endpoint> endpoints_;

    friend class server;

    public:
    controller_config &add_endpoint(endpoint &&endpoint) {
        endpoints_.push_back(endpoint);
        return *this;
    }
};

class controller {
    protected:
    kana::controller_config config;
    friend class server;

    public:
    controller() {};
    virtual void setup(kana::controller_config &config) = 0;
};
}
