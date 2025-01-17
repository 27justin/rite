#pragma once
#include "http/parser.hpp"
#include <protocols/h2.hpp>

#include <cstdint>
template<>
struct h2::frame::characteristics<h2::frame::type::HEADERS> {
    static constexpr uint8_t END_STREAM = 0x1;
    static constexpr uint8_t END_HEADERS = 0x4;
    static constexpr uint8_t PADDED = 0x8;
    static constexpr uint8_t PRIORITY = 0x20;

    static bool is_exclusive(const h2::payload<h2::frame::HEADERS> &payload);
};

template<>
struct h2::payload<h2::frame::HEADERS> {
    public:
    using flags = h2::frame::characteristics<h2::frame::HEADERS>;

    uint8_t  pad_length;        // Optional, only if PADDED
    uint32_t stream_dependency; // 1st bit is EXCLUSIVE, only if PRIORITY
    uint8_t  weight;            // Optional, only if PRIORITY

    std::vector<std::byte> data;

    payload(const h2::frame &frame);
};
