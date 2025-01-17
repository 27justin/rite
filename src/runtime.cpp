#include <runtime.hpp>
#include <iostream>

void
kana::runtime::start() {
    if(num_workers_ == 0) {
        std::cerr << "Runtime: starting with 0 threads configured, are you sure this is intended?" << std::endl;
    }

    auto &consumer = thread_pool_.rx();
    for (size_t i = 0; i < num_workers_; ++i) {
        threads_.push_back(std::thread([&consumer]() {
            for(;;) {
                auto task = consumer.wait();
                task();
            }
        }));
    }
    for (auto &thread : threads_) {
        thread.join();
    }
    // TODO: Can this actually happen? Would be bad for us.
    for(;;);
}

void
kana::runtime::worker_threads(size_t num) {
    threads_.reserve(num);
    num_workers_ = num;
}

void
kana::runtime::dispatch(std::function<void()> &&work) {
    thread_pool_.tx()(std::move(work));
}

