#include <optional>
#include <span>
#include <sstream>

#include "connection.hpp"
#include "http/parser.hpp"
#include "http/request.hpp"

bool
// parser<http_request>::parse(const std::shared_ptr<connection> &conn, std::span<const std::byte> data, http_request &req) {
parser<http_request>::parse(connection<void> *conn, std::span<const std::byte> data, http_request &req) {
    req.client_ = conn;
    std::string        request_string(reinterpret_cast<const char *>(data.data()), data.size());
    std::istringstream request_stream(request_string);
    std::string        line;

    // Parse the request line
    if (!std::getline(request_stream, line)) {
        return false;
    }

    std::istringstream request_line(line);
    std::string        method_str;
    std::string        path;
    std::string        version;

    // Read method, path, and version
    if (!(request_line >> method_str >> path >> version)) {
        return false;
    }

    if (version == "HTTP/1.1") {
        req.version_ = http_version::HTTP_1_1;
    } else if (version == "HTTP/2.0") {
        req.version_ = http_version::HTTP_2_0;
    } else if (version == "HTTP/3.0") {
        req.version_ = http_version::HTTP_3_0;
    } else if (version == "HTTP/1.0") {
        req.version_ = http_version::HTTP_1_0;
    } else {
        return false;
    }

    if (path.find('?') != std::string::npos) {
        query_parameters query = parser<query_parameters>{}.parse(path).value();
        req.query_ = query;
    }

    // Set the method
    if (method_str == "GET") {
        req.method_ = http_method::GET;
    } else if (method_str == "HEAD") {
        req.method_ = http_method::HEAD;
    } else if (method_str == "POST") {
        req.method_ = http_method::POST;
    } else if (method_str == "PUT") {
        req.method_ = http_method::PUT;
    } else if (method_str == "DELETE") {
        req.method_ = http_method::DELETE;
    } else if (method_str == "CONNECT") {
        req.method_ = http_method::CONNECT;
    } else if (method_str == "OPTIONS") {
        req.method_ = http_method::OPTIONS;
    } else if (method_str == "TRACE") {
        req.method_ = http_method::TRACE;
    } else if (method_str == "PATCH") {
        req.method_ = http_method::PATCH;
    } else {
        return false;
    }

    req.path_ = path;

    // Parse headers
    while (std::getline(request_stream, line) && !line.empty()) {
        auto colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            // Trim whitespace from key and value
            key.erase(key.find_last_not_of(" \n\r\t") + 1);
            value.erase(0, value.find_first_not_of(" \n\r\t"));
            req.headers_[key] = value;
        }
    }

    // Read the body if needed (for POST/PUT requests)
    if (req.method_ == http_method::POST || req.method_ == http_method::PUT) {
        // Read the remaining data from the request stream as the body
        std::string body_string((std::istreambuf_iterator<char>(request_stream)), std::istreambuf_iterator<char>());

        // Convert the string body to std::vector<std::byte>
        req.body_.resize(body_string.size());
        std::transform(body_string.begin(), body_string.end(), req.body_.begin(), [](char c) { return static_cast<std::byte>(c); });
    }
    return true;
}

std::optional<query_parameters>
parser<query_parameters>::parse(std::string_view query_string) {
    if (query_string.find('?') == std::string::npos)
        return std::nullopt;

    ssize_t          current = query_string.find('?') + 1;
    query_parameters query{};

    std::string       param;
    std::stringstream ss = std::stringstream(std::string(query_string.substr(current)));
    while (std::getline(ss, param, '&')) {
        auto equal = param.find('=');
        if (equal == std::string::npos) {
            // Empty parameters are permitted, these will be
            // constructed with an empty string.  Though maybe we
            // should construct them using bool(true)? I think this
            // implicitely makes sense, will have to think abit more
            // about this.
            query.parameters_.emplace_back(std::make_tuple<std::string, std::string>(decode_uri_component(param), ""));
            continue;
        }

        std::string key = param.substr(0, equal);
        std::string value = param.substr(equal + 1);
        query.parameters_.emplace_back(std::make_tuple<std::string, std::string>(decode_uri_component(key), decode_uri_component(value)));
    }
    return query;
}

std::string
decode_uri_component(const std::string &encoded) {
    std::string decoded;
    char        ch;
    size_t      i = 0;
    while (i < encoded.length()) {
        if (encoded[i] == '%') {
            sscanf(encoded.substr(i + 1, 2).c_str(), "%x", (unsigned int *)&ch);
            decoded += ch;
            i += 3; // Skip past the %xx
        } else {
            decoded += encoded[i++];
        }
    }
    return decoded;
}
