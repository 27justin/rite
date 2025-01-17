#include <algorithm>
#include <chrono>
#include <cstdint>
#include <jt.hpp>
#include <kana.hpp>
#include <set>
#include <iostream>

#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <controller.hpp>
#include <http/request.hpp>
#include <http/response.hpp>

#include <future>
#include <http/parser.hpp>
#include <http/serializer.hpp>

#define BENCHMARK(label, expr)                                                                                                                                                                         \
    {                                                                                                                                                                                                  \
        auto __start = std::chrono::steady_clock::now();                                                                                                                                               \
        expr;                                                                                                                                                                                          \
        auto __end = std::chrono::steady_clock::now();                                                                                                                                                 \
        std::print("{} took {}us\n", label, std::chrono::duration_cast<std::chrono::microseconds>( __end - __start ).count()); \
        \
    }

using socket_fd = int;

void
kana::server::finish_http_request(http_request &request, http_response &response) {
    trigger(kana::server::event::pre_send, request, response);
    // Send everything but the body
    std::vector<std::byte> payload = serializer<http_response>{
        .serialize_body = false // Streamed, only serialize known
                                // portion of the response
    }(response);
    // TODO: Consider using MSG_MORE flag to accumulate the response.
    send(request.socket(), payload.data(), payload.size(), 0);

    kana::buffer                            b;
    std::shared_ptr<jt::mpsc<kana::buffer>> channel = response.channel;
    jt::mpsc<kana::buffer>::consumer       &rx = channel->rx();
    do {
        response.trigger(http_response::event::chunk);
        b = rx.wait();
        send(request.socket(), b.data.get(), b.len, 0);
    } while (!b.last);
    trigger(kana::server::event::post_send, request, response);
    response.trigger(http_response::event::finish);
    // close(request.socket());
}

void
dbg_finish(connection &con, http_response &response) {
    // Send everything but the body
    std::vector<std::byte> payload = serializer<http_response>{
        .serialize_body = false // Streamed, only serialize known
                                // portion of the response
    }(response);
    if(send(con.socket(), payload.data(), payload.size(), MSG_NOSIGNAL) < 1) return;

    kana::buffer                            b;
    std::shared_ptr<jt::mpsc<kana::buffer>> channel = response.channel;
    jt::mpsc<kana::buffer>::consumer       &rx = channel->rx();
    do {
        // response.trigger(http_response::event::chunk);
        b = rx.wait();
        if(send(con.socket(), b.data.get(), b.len, MSG_NOSIGNAL) < 1) return;
    } while (!b.last);
    // response.trigger(http_response::event::finish);
}

void
kana::server::start() {
    // Thread pool
    int epfd;

    // jt::mpsc<std::shared_ptr<connection>> worker_pool{};
    jt::mpsc<connection *> worker_pool{};
    // jt::mpsc<sockfd> worker_pool{};

    auto &work = worker_pool.rx();
    for (size_t i = 0; i < server_config.worker_threads; ++i) {
        std::thread([&work, &worker_pool, &epfd, i, this]() -> void {
            static std::unique_ptr<std::byte[]> buffer = std::make_unique<std::byte[]>(server_config.worker_buffer_size);
            while (true) {
                // std::print("Worker #{}: Ready for work.\n", i);
                // std::shared_ptr<connection> client = std::move(work.wait());
                connection *client = std::move(work.wait());
                std::cerr << ("Got conn: ") << client<< std::endl;
                // sockfd fd = work.wait();
                // std::print("Worker #{}: sockfd:{} to handle\n", i, client->socket());
                // Process
                ssize_t bytes = recv(client->socket(), buffer.get(), server_config.worker_buffer_size, 0);
                if (bytes < 1) {
                    // Release root ref, with this the socket should
                    // get immediately closed by the sentinel thread
                    client->release();
                    continue;
                }

                http_response response(http_status_code::eOk, "okay, let's test our throughput");
                dbg_finish(*client, response);
                client->release();

                // http_request *req = new http_request;
                // bool          valid = parser<http_request>{}.parse(client, std::span<const std::byte>(buffer.get(), bytes), *req);
                // if (valid) {
                //     trigger(kana::server::event::on_request, *req);
                //     // TODO: Refactor, this is too much control-flow.
                //     kana::endpoint              *e = find_endpoint(*req);
                //     std::optional<http_response> response;
                //     if (e != nullptr) {
                //         // Walk through middlewares of this endpoint
                //         for (const std::string &name : e->middlewares) {
                //             kana::middleware *middleware = this->middleware(name);
                //             if (middleware)
                //                 response = middleware->run(*req);
                //             else
                //                 std::print("An endpoint requires middleware '{}' which was never registered.", name);
                //             if (response.has_value()) {
                //                 // When a middleware returns a response, we flush that instead and exit early.
                //                 finish_http_request(*req, response.value());
                //                 client->release();
                //                 goto next_request;
                //             }
                //         }

                //         // Determine the launch policy for executing the handler based on the endpoint's configuration.
                //         // - If a thread pool is provided, the handler must run in the thread that picks up the task.
                //         // - If there is no thread pool and the endpoint is not asynchronous, the handler runs in the current worker thread.
                //         // - If there is no thread pool and the endpoint is asynchronous, a new thread is spawned using std::launch::async.
                //         std::launch policy = std::launch::deferred; // Default to deferred execution (i.e. current thread)

                //         if (e->asynchronous && !e->thread_pool.has_value()) {
                //             // Asynchronous without a thread pool: spawn a new thread
                //             policy = std::launch::async;
                //         } else if (e->thread_pool.has_value()) {
                //             // Thread pool is available: use deferred execution
                //             policy = std::launch::deferred;
                //         }

                //         std::future<void> handler = std::async(policy, [&e, req, client, this]() mutable {
                //             client->take();
                //             http_response response = e->handler(*req);
                //             finish_http_request(*req, response);
                //             client->release();
                //             delete req;
                //         });

                //         if (e->thread_pool) {
                //             e->thread_pool.value().dispatch([&handler]() mutable { handler.get(); });
                //         } else {
                //             if (policy == std::launch::deferred)
                //                 handler.get();
                //             // Otherwise we'll ignore it; std should create the thread for us.
                //         }
                //         // std::print("Worker-Thread #{}: Client served, releasing connection to him.\n", i);
                //     } else {
                //         std::print("No endpoint found for path {}\n", req->path());
                //         http_response r(http_status_code::eNotFound, "Not found.");
                //         finish_http_request(*req, r);
                //         client->release();
                //     }
                // } else {
                //     // TODO: Actually do close the connection
                //     std::print("Invalid request, terminating connection\n");
                // }
              // next_request:
            }
        }).detach();
    }

    // Networking

    int sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
    int enable = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int));
    struct sockaddr_in server_addr {
        .sin_family = AF_INET, .sin_port = htons(std::get<1>(server_config.bind[0])), .sin_addr = { 0 }, .sin_zero = { 0 }
    };
    server_addr.sin_addr.s_addr = htonl(std::get<0>(server_config.bind[0]));

    int result = ::bind(sock, (const sockaddr *)&server_addr, sizeof(server_addr));
    if (result < 0) {
        perror("Failed to bind socket");
    }

    result = listen(sock, server_config.max_connections);
    // result = listen(sock, SOMAXCONN);
    // connections_.resize(server_config.max_connections, (uintptr_t) ((uintptr_t) 1 << 63));
    connections_.reserve(server_config.max_connections);
    // for(auto &at : connections_) {
    for(size_t i = 0; i < server_config.max_connections; ++i) {
        connections_.emplace_back((uintptr_t) ((uintptr_t) 1 << 63));
    }


    std::unique_ptr<struct epoll_event[]> events = std::make_unique<struct epoll_event[]>(server_config.max_connections + 1);
    // Add server socket to epollfd
    {
        epfd = epoll_create(1);
        struct epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = sock;
        epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &event);
    }

    struct sockaddr_storage client_address {};
    socklen_t               client_address_len = 0;
    for (;;) {
        // std::print("Server-Thread: Waiting for events\n");
        // std::print("We are waiting for events.\n");
        size_t ready = epoll_wait(epfd, events.get(), server_config.max_connections + 1, -1);
        for (size_t i = 0; i < ready; i++) {
            struct epoll_event &event = events[i];
            if (event.data.fd == sock) { // Server socket
                int client_socket = accept(sock, (struct sockaddr *)&client_address, &client_address_len);
                if (client_socket < 1) {
                    perror("Failed to accept client");
                    continue;
                }
                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET;
                {
                    auto *con = new connection(client_socket, epfd, client_address, client_address_len);

                    auto next_it = std::find_if(connections_.begin(), connections_.end(), [](auto &atomic_ptr) {
                        // find invalid one
                        uintptr_t ptr = atomic_ptr.load();
                        return (ptr & ((uintptr_t) 1 << 63)) != 0;
                    });
                    if (next_it == connections_.end()) {
                        std::print("Active indexes all taken up.\n");
                        delete con;
                    }
                    auto next_idx = std::distance(connections_.begin(), next_it);
                    std::cout << "Found connection " << (*next_it).load() << " that was cleaned up!" << std::endl;
                    connections_[next_idx] = reinterpret_cast<uintptr_t>(con);

                    ev.data.u64 = next_idx;
                    std::thread(std::bind(&server::connection_sentinel, this, std::placeholders::_1), next_idx).detach();
                }
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_socket, &ev) == -1) {
                    perror("Failed to add epoll socket");
                }
            } else { // Client event
                if ((event.events & (EPOLLIN | EPOLLHUP | EPOLLERR)) != 0) {
                    // std::shared_ptr<connection> client;
                    // {
                    //     std::lock_guard<std::mutex> lock(connection_mtx_);
                    //     if (!connections_.contains(event.data.fd)) {
                    //         // Remove from epollfd just to be sure
                    //         epoll_ctl(epfd, EPOLL_CTL_DEL, event.data.fd, nullptr);
                    //         continue;
                    //     }
                    //     client = connections_[event.data.fd];
                    // }
                    connection *client = reinterpret_cast<connection *>(connections_[event.data.u64].load());
                    if (((uintptr_t)client & ((uintptr_t)1 << 63)) != 0) {
                        // ded
                        std::print("dead pointer, skipping ev\n");
                        continue;
                    } else if (client->use_count() <= 0) continue;
                    client->was_active();
                    worker_pool.tx()(static_cast<connection *&&>(client));
                    // worker_pool.tx()(int(event.data.fd));
                }
            }
        }
    }
}


// TODO: Considerations:
//
// Timeout management is a kinda complicated, the current
// approach is that on each accept(), we spawn a new thread
// that checks whether our socket is supposed to be alive, or
// dead, understandedly, this sucks up some resources (1
// thread per connection)
//
// Another idea I had was to perform the check in this loop,
// put all connections into a std::deque<> and walk from the front
// (oldest connection) to the back (newest connection), checking the
// times for each.
void
// kana::server::connection_sentinel(std::shared_ptr<connection> con) {
kana::server::connection_sentinel(size_t connection_idx) {
    connection *con = reinterpret_cast<connection *>(connections_[connection_idx].load());
    for (;;) {
        std::unique_lock<std::mutex> lk(con->mutex());
        auto                         last_active = con->last_active();
        auto                         keep_alive = con->get_keep_alive();
        auto                         next_wakeup = last_active + keep_alive;

        con->cv()
            .wait_until(lk, next_wakeup, [&con]() { return con->idle() || con->use_count() <= 0; });
        if (con->idle() || con->use_count() <= 0)
            break;
    }
    connections_[connection_idx].store(((uintptr_t)con | (((uintptr_t)1) << 63)));
    // std::cerr << "Connection cleaned up: " << connections_[connection_idx] << std::endl;
    // Remove connection
    std::cerr << "Deallocating " << con << std::endl;
    delete con;
}
