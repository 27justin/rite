// HTTP/2
#pragma once
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

constexpr static size_t MAX_FRAME_SIZE = 1024 * 1024 * 4; // 4 MiB

namespace h2 {
enum class frame_state { eInsufficientData, eInvalid, eTooBig };

using stream_id = uint32_t;

struct stream {
    enum stream_state { idle, open, reserved, half_closed, closed };
    stream_state state;
    uint32_t     stream_id;
};

struct frame {
    enum type : uint8_t { DATA = 0x0, HEADERS = 0x1, PRIORITY = 0x2, RST_STREAM = 0x3, SETTINGS = 0x4, PUSH_PROMISE = 0x5, PING = 0x6, GOAWAY = 0x7, WINDOW_UPDATE = 0x8, CONTINUATION = 0x9 };

    template<h2::frame::type>
    struct characteristics;

    // only 24 lsb used
    uint32_t    length;
    frame::type type;
    uint8_t     flags;
    // only 31 lsb used
    uint32_t stream_identifier;

    std::vector<std::byte> data;

    // Function to pack the fields into a byte array
    bool pack(std::span<std::byte> buffer) const {
        if (buffer.size() < 9)
            return false; // Ensure buffer is large enough

        // Set the length to the first 3 bytes
        buffer[0] = (std::byte)((length >> 16) & 0xFF); // Most significant byte
        buffer[1] = (std::byte)((length >> 8) & 0xFF);  // Middle byte
        buffer[2] = (std::byte)(length & 0xFF);         // Least significant byte

        // Set the type, flags, reserved, and stream identifier
        buffer[3] = (std::byte)type;
        buffer[4] = (std::byte)flags;

        // Set the stream identifier
        buffer[5] = (std::byte)((stream_identifier >> 24) & 0xFF); // Most significant byte
        buffer[6] = (std::byte)((stream_identifier >> 16) & 0xFF); // Middle byte
        buffer[7] = (std::byte)((stream_identifier >> 8) & 0xFF);  // Least significant byte
        buffer[8] = (std::byte)(stream_identifier & 0xFF);         // Least significant byte
        return true;
    }

    // Function to unpack from a byte array
    bool unpack(const std::span<std::byte> buffer) {
        if (buffer.size() < 9)
            return false; // Ensure buffer is large enough

        // Read the length from the first 3 bytes (big-endian)
        length = 0;
        length = (static_cast<uint32_t>(buffer[0]) << 16) | (static_cast<uint32_t>(buffer[1]) << 8) | (static_cast<uint32_t>(buffer[2]));

        // Read the type and flags
        type = static_cast<enum type>(buffer[3]);
        flags = 0;
        flags = static_cast<uint8_t>(buffer[4]);

        // Read the stream identifier from the next 4 bytes
        stream_identifier = 0;
        stream_identifier = (static_cast<uint32_t>(buffer[5]) << 24) | (static_cast<uint32_t>(buffer[6]) << 16) | (static_cast<uint32_t>(buffer[7]) << 8) | (static_cast<uint32_t>(buffer[8]));
        return true;
    }
};

// frame type specific overloads to conveniently access wrapped
// data
template<enum frame::type Ty>
struct payload {};

struct protocol {};
};

template<>
struct h2::frame::characteristics<h2::frame::type::SETTINGS> {
    static constexpr uint8_t ACK = 0x1;
};

template<>
struct h2::frame::characteristics<h2::frame::type::DATA> {
    static constexpr uint8_t END_STREAM = 0x1;
    static constexpr uint8_t PADDED = 0x8;
};

template<>
struct h2::frame::characteristics<h2::frame::type::PING> {
    static constexpr uint8_t ACK = 0x1;
};
