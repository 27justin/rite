#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <print>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <utility>

using namespace std::chrono;

using sockfd = int;
template<typename T>
class connection {
    public:
    using native_handle = int;

    protected:
    sockfd                   socket_;
    struct sockaddr_storage  address_;
    size_t                   address_len_;
    sockfd                   epoll_set_;
    steady_clock::time_point last_active_;
    microseconds             keep_alive_; // in usecs (microseconds)
    std::condition_variable  cv_;
    std::atomic_intmax_t     refs_;
    std::atomic_bool         closed_;

    mutable std::mutex lock_;

    public:
    // Refer to docs/connection.org for an explanation
    static connection<T> *invalid;

    connection(sockfd socket, struct sockaddr_storage address, size_t addr_len)
      : socket_(socket)
      , address_(address)
      , address_len_(addr_len)
      , last_active_(steady_clock::now())
      , keep_alive_(seconds(5))
      , refs_(0)
      , closed_(false)
      , lock_() {}

    // IMPORTANT:
    // This move constructor should only ever be called BEFORE
    // the connection is fully estabilished and part of a `server<T>`.
    // If the move constructor is ever called /after/ ~on_accept~,
    // this may very well lead to UB.
    connection(connection<void> &&other)
      : socket_(std::exchange(other.socket_, -1))
      , address_(other.address_)
      , address_len_(other.address_len_)
      , last_active_(other.last_active_)
      , keep_alive_(other.keep_alive_)
      , refs_(other.refs_.load())
      , closed_(other.closed_.load())
      , lock_() {}

    virtual ~connection() {
        if (socket_ != -1)
            ::close(socket_);
    };

    sockfd socket() { return socket_; }

    struct sockaddr_storage addr() const { return address_; }

    size_t addr_length() const { return address_len_; }

    void was_active() {
        std::lock_guard<std::mutex> lock(lock_);
        last_active_ = steady_clock::now();
        cv_.notify_all();
    }

    steady_clock::time_point last_active() const { return last_active_; }

    bool idle() const { return duration_cast<microseconds>(steady_clock::now() - last_active_) > keep_alive_; }

    void set_keep_alive(microseconds usecs) {
        std::lock_guard<std::mutex> lock(lock_);
        keep_alive_ = usecs;
        cv_.notify_all();
    }

    microseconds get_keep_alive() const { return keep_alive_; }

    void take() {
        refs_.fetch_add(1);
        cv_.notify_all();
    }
    void release() {
        refs_.fetch_sub(1);
        cv_.notify_all();
    }

    void close() {
        closed_.store(true);
        cv_.notify_all();
    }

    bool is_closed() { return closed_.load(); }

    std::condition_variable &cv() { return cv_; }
    std::mutex              &mutex() { return lock_; }
    uintmax_t                use_count() const { return refs_.load(); }

    std::lock_guard<std::mutex>  lock() { return std::lock_guard<std::mutex>(lock_); }
    std::unique_lock<std::mutex> unique_lock() { return std::unique_lock<std::mutex>(lock_); }

    virtual ssize_t write(std::span<const std::byte> what, int flags) = 0;
    virtual ssize_t read(std::span<std::byte> target, int flags) = 0;
};

// TODO: I'd like this to be `constexpr static` and inline defined, but `reinterpret_cast` is not `constexpr`
// such that inline I'd (apparently only have the option of uintptr_t, but I'd like to avoid casting it to connection<> when using connection<T>::invalid)
template<typename T>
connection<T> *connection<T>::invalid = reinterpret_cast<connection<T> *>((uintptr_t)0x8000000000000000);
