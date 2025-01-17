#pragma once

#include "http/parser.hpp"
#include "http/serializer.hpp"
#include "huffman.hpp"

#include <protocols/h2.hpp>

#include <deque>
#include <expected>
#include <unordered_set>

constexpr size_t DEFAULT_HPACK_TABLE_SIZE = 256;

/* HPACK Implementation */

namespace h2 {
struct hpack {
    struct header {
        std::string key, value;
    };
    enum class error { eUnknownHeader, eSizeUpdate, eDone, eMore };

    /*
      The dynamic table consists of a list of header fields maintained in
      first-in, first-out order.  The first and newest entry in a dynamic
      table is at the lowest index, and the oldest entry of a dynamic table
      is at the highest index.
    */
    static std::unordered_map<ssize_t, h2::hpack::header> STATIC_HEADER_TABLE;
    using dynamic_header_map = std::deque<header>;
    using headers = std::vector<header>;
    static huffman &decoder();
};

template<size_t N> // N = Prefix
struct variable_integer {
    public:
    // Encode an integer
    static std::vector<std::byte> encode(uint32_t value) {
        if (value < (1u << N) - 1) {
            // Case 1: Value fits in the N-bit prefix
            std::byte prefix = static_cast<std::byte>(value);
            return { prefix };
        } else {
            // Case 2: Value does not fit in the N-bit prefix
            std::byte              prefix = static_cast<std::byte>((1u << N) - 1);
            std::vector<std::byte> result = { prefix };

            value -= (1u << N) - 1; // Adjust value

            do {
                uint32_t quotient = value / 128;  // 2^7
                uint32_t remainder = value % 128; // 2^7

                if (quotient > 0) {
                    result.push_back(static_cast<std::byte>(0x80 | remainder)); // Set the continuation bit
                } else {
                    result.push_back(static_cast<std::byte>(remainder)); // No continuation bit
                }
                value = quotient; // Update value for next iteration
            } while (value > 0);
            return result;
        }
    }

    // Decode a byte stream back to an integer
    /// \param bytes - Stores the length of the decoded sequence
    static uint32_t decode(std::span<const std::byte> data, ssize_t &bytes) {
        if (data.empty()) {
            throw std::invalid_argument("Data cannot be empty");
        }

        bytes = 1;
        uint8_t prefix = static_cast<uint8_t>(data[0]);
        // Trim out the first (8 - N) bits on the prefix, these
        // shall be used for information.
        prefix &= (0b11111111 >> (8 - N));
        if (prefix < (1u << N) - 1) {
            // Case 1: Value fits in the N-bit prefix
            return static_cast<uint32_t>(prefix);
        } else {
            // Case 2: Decode the variable-length integer
            uint32_t value = (1u << N) - 1; // Start with the maximum value for the prefix
            uint32_t shift = 0;

            for (size_t i = 1; i < data.size(); ++i, ++bytes) {
                uint8_t byte = static_cast<uint8_t>(data[i]);
                value += (byte & 0x7F) << shift; // Add the 7 bits
                shift += 7;                      // Move to the next 7 bits

                if ((byte & 0x80) == 0) {
                    // If the continuation bit is not set, we are done
                    break;
                }
            }

            return value;
        }
    }
};
}

template<>
struct parser<h2::hpack> {
    std::string parse_string(std::span<std::byte>::const_iterator &pos, std::span<std::byte> payload);

    public:
    h2::hpack::dynamic_header_map header_map_;
    h2::hpack::headers            decoded_;
    ssize_t                       max_table_size = DEFAULT_HPACK_TABLE_SIZE;

    parser();

    const std::string       &key_by_index(uint8_t index) const;
    const h2::hpack::header *header_by_index(uint32_t index);

    public:
    h2::hpack::error parse(const h2::frame &);

    // Throws an exception if a previous `parse` call did not return
    // h2::hpack::error::eDone.  Also moves away the headers, further
    // access will cause an exception to be raised.
    // TODO: Not implemented yet.
    h2::hpack::headers &&result();
};

template<>
struct serializer<h2::hpack> {
    h2::hpack::dynamic_header_map header_map_;

    // Empty payload, add headers using `serialize`
    // when finished, call `finish` and flush the
    // frame to the client.
    std::vector<std::byte> payload;

    public:
    serializer() = default;

    struct fully_indexed { ssize_t index; };
    struct key_indexed { ssize_t index; };
    struct literal {};

    std::variant < fully_indexed, key_indexed,
                   literal > search_index(const h2::hpack::header &h);

    // NOTE: This function is fairly stupid and does not particularly
    // make use of the header compression HPACK offers, ideally we'd
    // save some state about which headers are most often used and
    // tell our peer to index those, but instead we'll just send stupp
    // generally without indexing except for the static table.
    void serialize(std::span<const h2::hpack::header>);

    h2::frame
    finish(uint32_t);
};
