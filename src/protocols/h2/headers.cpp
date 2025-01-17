#include <cassert>
#include <netinet/in.h>
#include <protocols/h2/headers.hpp>
#include <stdexcept>

h2::payload<h2::frame::HEADERS>::payload(const h2::frame &frame)
  : pad_length(0)
  , stream_dependency(0)
  , weight(0) {
    auto flags = frame.flags;

    /*
      +---------------+
      |Pad Length? (8)|
      +-+-------------+-----------------------------------------------+
      |E|                 Stream Dependency? (31)                     |
      +-+-------------+-----------------------------------------------+
      |  Weight? (8)  |
      +-+-------------+-----------------------------------------------+
      |                   Header Block Fragment (*)                 ...
      +---------------------------------------------------------------+
      |                           Padding (*)                       ...
      +---------------------------------------------------------------+

      Figure 7: HEADERS Frame Payload
    */
    auto pos = frame.data.begin();
    auto end = frame.data.end();
    if (flags & h2::frame::characteristics<h2::frame::HEADERS>::PADDED) {
        // Pad lengh is present
        std::print("HEADERS frame has PADDING\n");
        pad_length = static_cast<uint8_t>(*(pos++));
    }

    if (flags & h2::frame::characteristics<h2::frame::HEADERS>::PRIORITY) {
        // TODO: Custom parse error
        std::print("HEADERS frame should be prioritized\n");
        if (end - pos < 4)
            throw h2::frame_state::eInvalid;

        // Stream dependency is present
        // TODO: Evaluation order of the OR operations?
        // Might screw us over for this field.
        stream_dependency = static_cast<uint8_t>(*(pos++)) << 24 | static_cast<uint8_t>(*(pos++)) << 16 | static_cast<uint8_t>(*(pos++)) << 8 | static_cast<uint8_t>(*(pos++));
        stream_dependency = ntohl(stream_dependency);

        // Weight is also present
        weight = static_cast<uint8_t>(*(pos++));
    }

    // Check that the remaining size is > than pad_length
    assert(end - pos > pad_length);

    // Copy over the header block
    auto header_block_size = std::distance(frame.data.begin(), pos) - pad_length;
    data.reserve(header_block_size);
    data.insert(data.begin(), pos, end - pad_length);

    // The rest is padding
}

bool
h2::frame::characteristics<h2::frame::type::HEADERS>::is_exclusive(const h2::payload<h2::frame::HEADERS> &payload) {
    return payload.stream_dependency & ((uint32_t)1 << 31);
}
