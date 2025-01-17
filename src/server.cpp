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
#include <thread>
#include <unistd.h>
#include <netinet/tcp.h>

#include <controller.hpp>
#include <http/request.hpp>
#include <http/response.hpp>

#include <future>
#include <http/parser.hpp>
#include <http/serializer.hpp>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <plain.hpp>
#include <tls.hpp>

#define BENCHMARK_MS(label, expr)                                                                                                                                                                         \
    {                                                                                                                                                                                                  \
        auto __start = std::chrono::steady_clock::now();                                                                                                                                               \
        expr;                                                                                                                                                                                          \
        auto __end = std::chrono::steady_clock::now();                                                                                                                                                 \
        std::cout << label << " took " << std::setprecision(5) << std::chrono::duration_cast<std::chrono::microseconds>( __end - __start ).count() / 1000.0 << "ms\n"; \
        \
    }

#define BENCHMARK(label, expr)                                                                                                                                                                         \
    {                                                                                                                                                                                                  \
        auto __start = std::chrono::steady_clock::now();                                                                                                                                               \
        expr;                                                                                                                                                                                          \
        auto __end = std::chrono::steady_clock::now();                                                                                                                                                 \
        std::print("{} took {}us\n", label, std::chrono::duration_cast<std::chrono::microseconds>( __end - __start ).count()); \
        \
    }

void
kana::server::finish_http_request(http_request &request, http_response &response) {
    trigger(kana::server::event::pre_send, request, response);
    // Send everything but the body
    std::vector<std::byte> payload = serializer<http_response>{
        .serialize_body = false // Streamed, only serialize known
                                // portion of the response
    }(response);
    if(request.client()->write(payload, MSG_MORE) < 1) return;

    kana::buffer                            b;
    std::shared_ptr<jt::mpsc<kana::buffer>> channel = response.channel;
    jt::mpsc<kana::buffer>::consumer       &rx = channel->rx();
    do {
        response.trigger(http_response::event::chunk);
        b = rx.wait();
        if(request.client()->write(std::span<std::byte>(b.data.get(), b.len), MSG_NOSIGNAL | (b.last ? 0 : MSG_MORE)) < 1) return;
    } while (!b.last);
    trigger(kana::server::event::post_send, request, response);
    response.trigger(http_response::event::finish);
}

void
dbg_finish(connection<void> &con, http_response &response) {
    // Send everything but the body
    std::vector<std::byte> payload = serializer<http_response>{
        .serialize_body = false // Streamed, only serialize known
                                // portion of the response
    }(response);
    if(con.write(payload, MSG_MORE) < 1) return;

    kana::buffer                            b;
    std::shared_ptr<jt::mpsc<kana::buffer>> channel = response.channel;
    jt::mpsc<kana::buffer>::consumer       &rx = channel->rx();
    do {
        // response.trigger(http_response::event::chunk);
        b = rx.wait();
        if(con.write(std::span<std::byte>(b.data.get(), b.len), MSG_NOSIGNAL | (b.last ? 0 : MSG_MORE)) < 1) return;
    } while (!b.last);
    response.trigger(http_response::event::finish);
}

void
kana::server::start() {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
    // jt::mpsc<std::shared_ptr<connection>> worker_pool{};
    jt::mpsc<std::tuple<size_t, server_bind_config *>> worker_pool{};
    // jt::mpsc<sockfd> worker_pool{};

    auto &work = worker_pool.rx();
    for (size_t i = 0; i < server_config.worker_threads; ++i) {
        std::thread([&work, &worker_pool, i, this]() -> void {
            static std::unique_ptr<std::byte[]> buffer = std::make_unique<std::byte[]>(server_config.worker_buffer_size);

            while (true) {
                server_bind_config *ctx;
                size_t index;
                std::tie(index, ctx) = work.wait();

                connection<void> **raw_client = &ctx->connections_[index];
                uintptr_t mask = (~(0ULL) >> 16);
                connection<void> *client = reinterpret_cast<connection<void> *>(((uintptr_t) (*raw_client)) & mask);

                ssize_t bytes = client->read(std::span<std::byte>(buffer.get(), server_config.worker_buffer_size), 0); //recv(client->socket(), buffer.get(), server_config.worker_buffer_size, 0);
                if (bytes < 1) {
                    std::cout << " Worker encountered bad sockfd: " << client->socket() << std::endl;
                    // Release root ref, with this the socket should
                    // get immediately closed by the sentinel thread
                    client->release();
                    client->close();
                    continue;
                }

                http_request *req = new http_request;
                bool          valid = parser<http_request>{}.parse(client, std::span<const std::byte>(buffer.get(), bytes), *req);
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

                        client->take();
                        std::future<void> handler = std::async(policy, [&e, req, client, this]() mutable {
                            http_response response = e->handler(*req);
                            finish_http_request(*req, response);
                            client->release();
                            delete req;
                        });

                        if (e->thread_pool) {
                            e->thread_pool.value().dispatch([&handler]() mutable { handler.get(); });
                        } else {
                            if (policy == std::launch::deferred)
                                handler.get();
                            // Otherwise we'll ignore it; std should create the thread for us.
                        }
                        // std::print("Worker-Thread #{}: Client served, releasing connection to him.\n", i);
                    } else {
                        std::print("No endpoint found for path {}\n", req->path());
                        http_response r(http_status_code::eNotFound, "Not found.");
                        finish_http_request(*req, r);
                    }
                } else {
                    // TODO: Actually do close the connection
                    std::print("Invalid request, terminating connection\n");
                }
              next_request:
                client->release();
            }
        }).detach();
    }

    // Networking
    std::vector<std::thread> active_threads;
    for (auto const &binding : server_config.bind) {
        // TODO: IPv6
        uint32_t ip_;
        uint16_t port_;
        std::variant<kana::http_config, kana::https_config> protocol_config;
        std::unique_ptr<server_bind_config> current = std::make_unique<server_bind_config>();
        std::tie(ip_, port_, protocol_config) = binding;

        // TODO: Urgh.... properly encapsulate this
        SSL_CTX *ctx = nullptr;

        if (std::holds_alternative<kana::http_config>(protocol_config))
            current->protocol_ = kana::protocol::http;
        else if(std::holds_alternative<kana::https_config>(protocol_config)) {
            current->protocol_ = kana::protocol::https;
            const SSL_METHOD *method = TLS_server_method();
            ctx = SSL_CTX_new(method);

            kana::https_config &config = std::get<kana::https_config>(protocol_config);
            if (SSL_CTX_use_certificate_file(ctx, config.certificate_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
                ERR_print_errors_fp(stderr);
                exit(EXIT_FAILURE);
            }
            if(SSL_CTX_use_PrivateKey_file(ctx, config.private_key_file.c_str(), SSL_FILETYPE_PEM) <= 0){
                ERR_print_errors_fp(stderr);
                exit(EXIT_FAILURE);
            }
        }


        if(current->protocol_ == protocol::http || current->protocol_ == protocol::https)
            current->server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
        else if (current->protocol_ == protocol::quic)
            current->server_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);

        int enable = 1;
        setsockopt(current->server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
        setsockopt(current->server_fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int));
        setsockopt(current->server_fd, SOL_SOCKET, TCP_NODELAY, &enable, sizeof(int));

        std::print("Binding port {}\n", port_);
        struct sockaddr_in address {
            .sin_family = AF_INET, .sin_port = ntohs(port_), .sin_addr = {.s_addr = htonl(ip_)},
            .sin_zero = { 0 }
        };
        int result = ::bind(current->server_fd, (struct sockaddr *) &address, sizeof(address));
        if (result != 0) {
            perror("Failed to bind socket");
        }
        result = listen(current->server_fd, server_config.max_connections);
        current->connections_.resize(server_config.max_connections, (connection<void> *) ((uintptr_t) 1 << 63));


        // Add server socket to epollfd
        current->epoll_fd = epoll_create1(0);
        struct epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = current->server_fd;
        epoll_ctl(current->epoll_fd, EPOLL_CTL_ADD, current->server_fd, &event);

        active_threads.emplace_back(std::thread([ctx, &worker_pool, this, current = std::move(current)]() {
            std::unique_ptr<struct epoll_event[]> events = std::make_unique<struct epoll_event[]>(server_config.max_connections + 1);

            struct sockaddr_storage client_address {};
            socklen_t               client_address_len = 0;

            std::cout << "Thread for " << serializer<kana::protocol>{}((current->protocol_)) << " is active." << std::endl;
            std::print("{}: max simultaneous connections: {}\n", serializer<kana::protocol>{}((current->protocol_)), server_config.max_connections);
            for (;;) {
                size_t ready = epoll_wait(current->epoll_fd, events.get(), server_config.max_connections + 1, -1);
                for (size_t i = 0; i < ready; i++) {
                    struct epoll_event &event = events[i];
                    if (event.data.fd == current->server_fd) { // Server socket
                        int client_socket = accept(current->server_fd, (struct sockaddr *)&client_address, &client_address_len);
                        if (client_socket < 1) {
                            perror("Failed to accept client");
                            continue;
                        }
                        // setsockopt(client_socket, SOL_SOCKET, TCP_NODELAY, &enable, sizeof(int));

                        struct epoll_event ev;
                        ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET;
                        {
                            auto *con = new connection<tls>(ctx, client_socket, client_address, client_address_len);
                            auto next_it = std::find_if(current->connections_.begin(), current->connections_.end(), [](auto ptr) {
                                // find invalid one
                                return (reinterpret_cast<uintptr_t>(ptr) & ((uintptr_t) 1 << 63)) != 0;
                            });
                            if (next_it == current->connections_.end()) {
                                std::print("Active indexes all taken up.\n");
                                delete con;
                                continue;
                            }
                            auto next_idx = std::distance(current->connections_.begin(), next_it);
                            // std::cout << "Found connection " << (*next_it) << " that was cleaned up!" << std::endl;
                            current->connections_[next_idx] = con;

                            ev.data.u64 = next_idx;
                            std::thread(std::bind(&server::connection_sentinel, this, std::placeholders::_1, std::placeholders::_2), next_idx, current.get()).detach();
                        }
                        if (epoll_ctl(current->epoll_fd, EPOLL_CTL_ADD, client_socket, &ev) == -1) {
                            perror("Failed to add epoll socket");
                        }
                    } else { // Client event
                        if ((event.events & (EPOLLIN | EPOLLHUP | EPOLLERR)) != 0) {
                            connection<void> *client = reinterpret_cast<connection<void> *>(current->connections_[event.data.u64]);
                            if (((uintptr_t)client & ((uintptr_t)1 << 63)) != 0) {
                                // Event was dispatched for client that has already been deallocated.
                                continue;
                            }

                            client = reinterpret_cast<connection<void>*>(((uintptr_t) client) & ((~0ULL) >> 16));
                            client->take();
                            client->was_active();
                            worker_pool.tx()(std::make_tuple<size_t, server_bind_config *>((size_t) event.data.u64, current.get()));
                        }
                    }
                }
            }
        }));
    }

    for (auto &th : active_threads) {
        th.join();
    }
    // This should never run.
    std::print("It ran....\n");
    for(;;) {}
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
kana::server::connection_sentinel(size_t connection_idx, server_bind_config *ctx) {
    connection<void> *con = reinterpret_cast<connection<void> *>(ctx->connections_[connection_idx]);
    // Remove application specific information from the pointer
    uintptr_t mask = (~(0ULL) >> 16);
    con = reinterpret_cast<connection<void> *>(((uintptr_t) con) & mask);

    for (;;) {
        std::unique_lock<std::mutex> lk(con->mutex());
        auto                         last_active = con->last_active();
        auto                         keep_alive = con->get_keep_alive();
        auto                         next_wakeup = last_active + keep_alive;

        con->cv()
            .wait_until(lk, next_wakeup, [&con]() {
                return (con->idle() && con->use_count() <= 0) || con->is_closed();
            });
        if ((con->idle() && con->use_count() <= 0) || con->is_closed())
            break;
    }
    ctx->connections_[connection_idx] = (connection<void> *) ((uintptr_t)con | (((uintptr_t)1) << 63));
    // std::cerr << "Connection cleaned up: " << connections_[connection_idx] << std::endl;
    delete con;
}

