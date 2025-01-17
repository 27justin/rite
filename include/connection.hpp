#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <print>
#include <sys/epoll.h>
#include <sys/socket.h>

using namespace std::chrono;

using sockfd = int;
class connection {
    sockfd                   socket_;
    struct sockaddr_storage  address_;
    size_t                   address_len_;
    sockfd                   epoll_set_;
    steady_clock::time_point last_active_;
    microseconds             keep_alive_; // in usecs (microseconds)
    std::condition_variable  cv_;
    std::atomic_intmax_t     refs_;

    mutable std::mutex lock_;

    public:
    connection(sockfd socket, sockfd epoll_set, struct sockaddr_storage address, size_t addr_len)
      : socket_(socket)
      , address_(address)
      , address_len_(addr_len)
      , epoll_set_(epoll_set)
      , last_active_(steady_clock::now())
      , keep_alive_(seconds(5))
      , refs_(1)
      , lock_() {}

    ~connection() {
        close(socket_);
        // NOTE: The following peace of code is deadly.  When firing
        // at the server with very high RPS, I frequently encountered
        // an issue, where the server would crash with various
        // syptoms.  sometimes it was a SEGFAULT, other times a
        // malloc(): unsorted double linked list corrupted From my
        // research, calling close() on a socket also automatically
        // removes it from all active epoll sets, so this should be
        // safe from memory leaks.
        //
        epoll_ctl(epoll_set_, EPOLL_CTL_DEL, socket_, nullptr);
    }

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
        // std::print("Connection got adopted. {}\n", refs_.load());
        cv_.notify_all();
    }
    void release() {
        refs_.fetch_sub(1);
        cv_.notify_all();
    }
    std::condition_variable &cv() { return cv_; }
    std::mutex              &mutex() { return lock_; }
    uintmax_t                use_count() const { return refs_.load(); }
};

struct connection_state_comparator {

    bool operator()(const connection **left, const connection **right) {
        // Active connections have the MSB of the pointer set to one, these ones
        // will thus gravitate to the beginning of the std::set.
        // Stale pointers will be updated to be zeroed out on the MSBs, these ones can be
        // safely reused.
        // Using this, we should be able to perform a binary search for usage space in our std::set, without reallocations.
        uint16_t msb_left = reinterpret_cast<uintptr_t>(left) & (static_cast<uintptr_t>(std::numeric_limits<uint16_t>::max()) << 48);
        uint16_t msb_right = reinterpret_cast<uintptr_t>(right) & (static_cast<uintptr_t>(std::numeric_limits<uint16_t>::max()) << 48);
        return msb_left > msb_right;
    }
};
