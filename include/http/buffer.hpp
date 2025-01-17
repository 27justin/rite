#pragma once
#include <cstdint>
#include <jt.hpp>
#include <memory>
#include <print>

namespace rite {
class buffer {
    public:
    std::unique_ptr<std::byte[]> data;
    ssize_t                      len;
    bool                         last;

    buffer()
      : data(nullptr)
      , len(0)
      , last(true) {};

    buffer(std::unique_ptr<std::byte[]> data, ssize_t len, bool last) {
        this->data.swap(data);
        this->len = len;
        this->last = last;
    }

    buffer(buffer &&other)
      : len(other.len)
      , last(other.last) {
        data.swap(other.data);
    }

    buffer(buffer &) = delete;

    void operator=(buffer &) = delete;
    void operator=(buffer &&other) {
        this->data.swap(other.data);
        this->len = other.len;
        this->last = other.last;
    }

    ~buffer() = default;

    static buffer finish() { return buffer(nullptr, 0, true); }
};
}
