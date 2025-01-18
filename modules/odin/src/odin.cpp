#include <arpa/inet.h>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <print>
#include <rite/extensions/odin.hpp>
#include <sys/socket.h>
#include <thread>

#include "rite/extensions/odin/resources.hpp"

using namespace std::chrono;
using namespace rite::http;

namespace rite::extensions {

odin::odin(odin_config config)
  : config_(config)
  , timings_(std::make_unique<odin_timing[]>(32768)) {

    // Set up RPS thread
    std::thread rps([this]() {
        while (true) {
            std::this_thread::sleep_for(seconds(1));
            calculate_rps();
        }
    });
    rps.detach();
}

void
odin::on_hook(rite::http::layer &server) {
    // server.event(rite::http::layer::event::on_request, [this](http_request &r) { on_request(r); });
    // server.event(rite::http::layer::event::pre_send, std::bind(&odin::pre_send, this, std::placeholders::_1, std::placeholders::_2));
    // server.event(rite::http::layer::event::post_send, std::bind(&odin::post_send, this, std::placeholders::_1, std::placeholders::_2));

    server.add_endpoint(
      rite::http::endpoint{ .method = GET, .path = path("/__server/?$"), .handler = std::bind(&odin::index, this, std::placeholders::_1, std::placeholders::_2), .asynchronous = false });

    server.add_endpoint(rite::http::endpoint{ .method = GET, .path = path("/__server/css"), .handler = std::bind(&odin::css, this, std::placeholders::_1, std::placeholders::_2) });
    server.add_endpoint(rite::http::endpoint{
      .method = GET, .path = path("/__server/\\!component/card.*"), .handler = std::bind(&odin::card, this, std::placeholders::_1, std::placeholders::_2), .asynchronous = false });
    server.add_endpoint(rite::http::endpoint{
      .method = GET, .path = path("/__server/\\!component/request-list.*"), .handler = std::bind(&odin::request_list, this, std::placeholders::_1, std::placeholders::_2), .asynchronous = false });
}

void
odin::on_request(http_request &request) {
    // Record the request time
    if (request.path().starts_with(config_.admin_path)) {
        request.set_context<odin_admin_user>(odin_admin_user{});
        return;
    }
    rps_ += 1.0;
    request.set_context<odin_timing>(odin_timing{ .received = steady_clock::now() });
}

void
odin::pre_send(http_request &request, http_response &response) {
    if (request.context<odin_admin_user>().has_value())
        return;

    request.context<odin_timing>()->get().processed = steady_clock::now();
    int code = static_cast<int>(response.status_code());
    if (code >= 200 && code < 300)
        status_code_.ok += 1;
    if (code >= 400 && code < 500)
        status_code_.client_error += 1;
    if (code >= 500 && code < 599)
        status_code_.server_error += 1;
}

void
odin::post_send(http_request &request, http_response &response) {
    if (request.context<odin_admin_user>().has_value())
        return;

    odin_timing &timing = request.context<odin_timing>()->get();
    {
        std::lock_guard<std::mutex> lock(ring_buffer_mtx_);
        ring_buffer_.push_front(odin_http_request{ .path = std::string(request.path()),
                                                   .query = request.query(),
                                                   .ip = request.client()->addr(),
                                                   .ip_len = request.client()->addr_length(),
                                                   .method = request.method(),
                                                   .request_body_len = request.body().size(), // TODO: Not available yet
                                                   .version = request.version_,               // TODO: Not available yet
                                                   // .protocol = ,        // TODO: Not available yet
                                                   .time = timing.received,
                                                   .processing_time = duration_cast<std::chrono::microseconds>(timing.processed - timing.received).count() });
        if (ring_buffer_.size() > 5'000) {
            ring_buffer_.pop_back();
        }
    }

    timing.flushed = steady_clock::now();

    std::lock_guard<std::mutex> lock(mtx);
    timings_[timing_idx_] = timing;
    timing_idx_ = (timing_idx_ + 1) % 32768;
    requests_served_ += 1;
}

void
odin::calculate_rps() {
    // Reset RPS for the next second
    rps_ = 0.0;
}

odin_percentiles
odin::calculate_percentiles() {
    static std::array<double, 32768> durations;

    std::lock_guard<std::mutex> lock(mtx);

    for (size_t i = 0; i < 32768; ++i) {
        auto &timing = timings_[i];
        auto  duration = std::chrono::duration_cast<std::chrono::milliseconds>(timing.flushed - timing.received).count();
        durations[i] = duration;
    }

    // Sort durations for percentile calculation
    std::sort(durations.begin(), durations.end());
    size_t n = durations.size();

    // Calculate percentiles
    odin_percentiles percentiles{};
    percentiles.p99 = durations[static_cast<size_t>(n * 0.99) - 1];
    percentiles.p90 = durations[static_cast<size_t>(n * 0.90) - 1];
    percentiles.p75 = durations[static_cast<size_t>(n * 0.75) - 1];
    percentiles.p50 = durations[static_cast<size_t>(n * 0.50) - 1];
    return percentiles;
}

std::string
body_(const std::string &inner) {
    return std::format(R"(
<!DOCTYPE html>
<html>
<head>
<link rel="stylesheet" href="./css" />
<script src="https://unpkg.com/htmx.org@2.0.4" integrity="sha384-HGfztofotfshcF7+8n44JQL2oJmowVChPTg48S+jvZoztPfvwD79OC/LTtG6dMp+" crossorigin="anonymous"></script>
</head>
<body class="content-padding">
{}
</body>
</html>
)",
                       inner);
}

http_response
odin::index(http_request &request, path::result) {
    std::string content;
    return http_response(http_status_code::eOk, body_(R"(
<h1>Admin Panel</h1>
<div class="flex justify-between">
<h2>Overview</h2>
<button class="flex center" hx-trigger="click" hx-get="/__server/!component/card?metric=RPS&metric=P90&metric=P75&metric=P50&metric=total_served&metric=2xx&metric=4xx&metric=5xx" hx-target="#bento" hx-swap="innerHTML">
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-refresh-ccw"><path d="M21 12a9 9 0 0 0-9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"/><path d="M3 3v5h5"/><path d="M3 12a9 9 0 0 0 9 9 9.75 9.75 0 0 0 6.74-2.74L21 16"/><path d="M16 16h5v5"/></svg>
</button>
</div>
<div class="grid gap-lg mb-lg" id="bento" style="--columns: repeat(4, 1fr) "
hx-trigger="revealed" hx-get="/__server/!component/card?metric=RPS&metric=P90&metric=P75&metric=P50&metric=total_served&metric=2xx&metric=4xx&metric=5xx" hx-swap="innerHTML">
    <div class="h-full w-full flex center"><span class="big">Loading...</span></div>
</div>
<h2>Requests</h2>
<div class="flex column gap-sm"
hx-trigger="revealed"
hx-get="/__server/!component/request-list?num=25"
hx-swap="innerHTML"
></div>

)"));
}

std::string
render(const std::string_view &templateStr, const std::unordered_map<std::string, std::string> &values) {
    std::string result = std::string(templateStr);
    for (const auto &pair : values) {
        std::string placeholder = "%" + pair.first;
        size_t      pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), pair.second);
            pos += pair.second.length(); // Move past the replaced value
        }
    }
    return result;
}

http_response
odin::css(http_request &request, path::result) {
    http_response response(http_status_code::eOk, std::string(DEFAULT_CSS));
    response.set_header("Content-Type", "text/css");
    return response;
}

std::string
serialize_odin_request(const odin_http_request &r) {
    constexpr std::string_view template_ = R"(
<div class="flex row gap justify-between -request">
<span class="-method" x-http-method="%method">%method</span>
<div class="flex column items-center">
<span class="-path">%path</span>
<span class="muted -ip">%ip</span>
</div>
<span>%size KiB</span>
<span>%request_time</span>
<span>%processing_time us</span>
</div>
)";
    char                       ip[INET6_ADDRSTRLEN] = {};
    char                       port[6] = {};

    getnameinfo((struct sockaddr *)&r.ip, sizeof(r.ip), ip, sizeof(ip), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);

    return render(template_,
                  { { "method", std::to_string(static_cast<int>(r.method)) },
                    { "path", r.path },
                    { "ip", std::format("{}:{}", ip, port) },
                    { "size", std::to_string(r.request_body_len / 1024) },
                    { "request_time", "" },
                    { "processing_time", std::to_string(r.processing_time) } });
}

http_response
odin::card(http_request &request, path::result) {
    // auto metrics = request.query().get<std::vector<std::string>>("metric");
    // if (!metrics.has_value() || (metrics.has_value() && metrics->size() < 1))
        // return http_response(http_status_code::eBadRequest, "");

//     constexpr std::string_view template_ = R"(
// <div class="card">
// <p class="tooltip inline">%name
// <span role="tooltip-text">%explanation</span></p>
// <p class="big">%value</p>
// </div>
// )";

    std::optional<odin_percentiles> percentile;
    std::string                     output_ = "";
    // for (const std::string &metric : metrics.value()) {
    //     if (metric == "P99") {
    //         if (!percentile)
    //             percentile = calculate_percentiles();
    //         output_ += render(
    //           template_, { { "name", "99th Percentile" }, { "value", std::format("{}ms", percentile->p99) }, { "explanation", "The sample size for percentiles is the most recent 32768 requests." } });
    //     } else if (metric == "P90") {
    //         if (!percentile)
    //             percentile = calculate_percentiles();
    //         output_ += render(
    //           template_, { { "name", "90th Percentile" }, { "value", std::format("{}ms", percentile->p90) }, { "explanation", "The sample size for percentiles is the most recent 32768 requests." } });
    //     } else if (metric == "P75") {
    //         if (!percentile)
    //             percentile = calculate_percentiles();
    //         output_ += render(
    //           template_, { { "name", "75th Percentile" }, { "value", std::format("{}ms", percentile->p75) }, { "explanation", "The sample size for percentiles is the most recent 32768 requests." } });
    //     } else if (metric == "P50") {
    //         if (!percentile)
    //             percentile = calculate_percentiles();
    //         output_ += render(
    //           template_, { { "name", "50th Percentile" }, { "value", std::format("{}ms", percentile->p50) }, { "explanation", "The sample size for percentiles is the most recent 32768 requests." } });
    //     } else if (metric == "RPS") {
    //         output_ += render(template_, { { "name", "RPS" }, { "value", std::format("{}", rps_.load()) }, { "explanation", "Requests per second" } });
    //     } else if (metric == "total_served") {
    //         output_ += render(template_, { { "name", "Total Requests" }, { "value", std::format("{}", requests_served_.load()) }, { "explanation", "Requests since server start" } });
    //     } else if (metric == "2xx") {
    //         output_ += render(template_, { { "name", "OK" }, { "value", std::format("{}", status_code_.ok.load()) }, { "explanation", "Responses sent with status code 200-299" } });
    //     } else if (metric == "4xx") {
    //         output_ +=
    //           render(template_, { { "name", "Client Error" }, { "value", std::format("{}", status_code_.client_error.load()) }, { "explanation", "Responses sent with status code 400-499" } });
    //     } else if (metric == "5xx") {
    //         output_ +=
    //           render(template_, { { "name", "Server Error" }, { "value", std::format("{}", status_code_.server_error.load()) }, { "explanation", "Responses sent with status code 500-599" } });
    //     }
    // }

    http_response response(http_status_code::eOk, std::string(output_));
    response.set_header("Content-Type", "text/html");
    return response;
}

http_response
odin::request_list(http_request &request, path::result) {
    // size_t num = request.query().get<size_t>("num").value_or(20);

    std::string body = "";
    // {
        // std::lock_guard<std::mutex> lock(ring_buffer_mtx_);
        // for (size_t i = 0; i < std::min(num, ring_buffer_.size()); ++i) {
            // body += serialize_odin_request(ring_buffer_[i]);
        // }
    // }

    http_response response(http_status_code::eOk, body);
    return response;
}

}
