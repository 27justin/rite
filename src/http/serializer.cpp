#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>

#include "http/request.hpp"
#include "http/response.hpp"
#include "http/serializer.hpp"

std::vector<std::byte>
serializer<http_response>::operator()(const http_response &response) const {
    std::vector<std::byte> serialized_data;

    // Write the HTTP version
    const char *http_version = "HTTP/1.1 ";
    serialized_data.insert(serialized_data.end(), reinterpret_cast<const std::byte *>(http_version), reinterpret_cast<const std::byte *>(http_version) + strlen(http_version));

    // Serialize the status code as a string
    int                status_code = static_cast<int>(response.status_code());
    std::ostringstream status_stream;
    status_stream << status_code << "\r\n";
    std::string status_code_str = status_stream.str();
    serialized_data.insert(serialized_data.end(), reinterpret_cast<const std::byte *>(status_code_str.data()), reinterpret_cast<const std::byte *>(status_code_str.data()) + status_code_str.size());

    // Serialize the headers
    const header_map &headers = response.headers();
    for (const auto &[key, value] : headers) {
        std::string header_line = key + ": " + value + "\r\n";
        serialized_data.insert(serialized_data.end(), reinterpret_cast<const std::byte *>(header_line.data()), reinterpret_cast<const std::byte *>(header_line.data()) + header_line.size());
    }

    // Add a blank line to separate headers from the body
    serialized_data.push_back(static_cast<std::byte>('\r'));
    serialized_data.push_back(static_cast<std::byte>('\n'));

    if (serialize_body) {
        // Serialize the body
        // const std::vector<std::byte>& body = response.body();
        // serialized_data.insert(serialized_data.end(), body.begin(), body.end());
    }

    return serialized_data;
}

std::string_view
serializer<rite::protocol>::operator()(const rite::protocol &proto) const {
    static const char QUIC[] = "quic";
    static const char HTTP[] = "http";
    static const char HTTPS[] = "https";

    switch (proto) {
        case rite::protocol::http:
            return std::string_view(HTTP);
        case rite::protocol::https:
            return std::string_view(HTTPS);
        case rite::protocol::quic:
            return std::string_view(QUIC);
    }
    return std::string_view("exhaustive check failed");
}
