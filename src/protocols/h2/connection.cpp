#include <algorithm>
#include <cassert>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <protocols/h2.hpp>
#include <protocols/h2/connection.hpp>
#include <stdexcept>
#include <sys/socket.h>

#include <netdb.h>
#include <netinet/in.h>

// Frame specific implementations
#include "protocols/h2/headers.hpp"

constexpr std::string_view HTTP2_CLIENT_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
constexpr size_t           HTTP2_FRAME_SIZE = 9;

std::expected<h2::frame, h2::frame_state>
connection<h2::protocol>::read_frame(std::span<const std::byte>::iterator &position, std::span<const std::byte> data) {
    // There is no frame that hasn't been fully parsed yet,
    // thus we can read the frame header.
    h2::frame frame_{};
    if (unfinished_frame_.has_value()) {
        // Continue parsing a frame that wasn't complete in the last packet received
        frame_ = std::move(*unfinished_frame_);
    } else {
        if (data.size() - std::distance(data.begin(), position) < HTTP2_FRAME_SIZE) {
            return std::unexpected(h2::frame_state::eInvalid);
        }
        if (frame_.unpack(data.subspan(std::distance(data.begin(), position), HTTP2_FRAME_SIZE)) != true) {
            return std::unexpected(h2::frame_state::eInvalid);
        }

        if (frame_.length > MAX_FRAME_SIZE) {
            return std::unexpected(h2::frame_state::eTooBig);
        }

        // Read data
        frame_.data.reserve(frame_.length);

        // Advance the iterator forwards
        position += HTTP2_FRAME_SIZE;
    }

    // Continue by reading data from the current position

    // Start accumulating remaining data we have available.
    size_t remaining = data.size() - std::distance(data.begin(), position);

    std::span binary(position, std::min(remaining, frame_.length - frame_.data.size()));
    frame_.data.insert(frame_.data.end(), binary.data(), binary.data() + binary.size());
    position += binary.size();

    if (frame_.data.size() < frame_.length) {
        // Wait for next wake-up to continue parsing
        unfinished_frame_.emplace(std::move(frame_));
        return std::unexpected(h2::frame_state::eInsufficientData);
    }

    // Frame is now finished
    unfinished_frame_.reset();
    return frame_;
}

connection<h2::protocol>::result
connection<h2::protocol>::process(std::span<const std::byte>::iterator &pos, std::span<const std::byte>::iterator end) {
    if (pos >= end)
        return result::eEof;

    auto guard_ = lock();

    std::span<const std::byte> remaining = std::span<const std::byte>(pos, end);
    switch (state_) {
        case CLIENT_PREFACE: {
            bool valid = false;
            if (remaining.size() >= HTTP2_CLIENT_PREFACE.size()) {
                auto preface = remaining.subspan(0, HTTP2_CLIENT_PREFACE.size());
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
            return result::eSettings;
        }
        case WAIT_CLIENT_SETTINGS: {
            std::expected<h2::frame, h2::frame_state> settings = read_frame(pos, remaining);

            // Wait for next packet
            if (settings == std::unexpected(h2::frame_state::eInsufficientData))
                return result::eMore;
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
                return result::eInvalid;
            }

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
            write(response);

            /*
              To avoid unnecessary latency, clients are permitted to send
              additional frames to the server immediately after sending the client
              connection preface, without waiting to receive the server connection
              preface.
            */
            state_ = IDLE;
            return result::eSettings;
        }
        case IDLE: {
            std::expected<h2::frame, h2::frame_state> frame = read_frame(pos, remaining);
            if (frame == std::unexpected(h2::frame_state::eInsufficientData)) {
                return result::eMore;
            } else if (!frame.has_value()) {
                terminate();
                std::exit(1);
                return result::eInvalid;
            }

            streams_[frame->stream_identifier].stream_id = frame->stream_identifier;
            switch (frame->type) {
                case h2::frame::type::SETTINGS: {
                    // Client likely acknowledged our settings.
                    // The HTTP specification says that the ACK should happen, but not in a specific order
                    // thus we handle it as though optional.
                    if ((frame->flags & h2::frame::characteristics<h2::frame::SETTINGS>::ACK) == 1) {
                    } else {
                        // We have to re-send ACK
                        h2::frame response{ .length = 0, .type = h2::frame::type::SETTINGS, .flags = h2::frame::characteristics<h2::frame::SETTINGS>::ACK, .stream_identifier = 0x0 };
                        write(response);
                    }
                    return result::eSettings;
                }
                case h2::frame::type::PING: {
                    // Send PING+ACK
                    h2::frame ack{ .length = 8, // PING mandates 8 bytes of opaque
                                                // data
                                   .type = h2::frame::type::PING,
                                   .flags = h2::frame::characteristics<h2::frame::type::PING>::ACK,
                                   .stream_identifier = frame->stream_identifier,
                                   .data = std::vector<std::byte>(8) };
                    write(ack);
                    return result::eSettings;
                }
                case h2::frame::type::WINDOW_UPDATE: {
                    // Flow control:
                    // TODO: We do not support flow control /yet/. I may add this in the future
                    return result::eSettings;
                }
                case h2::frame::type::CONTINUATION:
                    __attribute__((fallthrough));
                case h2::frame::type::HEADERS: {
                    // Automatically provisions the steam ID for us,
                    streams_[frame->stream_identifier].state = h2::stream::open;

                    auto result = parameters_->hpack.rx.parse(*frame);
                    switch (result) {
                        case h2::hpack::error::eUnknownHeader: {
                            terminate();
                            return result::eInvalid;
                        }
                        case h2::hpack::error::eInvalid: {
                            terminate();
                            return result::eInvalid;
                        }
                        case h2::hpack::error::eSizeUpdate: {
                            // This warrants an ACK to the client
                            write(h2::frame{ .length = 0, .type = h2::frame::SETTINGS, .flags = h2::frame::characteristics<h2::frame::SETTINGS>::ACK, .data = {} });
                            return result::eMore;
                        }
                        case h2::hpack::error::eMore: {
                            std::print("H2[process]: HPACK is missing data (no END_HEADERS bit set), waiting for more CONTINUATION or HEADERS frames.\n");
                            return result::eMore;
                        }
                        case h2::hpack::error::eDone: {
                            // Save the headers for later (until after request body, or now if END_STREAM is set.)
                            auto &headers = streams_[frame->stream_identifier].headers;
                            headers = std::move(parameters_->hpack.rx.result());

                            if ((frame->flags & h2::frame::characteristics<h2::frame::HEADERS>::END_STREAM) == 0) {
                                // Remote will send a request body. We'll have to wait for that.
                                std::print("H2[handle]: Missing END_STREAM, waiting for data...\n");
                                return result::eMore;
                            }
                            // Otherwise transition stream to
                            // half-closed (remote won't send any more
                            // data.)
                            streams_[frame->stream_identifier].state = h2::stream::half_closed;
                            // Consumes the actual headers to produce a request.
                            request = finish_stream(streams_[frame->stream_identifier]);
                            return result::eNewRequest;
                        }
                    }
                    break;
                }
                case h2::frame::type::DATA: {
                    // Requires an active stream that has headers.
                    if (!streams_.contains(frame->stream_identifier)) {
                        std::print("Terminating HTTP/2 connection. Client sent data on non-existant stream.\n");
                        terminate();
                        return result::eInvalid;
                    }

                    auto &stream = streams_[frame->stream_identifier];
                    stream.data.insert(stream.data.end(), frame->data.begin(), frame->data.end());

                    if ((frame->flags & h2::frame::characteristics<h2::frame::DATA>::END_STREAM) != 0) {
                        std::print("Stream {} was terminated with data\n", frame->stream_identifier);
                        // Handle the request, we parsed its body.
                        request = finish_stream(streams_[frame->stream_identifier]);
                        return result::eNewRequest;
                    }
                    return result::eMore;
                }
                case h2::frame::type::RST_STREAM: {
                    streams_[frame->stream_identifier].state = h2::stream::closed;
                    return result::eMore;
                }
                default: {
                    std::print("H2[process]: Received unsupported frame during idle {}\n", (uint8_t)frame->type);
                    return result::eMore;
                }
            }
            break;
        }
        default: {
            std::print("Hit unexpected branch on H2 state machine: {}\n", static_cast<int>(state_));
            std::exit(1);
        }
    }
    return result::eMore;
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

#include <cctype>

std::string uri_decode(const std::string &encoded) {
    std::string decoded;
    decoded.reserve(encoded.size()); // Reserve space to avoid multiple allocations

    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%') {
            // Check if there are enough characters for a percent-encoded sequence
            if (i + 2 < encoded.size() && std::isxdigit(encoded[i + 1]) && std::isxdigit(encoded[i + 2])) {
                // Convert the hex value to a character
                std::string hex = encoded.substr(i + 1, 2);
                char decodedChar = static_cast<char>(std::stoi(hex, nullptr, 16));
                decoded.push_back(decodedChar);
                i += 2; // Skip the next two characters
            } else {
                // If the percent encoding is malformed, just add the '%' character
                decoded.push_back(encoded[i]);
            }
        } else if (encoded[i] == '+') {
            // Convert '+' to space
            decoded.push_back(' ');
        } else {
            // Regular character, just add it
            decoded.push_back(encoded[i]);
        }
    }

    return decoded;
}


http_request
connection<h2::protocol>::finish_stream(h2::stream &stream) {
    auto get_header = [&stream](std::string key) {
        return std::find_if(stream.headers.cbegin(), stream.headers.cend(), [&key](const h2::hpack::header &h) { return h.key == key; });
    };

    auto path = get_header(":path");
    auto method = get_header(":method");

    if(path == stream.headers.cend() || method == stream.headers.cend()) {
        std::print("Stream {} is missing headers but sent END_STREAM, can't process.\n", stream.stream_id);
        std::print("Headers: {}.\n", stream.headers.size());

        for(auto &h : stream.headers) {
            std::print("    {}: {}\n", h.key, h.value);
        }

        http_request rval{};
        rval.path_ = "/error";
        rval.method_ = http_method::GET;
        rval.version_ = http_version::HTTP_2_0;
        rval.set_context<h2::stream_id>(h2::stream_id(stream.stream_id));
        rval.client_ = this;
        return rval;
    }

    http_request rval{};
    rval.path_ = (*path).value;

    // Set the method
    auto method_str = (*method).value;
    if (method_str == "GET") {
        rval.method_ = http_method::GET;
    } else if (method_str == "HEAD") {
        rval.method_ = http_method::HEAD;
    } else if (method_str == "POST") {
        rval.method_ = http_method::POST;
    } else if (method_str == "PUT") {
        rval.method_ = http_method::PUT;
    } else if (method_str == "DELETE") {
        rval.method_ = http_method::DELETE;
    } else if (method_str == "CONNECT") {
        rval.method_ = http_method::CONNECT;
    } else if (method_str == "OPTIONS") {
        rval.method_ = http_method::OPTIONS;
    } else if (method_str == "TRACE") {
        rval.method_ = http_method::TRACE;
    } else if (method_str == "PATCH") {
        rval.method_ = http_method::PATCH;
    }

    for (auto const &header : stream.headers) {
        // TODO: Headers are not unique.
        rval.headers_[header.key] = header.value;
    }

    rval.version_ = http_version::HTTP_2_0;

    // Decode URI components
    // TODO: Streamline this in a better way, i.e. a setter.
    rval.path_ = uri_decode(rval.path_);

    if (rval.path_.find('?') != std::string::npos) {
        query_parameters query = parser<query_parameters>{}.parse(rval.path_).value();
        rval.query_ = query;
        // Remove the query parameters
        rval.path_.erase(rval.path_.find('?'));
    }

    rval.set_context<h2::stream_id>(h2::stream_id(stream.stream_id));
    rval.client_ = this;
    rval.body_ = std::move(stream.data);

    return rval;
}
