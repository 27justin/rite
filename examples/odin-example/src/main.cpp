#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>

#include <arpa/inet.h>
#include <regex>
#include <sys/socket.h>
#include <thread>


#include <protocols/http.hpp>
#include <protocols/https.hpp>
#include <runtime.hpp>
#include <server.hpp>

int
main() {
    srand(std::chrono::steady_clock::now().time_since_epoch().count());

    kana::runtime *runtime = new kana::runtime();
    runtime->worker_threads(8);

    kana::server<https>::config https_config = kana::server<https>::config();
    https_config.private_key_file("private.pem").certificate_file("cert.crt").ip(INADDR_ANY).port(2003).max_connections(100);

    // kana::server<void>::config http_config{};
    // http_config.port(2002)
    //   .ip(INADDR_ANY)
    //     .max_connections(10);


    // kana::server<http> http_server = kana::server<http>( http_config );
    kana::server<https> https_server = kana::server<https>(https_config);

    // runtime->attach<kana::server<http>>(http_server);
    runtime->attach<kana::server<https>>(https_server);
    runtime->start();
    return 0;
}
