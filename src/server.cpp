#include <algorithm>
#include <chrono>
#include <jt.hpp>
#include <kana.hpp>
#include <set>

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

using socket_fd = int;

void
kana::server::finish_http_request(http_request &request, http_response &response) {
    trigger(kana::server::event::pre_send, request, response);
    // Send everything but the body
    std::vector<std::byte> payload = serializer<http_response>{
        .serialize_body = false // Streamed, only serialize known
                                // portion of the response
    }(response);
    send(request.socket(), payload.data(), payload.size(), MSG_MORE);

    kana::buffer                            b;
    std::shared_ptr<jt::mpsc<kana::buffer>> channel = response.channel;
    jt::mpsc<kana::buffer>::consumer       &rx = channel->rx();
    do {
        response.trigger(http_response::event::chunk);
        b = rx.wait();
        send(request.socket(), b.data.get(), b.len, MSG_MORE);
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
    send(con.socket(), payload.data(), payload.size(), MSG_MORE);

    kana::buffer                            b;
    std::shared_ptr<jt::mpsc<kana::buffer>> channel = response.channel;
    jt::mpsc<kana::buffer>::consumer       &rx = channel->rx();
    do {
        // response.trigger(http_response::event::chunk);
        b = rx.wait();
        send(con.socket(), b.data.get(), b.len, MSG_MORE);
    } while (!b.last);
    // response.trigger(http_response::event::finish);
}

void
kana::server::start() {
    // Thread pool
    int epfd;

    jt::mpsc<std::shared_ptr<connection>> worker_pool{};
    auto                                 &work = worker_pool.rx();
    for (auto i = 0; i < server_config.worker_threads; ++i) {
        std::thread([&work, &worker_pool, &epfd, i, this]() -> void {
            static std::unique_ptr<std::byte[]> buffer = std::make_unique<std::byte[]>(server_config.worker_buffer_size);
            while (true) {
                // std::print("Worker #{}: Ready for work.\n", i);
                std::shared_ptr<connection> client = std::move(work.wait());
                // std::print("Worker #{}: sockfd:{} to handle\n", i, client->socket());
                // Process
                ssize_t bytes = recv(client->socket(), buffer.get(), server_config.worker_buffer_size, 0);
                if (bytes < 1) {
                    std::lock_guard<std::mutex> lk(connection_mtx_);
                    auto                        num = connections_.erase(client->socket());
                    // std::print("Worker #{}: sockfd:{} disconnected, continuing\n", i, client->socket());
                    continue;
                }


                std::print("req: \n{}\n\n", std::string((char*)buffer.get(), bytes));

                http_request *req = new http_request;
                bool valid = parser<http_request>{}.parse(client, std::span<const std::byte>(buffer.get(), bytes), *req);
                if (valid) {
                    trigger(kana::server::event::on_request, *req);
                    // TODO: Refactor, this is too much control-flow.
                    kana::endpoint              *e = find_endpoint(*req);
                    std::optional<http_response> response;
                    if (e != nullptr) {
                        // Walk through middlewares of this endpoint
                        for (const std::string &name : e->middlewares) {
                            kana::middleware *middleware = this->middleware(name);
                            if (middleware)
                                response = middleware->run(*req);
                            else
                                std::print("An endpoint requires middleware '{}' which was never registered.", name);
                            if (response.has_value()) {
                                // When a middleware returns a response, we flush that instead and exit early.
                                finish_http_request(*req, response.value());
                                goto next_request;
                            }
                        }

                        // Determine the launch policy for executing the handler based on the endpoint's configuration.
                        // - If a thread pool is provided, the handler must run in the thread that picks up the task.
                        // - If there is no thread pool and the endpoint is not asynchronous, the handler runs in the current worker thread.
                        // - If there is no thread pool and the endpoint is asynchronous, a new thread is spawned using std::launch::async.
                        std::launch policy = std::launch::deferred; // Default to deferred execution (i.e. current thread)

                        if (e->asynchronous && !e->thread_pool.has_value()) {
                            // Asynchronous without a thread pool: spawn a new thread
                            policy = std::launch::async;
                        } else if (e->thread_pool.has_value()) {
                            // Thread pool is available: use deferred execution
                            policy = std::launch::deferred;
                        }

                        std::future<void> handler = std::async(policy, [&e, req = std::move(req), this]() mutable {
                            http_response response = e->handler(*req);
                            finish_http_request(*req, response);
                        });

                        if (e->thread_pool) {
                            e->thread_pool.value().dispatch([&handler, req = std::move(req), this]() mutable {
                                handler.get();
                            });
                        } else {
                            handler.get();
                        }
                        // std::print("Worker-Thread #{}: Client served, releasing connection to him.\n", i);
                        bool keep_alive = req->header("Connection").value_or("keep-alive") == "keep-alive";
                        if(!keep_alive)
                            client->release();
                        delete req;
                    } else {
                        std::print("No endpoint found for path {}\n", req->path());
                        http_response r(http_status_code::eNotFound, "Not found.");
                        finish_http_request(*req, r);
                        client->release();
                    }
                } else {
                    // TODO: Actually do close the connection
                    std::print("Invalid request, terminating connection\n");
                }
            next_request:
            }
        }).detach();
    }

    // Networking

    int sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
    int enable = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int));
    struct sockaddr_in server_addr {
        .sin_family = AF_INET, .sin_port = htons(std::get<1>(server_config.bind[0]))
    };
    server_addr.sin_addr.s_addr = htonl(std::get<0>(server_config.bind[0]));

    int result = ::bind(sock, (const sockaddr *)&server_addr, sizeof(server_addr));
    if (result < 0) {
        perror("Failed to bind socket");
    }

    // result = listen(sock, server_config.max_connections);
    result = listen(sock, SOMAXCONN);

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
                if (client_socket == -1) {
                    perror("Failed to accept client");
                    continue;
                }
                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET; /*| */ // EPOLLHUP | EPOLLRDHUP | EPOLLERR;
                ev.data.fd = client_socket;
                {
                    auto                        con = std::make_shared<connection>(client_socket, epfd, client_address, client_address_len);
                    std::lock_guard<std::mutex> lock(connection_mtx_);
                    connections_[client_socket] = con;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_socket, &ev) == -1) {
                        perror("Failed to add epoll socket");
                        std::exit(1);
                    }
                    // std::print("[Server]: Accepted new client #{} (sock:{}).\n", connections_.size(), client_socket);

                    std::thread(std::bind(&server::connection_sentinel, this, std::placeholders::_1), con)
                        .detach();
                }
            } else { // Client event
                if ((event.events & (EPOLLIN | EPOLLET | EPOLLHUP)) != 0) {
                    std::shared_ptr<connection> client;
                    {
                        std::lock_guard<std::mutex> lock(connection_mtx_);
                        if (!connections_.contains(event.data.fd)) {
                            // Remove from epollfd just to be sure
                            epoll_ctl(epfd, EPOLL_CTL_DEL, event.data.fd, nullptr);
                            continue;
                        }
                        client = connections_[event.data.fd];
                    }
                    client->was_active();
                    worker_pool.tx()(std::move(client));
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
kana::server::connection_sentinel(std::shared_ptr<connection> con) {
    for (;;) {
        // std::print("Connection sentinel active!\n");
        std::unique_lock<std::mutex> lk(con->mutex());
        auto last_active = con->last_active();
        auto keep_alive = con->get_keep_alive();
        auto next_wakeup = last_active + keep_alive;

        con->cv().wait_until(lk, next_wakeup, [&con]() {
            return con->idle() || con->use_count() == 0;
        });
        if (con->idle() || con->use_count() == 0)
            break;
    }
    // std::print("Disconnecting client\n");

    // Remove connection
    std::lock_guard<std::mutex> lock(connection_mtx_);
    connections_.erase(con->socket());
}
