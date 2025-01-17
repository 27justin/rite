#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>

#include <arpa/inet.h>
#include <filesystem>
#include <regex>
#include <sys/socket.h>
#include <thread>

#include <http/endpoint.hpp>
#include <http/parser.hpp>
#include <protocols/http.hpp>
#include <protocols/https.hpp>
#include <runtime.hpp>
#include <server.hpp>

#include <rite/extensions/odin.hpp>

#include <protocols/h2.hpp>

#include <iostream>

constexpr char BASE[] = ".";
constexpr char STATIC_IMAGES[] = "images";

int
main() {
    rite::runtime *runtime = new rite::runtime();
    runtime->worker_threads(8);

    rite::http::endpoint image = rite::http::endpoint{ .method = http_method::GET,
                                                       .path = rite::http::path("/image/{image_path:.+}"),
                                                       .handler = [](http_request &request, rite::http::path::result mapping) -> http_response {
                                                           using path = std::filesystem::path;
                                                           auto file_path = mapping.get<std::string>("image_path");
                                                           std::filesystem::path file = path(BASE) / path(STATIC_IMAGES) / *file_path;

                                                           if (std::filesystem::exists(file)) {
                                                               http_response response{};
                                                               response.set_status_code(http_status_code::eOk);

                                                               request.set_context<std::FILE *>(std::fopen(file.c_str(), "rb"));
                                                               response.set_header("Content-Type", "image/png");

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

    // clang-format off
    rite::http::endpoint index {
        .method = GET, .path = rite::http::path("/"), .handler = [](http_request &req, rite::http::path::result) {
            return http_response(http_status_code::eOk, "");
        }
    };
    // clang-format on

    std::shared_ptr<rite::http::layer> lyr = std::make_shared<rite::http::layer>();

    lyr->attach<rite::extensions::odin>(rite::extensions::odin_config{ .admin_path = "/__server/" });

    lyr->add_endpoint(image);

    // clang-format off
    rite::server<https>::config https_config = rite::server<https>::config();
    https_config
        .private_key_file("private.pem")
        .certificate_file("cert.crt")
        .behaviour(lyr)
        .ip(INADDR_ANY)
        .port(2003)
        .max_connections(100);
    // clang-format on

    // rite::server<http> http_server = rite::server<http>( http_config );
    rite::server<https> https_server = rite::server<https>(https_config);

    // runtime->attach<rite::server<http>>(http_server);
    runtime->attach<rite::server<https>>(https_server);
    runtime->start();
    return 0;
}
