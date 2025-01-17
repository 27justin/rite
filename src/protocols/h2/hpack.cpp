#include "protocols/h2/hpack.hpp"
#include "protocols/h2/headers.hpp"
#include <algorithm>
#include <bitset>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <stdexcept>

#include <iostream>
#include <utility>
#include <variant>

std::string
to_lower(std::string data) {
    std::transform(data.begin(), data.end(), data.begin(), [](unsigned char c) { return std::tolower(c); });
    return data;
}

h2::hpack::error
parser<h2::hpack>::parse(const h2::frame &frame) {
    h2::payload<h2::frame::HEADERS> headers(frame);
    auto                            payload = std::span<std::byte>(headers.data);
    auto                            pos = payload.cbegin();
    ssize_t                         len = 0;

    std::print("-------------------------\n");
    for (std::byte &byte : payload) {
        std::print("\\x{:02x}", static_cast<uint8_t>(byte));
    }
    std::print("\n");

    try {
        while (pos != payload.cend()) {
            // First check for category: Literal Header without Indexing
            uint8_t start = static_cast<uint8_t>(*pos);
            // auto begin = pos;

            if (start & 0x80) { // Indexed
                // std::print("{:02x} INDEXED\n", start);
                auto index = h2::variable_integer<7>::decode(std::span<const std::byte>(pos, payload.cend()), len);
                pos += len;
                auto header = header_by_index(index);
                // std::print("Emitting '{}' (indexed)\n", header->key);
                decoded_.push_back(*header);
                goto next;
            }

            if (start & 0x40) { // Literal indexed
                // std::print("{:02x} LITERAL\n", start);
                // 6-bit prefix index
                auto index = h2::variable_integer<6>::decode(std::span<const std::byte>(pos, payload.cend()), len);
                // std::print("vlen: {}\n", len);
                pos += len;

                std::string key, value;
                if (index == 0) {
                    key = parse_string(pos, payload);
                } else {
                    key = header_by_index(index)->key;
                }
                value = parse_string(pos, payload);

                // std::print("Indexed '{}'\n", key);
                decoded_.push_back(h2::hpack::header { key, value });
                header_map_.push_front(h2::hpack::header { key, value });
                goto next;
            }

            if (start & 0x20) { // Header table update
                // std::print("{:02x} TABLE UPDATE\n", start);
                // Dynamic Table Size Update
                auto size = h2::variable_integer<5>::decode(std::span<const std::byte>(pos, payload.cend()), len);
                pos += len;
                // std::print("HPACK[parse]: dynamic table update to: {}\n", size);

                if (size < header_map_.size()) {
                    while (header_map_.size() > size) {
                        // Remove oldest entry
                        header_map_.pop_back();
                    }
                } // TODO: I don't think that we have to handle the `else`
                // in our case.  but read up on RFC 7541 Section 4.2.
                // Maximum Table Size to be sure.
                // return h2::hpack::error::eSizeUpdate;
                goto next;
            }

            {
                // std::print("{:02x} NOTHING\n", start);
                // Not indexed
                // 4-bit prefix index
                auto index = h2::variable_integer<4>::decode(std::span<const std::byte>(pos, payload.cend()), len);
                pos += len;

                std::string key, value;
                if (index == 0) {
                    key = parse_string(pos, payload);
                } else {
                    key = header_by_index(index)->key;
                }
                // std::print("Emitting '{}'\n", key);
                value = parse_string(pos, payload);
                // header_map_.push_front(h2::hpack::header { key, value });
                decoded_.push_back(h2::hpack::header { key, value });
            }
          next:
            // auto sub = pos;
            // std::print("=> ");
            // for (; begin != sub; ++begin) {
                // std::print("\\x{:02x}", static_cast<uint8_t>(*begin));
            // }
            // std::print("\n----------------\n");

            continue;
        }

        // std::print("Current header indexing: \n");
        // for (size_t i = 0; i < header_map_.size(); ++i) {
        //     auto const &h = header_map_[i];
        //     std::print("[{}] {}: {}\n", i, h.key, h.value);
        // }
        // std::print("----------------------------\n");

        // std::print("Dynamic header size: {}\n", header_map_.size());
        return frame.flags & (h2::payload<h2::frame::HEADERS>::flags::END_HEADERS | h2::payload<h2::frame::HEADERS>::flags::END_STREAM) ? h2::hpack::error::eDone : h2::hpack::error::eMore;
    } catch (h2::hpack::error err) {
        return err;
    }
}

std::string
parser<h2::hpack>::parse_string(std::span<std::byte>::const_iterator &pos, std::span<std::byte> payload) {
    /*
      +---+---+-----------------------+
      | H |     Value Length (7+)     |
      +---+---------------------------+
      | Value String (Length octets)  |
      +-------------------------------+
    */
    bool    is_huffman = static_cast<uint8_t>(*pos) & 0b1000'0000;
    ssize_t len = 0;
    auto    length = h2::variable_integer<7>::decode(std::span<const std::byte>(pos, payload.cend()), len);
    // Skip the `Value Length`
    pos += len;


    // Read `length` bytes as the string starting from `pos`
    std::string raw((const char *)&(*pos), length);
    pos += length;
    if (is_huffman) {
        auto decoder = h2::hpack::decoder();
        return decoder.decode(raw);
    }
    return raw;
}

parser<h2::hpack>::parser() {}

const h2::hpack::header *
parser<h2::hpack>::header_by_index(uint32_t index) {
    if (index < h2::hpack::STATIC_HEADER_TABLE.size())
        return &h2::hpack::STATIC_HEADER_TABLE[index];

    /*
      Indices strictly greater than the length of the static table refer to
      elements in the dynamic table (see Section 2.3.2).  The length of the
      static table is subtracted to find the index into the dynamic table.
    */

    index -= h2::hpack::STATIC_HEADER_TABLE.size();
    index -= 1;
    std::print("Looking for index {} (#{})\n", index, header_map_.size());
    if (index < header_map_.size())
        return &header_map_[index];
    // Indices strictly greater than the sum of the lengths of both tables
    // MUST be treated as a decoding error.
    throw h2::hpack::error::eUnknownHeader;
}

h2::hpack::headers &&
parser<h2::hpack>::result() {
    return std::move(decoded_);
}

std::variant<serializer<h2::hpack>::fully_indexed, serializer<h2::hpack>::key_indexed, serializer<h2::hpack>::literal>
serializer<h2::hpack>::search_index(const h2::hpack::header &h) {
    // Look through our static table
    std::string key = to_lower(h.key);
    ssize_t best_index = -1;
    for (auto const &[idx, header] : h2::hpack::STATIC_HEADER_TABLE) {
        if (header.key == key && header.value == h.value){
            return fully_indexed{ idx };
        }else if (header.key == key && header.value != h.value){
            best_index = idx;
        }
    }
    if(best_index != -1)
        return key_indexed{ best_index };
    else
        return literal{};
}

// Serializer
void
serializer<h2::hpack>::serialize(std::span<const h2::hpack::header> list) {
    using fully_indexed = serializer<h2::hpack>::fully_indexed;
    using key_indexed = serializer<h2::hpack>::key_indexed;
    using literal = serializer<h2::hpack>::literal;

    for (auto const &header : list) {
        auto where = search_index(header);
        if (std::holds_alternative<fully_indexed>(where)) {
            // Emit 1 byte header
            fully_indexed &index = std::get<fully_indexed>(where);

            // Convert index to wire format
            auto wire = h2::variable_integer<7>::encode( index.index );
            wire[0] |= static_cast<std::byte>(0b1000'0000);
            payload.insert(payload.end(), wire.begin(), wire.end());
        }

        if (std::holds_alternative<key_indexed>(where)) {
            // Emit 1 byte header
            key_indexed &index = std::get<key_indexed>(where);

            // Convert index to wire format
            auto wire = h2::variable_integer<6>::encode(index.index);
            wire[0] = (wire[0] & static_cast<std::byte>(0b0011'1111)) | static_cast<std::byte>(0b0100'0000);
            payload.insert(payload.end(), wire.begin(), wire.end());

            // Use the encoder to encode the header value
            auto huffman = h2::hpack::decoder().encode(header.value); // Corrected to encoder

            // Insert value length
            wire = h2::variable_integer<7>::encode(huffman.size());

            // Ensure the size of wire is at least 1 before modifying
            if (!wire.empty()) {
                // Huffman encoding bitflag
                wire[0] |= static_cast<std::byte>(0b1000'0000);
            } else {
                // Handle the case where wire is empty if necessary
                std::cerr << "Error: wire is empty after encoding value length." << std::endl;
            }

            payload.insert(payload.end(), wire.begin(), wire.end());
            // Encode value
            payload.insert(payload.end(), huffman.begin(), huffman.end());
        }


        if (std::holds_alternative<literal>(where)) {
            // Emit 1 byte header
            std::print("Inserting literal header ({}: {})\n", header.key, header.value);
            std::vector<std::byte> wire = {static_cast<std::byte>(0b0000'0000)};
            payload.insert(payload.end(), wire.begin(), wire.end());

            // Encode key
            auto huffman = h2::hpack::decoder().encode(header.key);

            // Insert key length
            wire = h2::variable_integer<7>::encode( huffman.size() );
            // Huffman encoding bitflag
            wire[0] |= static_cast<std::byte>(0b1000'0000);
            payload.insert(payload.end(), wire.begin(), wire.end());

            // Encode value
            payload.insert(payload.end(), huffman.begin(), huffman.end());

            huffman = h2::hpack::decoder().encode(header.value);
            // Insert value length
            wire = h2::variable_integer<7>::encode( huffman.size() );
            // Huffman encoding bitflag
            wire[0] |= static_cast<std::byte>(0b1000'0000);
            payload.insert(payload.end(), wire.begin(), wire.end());

            // Encode value
            payload.insert(payload.end(), huffman.begin(), huffman.end());
        }
    }
}

h2::frame
serializer<h2::hpack>::finish(uint32_t stream_id) {
    // clang-format off
    return h2::frame {
        .length = static_cast<uint32_t>(payload.size()),
        .type = h2::frame::HEADERS,
        .flags = h2::payload<h2::frame::HEADERS>::flags::END_HEADERS, .stream_identifier = stream_id,
        .data = std::move(payload)
    };
    // clang-format on
}



// clang-format off
std::unordered_map<ssize_t, h2::hpack::header>
h2::hpack::STATIC_HEADER_TABLE = {
    {1,  h2::hpack::header{ ":authority", "" }},
    {2,  h2::hpack::header{ ":method", "GET" }},
    {3,  h2::hpack::header{ ":method", "POST" }},
    {4,  h2::hpack::header{ ":path", "/" }},
    {5,  h2::hpack::header{ ":path", "/index.html" }},
    {6,  h2::hpack::header{ ":scheme", "http" }},
    {7,  h2::hpack::header{ ":scheme", "https" }},
    {8,  h2::hpack::header{ ":status", "200" }},
    {9,  h2::hpack::header{ ":status", "204" }},
    {10, h2::hpack::header{ ":status", "206" }},
    {11, h2::hpack::header{ ":status", "304" }},
    {12, h2::hpack::header{ ":status", "400" }},
    {13, h2::hpack::header{ ":status", "404" }},
    {14, h2::hpack::header{ ":status", "500" }},
    {15, h2::hpack::header{ "accept-charset", "" }},
    {16, h2::hpack::header{ "accept-encoding", "gzip, deflate" }},
    {17, h2::hpack::header{ "accept-language", "" }},
    {18, h2::hpack::header{ "accept-ranges", "" }},
    {19, h2::hpack::header{ "accept", "" }},
    {20, h2::hpack::header{ "access-control-allow-origin", "" }},
    {21, h2::hpack::header{ "age", "" }},
    {22, h2::hpack::header{ "allow", "" }},
    {23, h2::hpack::header{ "authorization", "" }},
    {24, h2::hpack::header{ "cache-control", "" }},
    {25, h2::hpack::header{ "content-disposition", "" }},
    {26, h2::hpack::header{ "content-encoding", "" }},
    {27, h2::hpack::header{ "content-language", "" }},
    {28, h2::hpack::header{ "content-length", "" }},
    {29, h2::hpack::header{ "content-location", "" }},
    {30, h2::hpack::header{ "content-range", "" }},
    {31, h2::hpack::header{ "content-type", "" }},
    {32, h2::hpack::header{ "cookie", "" }},
    {33, h2::hpack::header{ "date", "" }},
    {34, h2::hpack::header{ "etag", "" }},
    {35, h2::hpack::header{ "expect", "" }},
    {36, h2::hpack::header{ "expires", "" }},
    {37, h2::hpack::header{ "from", "" }},
    {38, h2::hpack::header{ "host", "" }},
    {39, h2::hpack::header{ "if-match", "" }},
    {40, h2::hpack::header{ "if-modified-since", "" }},
    {41, h2::hpack::header{ "if-none-match", "" }},
    {42, h2::hpack::header{ "if-range", "" }},
    {43, h2::hpack::header{ "if-unmodified-since", "" }},
    {44, h2::hpack::header{ "last-modified", "" }},
    {45, h2::hpack::header{ "link", "" }},
    {46, h2::hpack::header{ "location", "" }},
    {47, h2::hpack::header{ "max-forwards", "" }},
    {48, h2::hpack::header{ "proxy-authenticate", "" }},
    {49, h2::hpack::header{ "proxy-authorization", "" }},
    {50, h2::hpack::header{ "range", "" }},
    {51, h2::hpack::header{ "referer", "" }},
    {52, h2::hpack::header{ "refresh", "" }},
    {53, h2::hpack::header{ "retry-after", "" }},
    {54, h2::hpack::header{ "server", "" }},
    {55, h2::hpack::header{ "set-cookie", "" }},
    {56, h2::hpack::header{ "strict-transport-security", "" }},
    {57, h2::hpack::header{ "transfer-encoding", "" }},
    {58, h2::hpack::header{ "user-agent", "" }},
    {59, h2::hpack::header{ "vary", "" }},
    {60, h2::hpack::header{ "via", "" }},
    {61, h2::hpack::header{ "www-authenticate", "" }},
};
// clang-format on
