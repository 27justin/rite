#pragma once

#include <algorithm>
#include <any>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "header_map.hpp"
#include "status_code.hpp"

#include "buffer.hpp"

template<typename T>
class serializer;

struct http_response {
    public:
    enum class event { chunk, finish };

    private:
    http_status_code status_code_;
    // Responses are streamed through `buffer`
    // objects.
    // Usage:
    // #+BEGIN_SRC cpp
    // http_response response;
    // response.body(std::string("Hello, world!")); // Implicitly sends one buffer
    // for(int i = 0; i < 5; ++i) {
    //     response.stream(... data ...);
    // }
    // response.stream(buffer::finish());
    // #+END_SRC

    header_map                                            headers_;
    std::map<event, std::function<void(http_response &)>> events_;
    std::unordered_map<size_t, std::any>                  context_;

    friend class serializer<http_response>;

    public:
    std::shared_ptr<jt::mpsc<rite::buffer, jt::fifo>> channel;

    http_response()
      : channel(std::make_shared<jt::mpsc<rite::buffer, jt::fifo>>()) {}

    // http_response(const http_response &) = delete;

    /// Create a HTTP response that has Content-Type: text/html and
    /// the given parameters.
    http_response(http_status_code status_code, std::string body)
      : status_code_(status_code)
      , channel(std::make_shared<jt::mpsc<rite::buffer, jt::fifo>>()) {
        headers_["Content-Type"] = "text/html";
        set_content_length(body.size());
        this->body(body);
    };

    http_status_code status_code() const { return status_code_; }
    http_status_code set_status_code(http_status_code code) {
        status_code_ = code;
        return code;
    }

    /// Set the response body.
    /// When sending large response bodies, prefer to use `stream`
    void body(const std::string &val) {
        std::unique_ptr<std::byte[]> heap_mem = std::make_unique<std::byte[]>(val.size());
        std::span<const std::byte>   span = std::span<const std::byte>((const std::byte *)val.data(), val.size());
        std::copy(span.begin(), span.end(), heap_mem.get());

        stream(rite::buffer(std::move(heap_mem), val.size(), true));
    }

    void event(event ev, std::function<void(http_response &)> &&callback) { events_[ev] = std::move(callback); }

    void trigger(enum event ev) {
        if (events_.contains(ev))
            events_[ev](*this);
    }

    /// This function should be used alongside `stream` when
    /// large bodies are streamed to the client.
    void   set_content_length(size_t length) { headers_["Content-Length"] = std::to_string(length); }
    size_t content_length() {
        if (!headers_.contains("Content-Length"))
            return 0;
        return std::stoll(headers_.at("Content-Length"));
    }

    void stream(const std::span<std::byte> &data) {
        // Is a copy needed here?
        std::unique_ptr<std::byte[]> heap_mem = std::make_unique<std::byte[]>(data.size());
        // Copy data over
        std::span<std::byte> span = std::span<std::byte>(heap_mem.get(), data.size());
        std::copy_n(data.begin(), span.size_bytes(), span.begin());

        //clang-format off
        channel->tx().dispatch(rite::buffer(std::move(heap_mem), static_cast<ssize_t>(data.size()), false));
        //clang-format on
    }

    void stream(std::string_view str) { stream(std::span<std::byte>((std::byte *)str.data(), str.size())); }

    void stream(std::vector<std::byte> &&data) { stream(std::span<std::byte>(data)); }

    void stream(rite::buffer &&data) { channel->tx().dispatch(std::move(data)); }

    const header_map &headers() const { return headers_; }
    void              set_header(std::string_view header, std::string_view value) { headers_[std::string(header)] = value; }

    template<typename T>
    void set_context(T &&value) {
        context_[typeid(T).hash_code()] = std::move(value);
    }

    template<typename T>
    std::optional<std::reference_wrapper<T>> context() {
        if (!context_.contains(typeid(T).hash_code()))
            return std::nullopt;
        std::any &ref = context_[typeid(T).hash_code()];
        return std::any_cast<T &>(ref);
    }
};
