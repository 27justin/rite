#pragma once

#include "hpack.hpp"
#include <connection.hpp>
#include <protocols/h2.hpp>
#include <tls.hpp>

#include <http/request.hpp>

namespace h2 {
struct parameters {
    struct {
        parser<h2::hpack>     rx;
        serializer<h2::hpack> tx;
    } hpack;
};

struct stream {
    enum stream_state { idle, open, reserved, half_closed, closed };
    stream_state           state;
    h2::stream_id          stream_id;
    h2::hpack::headers     headers;
    std::vector<std::byte> data;
};
}

template<>
class connection<h2::protocol> : public connection<tls> {
    private:
    http_request finish_stream(h2::stream &stream);

    public:
    enum connection_state {
        CLIENT_PREFACE,
        WAIT_CLIENT_SETTINGS,
        // WAIT_CLIENT_SETTINGS_ACK, // "optional"
        IDLE,
        OPEN,
        RSV_LOCAL,
        HC_REMOTE
    };

    connection_state         state_;
    std::optional<h2::frame> unfinished_frame_;

    public:
    std::unique_ptr<h2::parameters> parameters_;
    std::map<uint32_t, h2::stream>  streams_;
    http_request request;

    connection(connection<tls> &&channel)
      : connection<tls>(std::move(channel))
      , state_(CLIENT_PREFACE)
      , parameters_(std::make_unique<h2::parameters>()) {
        keep_alive_ = minutes(5);

      };

      enum class result {
          eNewRequest,
          eSettings,
          eInvalid,
          eEof,
          eMore
      };

    result
    process(std::span<const std::byte>::iterator &pos, std::span<const std::byte>::iterator end);

    // Terminate the connection with a GOAWAY
    void terminate();

    std::expected<h2::frame, h2::frame_state>                                        read_frame(std::span<const std::byte>::iterator &position, std::span<const std::byte> data);
    std::expected<std::vector<std::pair<std::string, std::string>>, h2::frame_state> header_frame(const h2::frame &frame);

    using connection<tls>::write;
    int write(const h2::frame &frame);
};
