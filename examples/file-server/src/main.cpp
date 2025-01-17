#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>

#include <arpa/inet.h>
#include <filesystem>
#include <iostream>
#include <regex>
#include <sys/socket.h>
#include <thread>

#include <http/behaviour.hpp>
#include <http/endpoint.hpp>
#include <http/parser.hpp>
#include <protocols/http.hpp>
#include <protocols/https.hpp>
#include <runtime.hpp>
#include <server.hpp>

constexpr char BASE[] = "/mnt/SSD-2/";
constexpr char STATIC_IMAGES[] = "Torrents";

int
main() {
    rite::runtime *runtime = new rite::runtime();
    runtime->worker_threads(8);

    rite::http::endpoint image = rite::http::endpoint{ .method = http_method::GET,
                                                       .path = rite::http::path("/{file:.*}"),
                                                       .handler = [](http_request &request, rite::http::path::result mapping) -> http_response {
                                                           using path = std::filesystem::path;
                                                           auto file_path = mapping.get<std::string>("file");
                                                           path file = std::filesystem::path(*file_path);

                                                           if (std::filesystem::exists(file)) {
                                                               http_response response{};
                                                               response.set_status_code(http_status_code::eOk);

                                                               request.set_context<std::FILE *>(std::fopen(file.c_str(), "rb"));
                                                               response.set_header("Content-Type", "text/plain");
                                                               response.set_content_length(std::filesystem::file_size(file));

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
                                                               std::cerr << "Tried to access file: " << file << std::endl;
                                                               return http_response(http_status_code::eNotFound, "File could not be found.");
                                                           }
                                                       },
                                                       .asynchronous = true };

    // A layer represents the default behaviour of the server.
    // The default `layer` that we expose is a very simple
    // path mapping controller suitable for CRUD applications.
    //
    // To cater to a large audience however; the https/http server
    // both do not really care for whatever the layer (`behaviour` on the server instance)
    // is, all it has to do is implement an `on_request(http_request) -> http_response`
    // function.
    std::shared_ptr<rite::http::layer> lyr = std::make_shared<rite::http::layer>();
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
        .private_key_file("private.pem")
        .certificate_file("cert.crt")
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
