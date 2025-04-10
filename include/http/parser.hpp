#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <variant>

#include "query_parameters.hpp"
#include "request.hpp"

// hpp
template<typename T>
struct parser;

template<typename T>
class connection;

std::string
decode_uri_component(const std::string &encoded);

// Specialization for http_request
template<>
struct parser<http_request> {
    public:
    // bool parse(const std::shared_ptr<connection> &connection, std::span<const std::byte> data, http_request &req);
    bool parse(connection<void> *connection, std::span<const std::byte> data, http_request &req);
};

// Specialization for query_parameters
template<>
struct parser<query_parameters> {
    public:
    std::optional<query_parameters> parse(std::string_view query_string);
};
