#pragma once

#include <protocols/h2.hpp>
#include <connection.hpp>
#include <tls.hpp>
#include "hpack.hpp"

namespace h2 {
    struct parameters {
        struct {
            parser<h2::hpack> rx;
            serializer<h2::hpack> tx;
        } hpack;
    };
}


template<>
class connection<h2::protocol> : public connection<tls> {
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

    // h2::compression compress_;
    // 31 bits are used, when wrapping around we should return to 2
    // std::atomic<int32_t> next_stream_identifier = 2;

    connection_state                state_;
    std::optional<h2::frame>        unfinished_frame_;
    std::unique_ptr<h2::parameters> parameters_;
    public:
    connection(connection<tls> &&channel)
      : connection<tls>(std::move(channel))
      , state_(CLIENT_PREFACE)
      , parameters_(std::make_unique<h2::parameters>()){

      };

    void process(std::span<std::byte>);

    // Terminate the connection with a GOAWAY
    void terminate();

    std::expected<h2::frame, h2::frame_state> read_frame(std::span<std::byte>::iterator &position, std::span<std::byte> data);
    std::expected<std::vector<std::pair<std::string, std::string>>, h2::frame_state> header_frame(const h2::frame &frame);

    using connection<tls>::write;
    int write(const h2::frame &frame);

    void     push();
};
