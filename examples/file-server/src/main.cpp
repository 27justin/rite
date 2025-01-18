#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <optional>
#include <time.h>

#include <arpa/inet.h>
#include <filesystem>
#include <iostream>
#include <sys/socket.h>

#include <http/behaviour.hpp>
#include <http/endpoint.hpp>
#include <http/parser.hpp>
#include <protocols/http.hpp>
#include <protocols/https.hpp>
#include <runtime.hpp>
#include <server.hpp>

#include <magic.h>


std::optional<std::string>
guess_content_type(const std::filesystem::path &file) {
    const char *mime = nullptr;
    magic_t     magic;

    magic = magic_open(MAGIC_MIME_TYPE);
    if (magic_load(magic, nullptr) != 0) {
        std::print("magic_load failed\n");
        magic_close(magic);
        return std::nullopt;
    }

    mime = magic_file(magic, file.c_str());
    if (mime == nullptr) {
        std::print("magic_file failed\n");
        magic_close(magic);
        return std::nullopt;
    }

    std::string mime_type(mime);
    magic_close(magic);
    return mime_type;
}

int
main() {
    rite::runtime *runtime = new rite::runtime();
    runtime->worker_threads(8);

    // A layer represents the default behaviour of the server.
    // The default `layer` that we expose is a very simple
    // path mapping controller suitable for CRUD applications.
    //
    // To cater to a large audience however; the https/http server
    // both do not really care for whatever the layer (`behaviour` on
    // the server instance) is, all it has to do is implement an
    // `handle(http_request, std::function<void(http_response &&)>
    // &&) -> void` function.
    std::shared_ptr<rite::http::layer> lyr = std::make_shared<rite::http::layer>();

    // clang-format off
    rite::http::endpoint image {
        .method = http_method::GET,
        .path = rite::http::path("/{file:.*}"),
        .handler = [lyr](http_request &request, rite::http::path::result mapping) -> http_response {
            using path = std::filesystem::path;
            auto file_path = mapping.get<std::string>("file");
            path file = std::filesystem::path(*file_path);

            { // Log
                time_t timestamp = time(nullptr);
                struct tm datetime = *localtime(&timestamp);
                char buf[24];
                strftime(buf, sizeof(buf), "%Y-%m-%d %T", &datetime);

                std::cout << "[" << buf << "]: access " << file.string() << '\n';
            }

            if (std::filesystem::exists(file)) {
                http_response response{};

                response.set_status_code(http_status_code::eOk);

                request.set_context<std::FILE *>(std::fopen(file.c_str(), "rb"));
                auto content_type = guess_content_type(file);
                response.set_header("Content-Type", content_type.value_or("application/octet-stream"));

                // This is only required for HTTP/1.1. HTTP/2 has a
                // built in way to terminate streams without hinting
                // at the length.
                if(request.version_ == http_version::HTTP_1_1) {
                    response.set_content_length(std::filesystem::file_size(file));
                }

                response.event(http_response::event::chunk, [&](http_response &response) {
                    std::unique_ptr<std::byte[]> buffer = std::make_unique_for_overwrite<std::byte[]>(16384);

                    auto &fstream = request.context<std::FILE *>().value().get();
                    auto  num = std::fread((void *)buffer.get(), 1, 16384, fstream);

                    rite::buffer buf(std::move(buffer), num, num < 16384);
                    if (num < 16384) {
                        std::fclose(fstream);
                    }
                    response.stream(std::move(buf));
                });

                return response;
            } else {
                // Use the generic 404 handler
                return lyr->not_found(request);
            }
        },
        // Handler & response sending spawns in a separate thread.
        .asynchronous = true
    };
    // clang-format on

    lyr->add_endpoint(image);

    // clang-format off
    rite::server<http>::config http_config = rite::server<http>::config();
    http_config
        .behaviour(lyr)
        .ip(INADDR_ANY)
        .port(2002)
        .max_connections(100);

    rite::server<https>::config https_config = rite::server<https>::config();
    https_config
        .private_key_file("key.pem")
        .certificate_file("cert.pem")
        .behaviour(lyr)
        .ip(INADDR_ANY)
        .port(2003)
        .max_connections(100);
    // clang-format on

    rite::server<http>  http_server(http_config);
    rite::server<https> https_server(https_config);

    runtime->attach<rite::server<http>>(http_server);
    runtime->attach<rite::server<https>>(https_server);
    runtime->start();
    return 0;
}
