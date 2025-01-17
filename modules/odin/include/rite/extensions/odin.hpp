// Copyright (c) 2025 Justin Andreas Lacoste <me@justin.cx>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <deque>
#include <future>
#include <sys/socket.h>

#include <http/behaviour.hpp>
#include <http/request.hpp>
#include <http/response.hpp>
#include <http/version.hpp>
#include <http/query_parameters.hpp>

// Admin UI
// TODO: Gosh, write better introductions...

#define ODIN_VERSION "0.0.1-dev"

namespace rite::extensions {
struct odin_config {
    std::string admin_path;
};

struct odin_percentiles {
    double p99; // 99th percentile
    double p90; // 90th percentile
    double p75;
    double p50;
};

// zero length struct to set into requests that are performed on our own controllers,
// to prevent them from cluttering the access log.
struct odin_admin_user {};

// // HTTP request for introspection, only saving fields that are of importance to us
struct odin_http_request {
    std::string                           path;
    query_parameters                      query;
    struct sockaddr_storage               ip;
    size_t                                ip_len;
    http_method                           method;
    size_t                                request_body_len;
    http_version                          version;
    // rite::protocol                        protocol;
    std::chrono::steady_clock::time_point time;
    long int                              processing_time; // us (microseconds)
};

class odin : public rite::http::extension {
    public:
    std::atomic<float> rps_ = 0.0;
    std::mutex         mtx;
    odin_config        config_;

    std::mutex                    ring_buffer_mtx_;
    std::deque<odin_http_request> ring_buffer_;

    // Per request timing, available through `context` on every
    // `http_request`
    struct odin_timing {
        std::chrono::steady_clock::time_point received;  // received
        std::chrono::steady_clock::time_point processed; // processed (pre_send)
        std::chrono::steady_clock::time_point flushed;   // finished (post_send)
    };

    std::unique_ptr<odin_timing[]> timings_;
    std::atomic<size_t>            timing_idx_ = 0;
    std::atomic<size_t>            requests_served_;

    struct {
        std::atomic<size_t> ok;
        std::atomic<size_t> client_error;
        std::atomic<size_t> server_error;
    } status_code_;

    odin(odin_config config);
    ~odin() {}

    // http_request events
    void on_request(http_request &request) override;
    void pre_send(http_request &request, http_response &response) override;
    void post_send(http_request &request, http_response &response) override;

    void on_hook(rite::http::layer &server) override;

    void             calculate_rps();
    odin_percentiles calculate_percentiles();


    http_response index(http_request &request, rite::http::path::result);
    http_response css(http_request &request, rite::http::path::result);
    http_response card(http_request &, rite::http::path::result);
    http_response request_list(http_request &, rite::http::path::result);
};

}
