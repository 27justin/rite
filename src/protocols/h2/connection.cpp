#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <protocols/h2.hpp>
#include <protocols/h2/connection.hpp>
#include <stdexcept>
#include <string>
#include <sys/socket.h>

#include <iomanip>
#include <iostream>

// Frame specific implementations
#include "protocols/h2/headers.hpp"

constexpr std::string_view HTTP2_CLIENT_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
constexpr size_t           HTTP2_FRAME_SIZE = 9;

std::expected<h2::frame, h2::frame_state>
connection<h2::protocol>::read_frame(std::span<std::byte>::iterator &position, std::span<std::byte> data) {
    // There is no frame that hasn't been fully parsed yet,
    // thus we can read the frame header.
    h2::frame frame_{};
    if (unfinished_frame_.has_value()) {
        // Continue parsing a frame that wasn't complete in the last packet received
        frame_ = std::move(*unfinished_frame_);
        // std::print("H2[read_frame]: Continue parsing unfinished frame\n");
    } else {
        if (data.size() - std::distance(data.begin(), position) < HTTP2_FRAME_SIZE) {
            // std::print("H2[read_frame]: Remaining data is too little to form a HTTP2 frame ({}b)\n", (data.end() - position));
            return std::unexpected(h2::frame_state::eInvalid);
        }
        if (frame_.unpack(data.subspan(std::distance(data.begin(), position), HTTP2_FRAME_SIZE)) != true) {
            // std::print("H2[read_frame]: Failed to unpack frame\n");
            return std::unexpected(h2::frame_state::eInvalid);
        } else {
            // std::print("H2[read_frame|debug]: Unpacked frame, ty:{:2x}, sz:{}, id:{}, flags:{}\n", (uint8_t)frame_.type, frame_.length, frame_.stream_identifier, frame_.flags);
        }

        if (frame_.length > MAX_FRAME_SIZE) {
            // std::print("H2[read_frame]: Frame length exceeds MAX_FRAME_SIZE: {} KiB / (max) {} KiB\n", frame_.length / 1024, MAX_FRAME_SIZE / 1024);
            return std::unexpected(h2::frame_state::eTooBig);
        }
        // std::print("H2[read_frame]: Reserving heap space of {} b\n", frame_.length);
        // // Read data
        // frame_.data.reserve(frame_.length);

        // Advance the iterator forwards
        position += HTTP2_FRAME_SIZE;
    }

    // Continue by reading data from the current position

    // Start accumulating remaining data we have available.
    size_t remaining = data.size() - std::distance(data.begin(), position);
    // std::print("H2[read_frame]: Have remaining data: {}\n", remaining);

    std::span binary(position, std::min(remaining, frame_.length - frame_.data.size()));
    frame_.data.insert(frame_.data.end(), binary.data(), binary.data() + binary.size());
    position += binary.size();

    if (frame_.data.size() < frame_.length) {
        // Wait for next wake-up to continue parsing
        // std::print("H2[read_frame]: Insufficient data for length: {}, have {}, waiting for next wakeup.\n", frame_.length, frame_.data.size());
        unfinished_frame_.emplace(std::move(frame_));
        return std::unexpected(h2::frame_state::eInsufficientData);
    }
    // std::print("H2[read_frame]: Finished frame ty:{}, sz:{} ({})\n", (uint8_t)frame_.type, frame_.length, frame_.data.size());

    // Frame is now finished
    unfinished_frame_.reset();
    return frame_;
}

void
connection<h2::protocol>::process(std::span<std::byte> span) {
    if (!lock_.try_lock()) {
        std::print("H2[process|error]: Refusing to read event, socket is locked\n");
        return;
    }

    auto pos = span.begin();
start:
    switch (state_) {
        case CLIENT_PREFACE: {
            bool valid = false;
            if (span.size() >= HTTP2_CLIENT_PREFACE.size()) {
                auto preface = span.subspan(0, HTTP2_CLIENT_PREFACE.size());
                valid = std::equal(preface.cbegin(), preface.cend(), std::span<const std::byte>((const std::byte *)HTTP2_CLIENT_PREFACE.data(), HTTP2_CLIENT_PREFACE.size()).cbegin());
            }
            if (!valid) {
                // TODO: Use custom exceptions
                throw std::runtime_error("Invalid client preface");
            }
            pos += HTTP2_CLIENT_PREFACE.size();

            // This sequence MUST be followed by a
            // SETTINGS frame (Section 6.5), which MAY be empty.
            //
            // Thus; read the SETTINGS frame (if data is remainig)
            state_ = WAIT_CLIENT_SETTINGS;
            __attribute__((fallthrough));
        }
        case WAIT_CLIENT_SETTINGS: {
            std::expected<h2::frame, h2::frame_state> settings = read_frame(pos, span);

            // Wait for next packet
            if (settings == std::unexpected(h2::frame_state::eInsufficientData))
                goto out;
            else if (!settings.has_value()) {
                throw std::runtime_error("Expected client settings, but got stream error");
            }

            if (settings->type != h2::frame::SETTINGS) {
                throw std::runtime_error("Expected client settings, but got " + std::to_string(static_cast<uint8_t>(settings->type)));
            }

            /*
              A SETTINGS frame with a length other than a multiple of 6 octets MUST
              be treated as a connection error (Section 5.4.1) of type
              FRAME_SIZE_ERROR.
            */
            if (settings->length % 6 != 0) {
                terminate();
            }

            // std::print("H2[WAIT_CLIENT_SETTINGS]: Received frame: ty:{:2x}, sz:{}, id:{}, flags:{}\n",
            //            (uint8_t)settings->type,
            //            (uint32_t)settings->length,
            //            (uint32_t)settings->stream_identifier,
            //            (uint8_t)settings->flags);

            /*
              The server connection preface consists of a potentially empty
              SETTINGS frame (Section 6.5) that MUST be the first frame the server
              sends in the HTTP/2 connection.

              The SETTINGS frames received from a peer as part of the connection
              preface MUST be acknowledged (see Section 6.5.3) after sending the
              connection preface.
             */

            // Send our preface (our settings)
            h2::frame response{ .length = 0, .type = h2::frame::type::SETTINGS, .flags = 0, .stream_identifier = 0x0 };
            std::print("    Sending our own settings to client\n");
            write(response);

            /*
              To avoid unnecessary latency, clients are permitted to send
              additional frames to the server immediately after sending the client
              connection preface, without waiting to receive the server connection
              preface.
            */
            state_ = IDLE;
            __attribute__((fallthrough));
        }
        case IDLE: {
            std::expected<h2::frame, h2::frame_state> frame = read_frame(pos, span);
            if (frame == std::unexpected(h2::frame_state::eInsufficientData))
                goto out;
            else if (!frame.has_value()) {
                terminate();
                goto out;
            }

            std::print("H2[process]: Received data in idle state\n");
            switch (frame->type) {
                case h2::frame::type::SETTINGS: {
                    // Client likely acknowledged our settings.
                    // The HTTP specification says that the ACK should happen, but not in a specific order
                    // thus we handle it as though optional.
                    if ((frame->flags & h2::frame::characteristics<h2::frame::SETTINGS>::ACK) == 1) {
                        std::print("H2[process]: Client acknowledged settings\n");
                    }
                    break;
                }
                case h2::frame::type::HEADERS: {
                    if (frame->flags & h2::frame::characteristics<h2::frame::HEADERS>::END_HEADERS) {

                        auto result = parameters_->hpack.rx.parse(*frame);
                        switch (result) {
                        case h2::hpack::error::eUnknownHeader: {
                            // TODO: throw decoding error and (probably) terminate the connection
                            break;
                        }
                        case h2::hpack::error::eSizeUpdate: {
                            // This warrants an ACK to the client
                            write(h2::frame{ .length = 0, .type = h2::frame::SETTINGS, .flags = h2::frame::characteristics<h2::frame::SETTINGS>::ACK, .data = {} });
                            __attribute__((fallthrough));
                        }
                        case h2::hpack::error::eMore: {
                            std::print("H2[process]: HPACK is missing data (no END_HEADERS bit set), waiting for more CONTINUATION or HEADERS frames.\n");
                            break;
                        }
                        case h2::hpack::error::eDone: {
                            // Print out the headers
                            auto headers = parameters_->hpack.rx.result();
                            // for (auto const &[k, v] : headers) {
                                // std::print("{}: {}\n", k, v);
                            // }

                            // Generate a dummy response
                            std::vector<h2::hpack::header> response_headers = {
                                h2::hpack::header{ .key = ":status", .value = "302" },
                                h2::hpack::header{"cache-control", "private"},
                                h2::hpack::header{ .key = "content-type", .value = "text/html; charset=UTF-8" },
                                h2::hpack::header{ .key = "server", .value = "WIP-CPP/0.1.1" },
                                h2::hpack::header{"content-length", "12"}
                            };
                            parameters_->hpack.tx.serialize(response_headers);
                            h2::frame response = parameters_->hpack.tx.finish(frame->stream_identifier);
                            write(response);

                            h2::frame dummy = {
                                .length = 12,
                                .flags = h2::frame::characteristics<h2::frame::DATA>::END_STREAM,
                                .stream_identifier = frame->stream_identifier,
                                .data = {}
                            };
                            const char DUMMY_RESPONSE[] = "Hello, world!";
                            dummy.data.insert(dummy.data.begin(), (std::byte *) DUMMY_RESPONSE, (std::byte *) DUMMY_RESPONSE + sizeof(DUMMY_RESPONSE));
                            write(dummy);

                            std::print("Sent out frame with stream ID 3\n");
                            break;
                        }
                        }
                    } else {
                        std::print("ERROR: Received HEADERS frame that is not enclosed within itself, END_HEADERS flag was not set. This is not yet supported.\n");
                        std::exit(1);
                    }
                    break;
                }
                case h2::frame::type::DATA: {
                    __attribute__((fallthrough));
                }
                default: {
                    std::print("H2[process]: Received unsupported frame during idle {}\n", (uint8_t)frame->type);
                }
            }
            break;
        }
        default: {
            std::print("Hit unexpected branch on H2 state machine: {}\n", static_cast<int>(state_));
            std::exit(1);
        }
    }

    if (pos != span.end()) {
        std::print("H2[process]: Going to start, data is not finished.\n");
        goto start;
    }
out:
    lock_.unlock();
}

int
connection<h2::protocol>::write(const h2::frame &frame) {
    std::array<std::byte, HTTP2_FRAME_SIZE> data_;
    frame.pack(data_);

    int total = write(data_, MSG_MORE);
    total += write(frame.data, 0);
    return total;
}

void
connection<h2::protocol>::terminate() {
    /*
      Clients and servers MUST treat an invalid connection preface as a
      connection error (Section 5.4.1) of type PROTOCOL_ERROR.  A GOAWAY
      frame (Section 6.8) MAY be omitted in this case, since an invalid
      preface indicates that the peer is not using HTTP/2.
    */
    close();
}
