#pragma once

#include <jt.hpp>

#include <functional>

namespace kana {
class runtime {
    jt::mpsc<std::function<void()>> thread_pool_;
    std::vector<std::thread>        threads_;
    size_t                          num_workers_;

    public:
    template<typename T>
    void attach(T &&run) {
        run.runtime = this;
        std::thread([run]() mutable { run(); }).detach();
    }

    void worker_threads(size_t);

    void dispatch(std::function<void()> &&);

    [[noreturn]]
    void start();
};
};
