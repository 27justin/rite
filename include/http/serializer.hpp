#pragma once

#include "response.hpp"
#include "../protocol.hpp"

template<typename T>
struct serializer;

template<>
struct serializer<http_response> {
public:
    bool serialize_body;
    std::vector<std::byte> operator()(const http_response &response_) const;
};

template<>
struct serializer<kana::protocol> {
public:
    std::string_view operator()(const kana::protocol &proto) const;
};

