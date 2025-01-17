#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>

#include <arpa/inet.h>
#include <regex>
#include <thread>

// struct demo_controller : public kana::controller {
//     void setup(kana::controller_config &config) override {
//         // clang-format off
//         config.add_endpoint(kana::endpoint{
//               .method = GET | POST | HEAD,
//               .path = "/load",
//               .handler = [](http_request &request) -> http_response {
//                   // auto random_timeout = 100 + (rand() % 250);
//                   // std::this_thread::sleep_for(std::chrono::milliseconds(random_timeout));
//                   http_response response(http_status_code::eOk, "Hello :')");
//                   response.set_header("Connection", "keep-alive");
//                   response.set_header("Keep-Alive", "timeout=5");
//                   return response;
//               },
//               .asynchronous = true
//         });
//         config.add_endpoint(kana::endpoint{
//               .method = GET | POST | HEAD,
//               .path = "/",
//               .handler = [](http_request &request) -> http_response {
//                   http_response response(http_status_code::eOk, "Hello :')");
//                   response.set_header("Connection", "keep-alive");
//                   response.set_header("Keep-Alive", "timeout=5");
//                   return response;
//               },
//               .asynchronous = false
//         });

//         // clang-format on
//     }
// };

#include <protocols/http.hpp>
#include <protocols/https.hpp>
#include <runtime.hpp>
#include <server.hpp>

// #include <kana.hpp>
// #include <kana/extensions/odin.hpp>

int
main() {
    srand(std::chrono::steady_clock::now().time_since_epoch().count());

    kana::runtime *runtime = new kana::runtime();
    runtime->worker_threads(4);

    kana::server<https>::config https_config = kana::server<https>::config();
    https_config.private_key_file("private.pem").certificate_file("cert.crt").ip(INADDR_ANY).port(2003).max_connections(100);

    kana::server<void>::config http_config{};
    http_config.port(2002)
      .ip(INADDR_ANY)
        .max_connections(10);

    runtime->attach<kana::server<http>>(kana::server<http>( http_config ));
    runtime->attach<kana::server<https>>(kana::server<https>(https_config));
    runtime->start();
    // kana::server server{};
    // server.register_controller<demo_controller>();

    // // clang-format off
    // server.worker_threads(10)
    //     .bind({ INADDR_ANY, 2002, kana::http_config {} })
    //     .bind({ INADDR_ANY, 2003, kana::https_config {
    //                 .private_key_file = "private.pem",
    //                 .certificate_file = "cert.crt"
    //             } })
    //       .load_extension<kana::extensions::odin>(kana::extensions::odin_config {
    //           .admin_path = "/__server"
    //       });
    // server.start();
    return 0;
}
