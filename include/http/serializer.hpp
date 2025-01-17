#pragma once

#include "../protocol.hpp"
#include "response.hpp"

template<typename T>
struct serializer;

template<>
struct serializer<http_response> {
    public:
    bool                   serialize_body;
    std::vector<std::byte> operator()(const http_response &response_) const;
};

template<>
struct serializer<rite::protocol> {
    public:
    std::string_view operator()(const rite::protocol &proto) const;
};
