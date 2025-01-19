#pragma once

#include <cstddef>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <vector>
#include <expected>

#include <any>
#include <memory>
#include <optional>
#include <unordered_map>

#include "connection.hpp"
#include "header_map.hpp"
#include "method.hpp"
#include "pluggable.hpp"
#include "query_parameters.hpp"
#include "version.hpp"

template<typename T>
struct parser;

struct http_request {
    public:
    std::string                          path_;
    query_parameters                     query_;
    http_method                          method_;
    std::vector<std::byte>               body_;
    // TODO: This is bad! Headers are NOT unique!
    header_map                           headers_;
    std::unordered_map<size_t, std::any> context_;
    http_version                         version_;

    // std::shared_ptr<connection> client_;
    connection<void> *client_;
    // sockfd          client_;

    friend struct parser<http_request>;

    public:
    int socket() const { return client_->socket(); }
    // std::shared_ptr<connection> client() { return client_; }
    connection<void> *client() { return client_; }

    http_method method() const { return method_; }

    std::string_view  path() const { return path_; }
    const header_map &headers() const { return headers_; }

    template<typename T>
    void set_context(T &&value) {
        // Use std::decay to remove references and cv-qualifiers
        using ValueType = typename std::decay<T>::type;

        // Store the value in the context map
        context_.emplace(typeid(ValueType).hash_code(), std::make_any<T>(value));
    }

    template<typename T>
    std::optional<std::reference_wrapper<T>> context() {
        if (!context_.contains(typeid(T).hash_code()))
            return std::nullopt;
        std::any &ref = context_[typeid(T).hash_code()];
        return std::any_cast<T &>(ref);
    }

    std::optional<std::string_view> header(const std::string &key) const {
        if (headers_.contains(key))
            return headers_.at(key);
        return std::nullopt;
    }

    // TODO: I think we need a better way to do this.
    // Currently both http_request & *_response provide their own `cookie_jar`
    // where the request's cookie_jar can only read, the response's can only set.
    // Streamlining this into one class would be quite nice.
    class cookie_jar {
        std::unordered_map<std::string, std::string> cookies_;
    public:
        enum class error { eNotFound, eDeserialization };

        cookie_jar(http_request &request) {
            for (auto const &h : request.headers()) {
                if (h.first == "cookie") {
                    std::istringstream ss(h.second);
                    std::string cookie;
                    while (std::getline(ss, cookie, ';')) {
                        std::istringstream inner(cookie);
                        std::string key, value;

                        std::getline(inner, key, '=');
                        std::print("Got cookie '{}'\n", key);

                        std::getline(inner, value, '=');
                        cookies_[key] = value;
                    }
                }
            }
        }

        bool has(const std::string &key) {
            return cookies_.contains(key);
        }

        template<typename T>
        std::expected<T, error> get(const std::string &key) {
            if(!cookies_.contains(key)) return std::unexpected(error::eNotFound);

            try {
                T t = pluggable<T>::deserialize(cookies_[key]);
                return t;
            } catch (...) {
                return std::unexpected(error::eDeserialization);
            }
        }
    };

    cookie_jar cookies() {
        return cookie_jar(*this);
    }

    const std::vector<std::byte> &body() const { return body_; }
    query_parameters &query() { return query_; }
};

