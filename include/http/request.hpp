#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <sys/socket.h>
#include <vector>

#include <any>
#include <memory>
#include <optional>
#include <unordered_map>

#include "connection.hpp"
#include "header_map.hpp"
#include "method.hpp"
#include "query_parameters.hpp"

template<typename T>
class parser;

struct http_request {
    private:
    std::string                          path_;
    query_parameters                     query_;
    http_method                          method_;
    std::vector<std::byte>               body_;
    header_map                           headers_;
    std::unordered_map<size_t, std::any> context_;

    std::shared_ptr<connection>          client_;

    friend class parser<http_request>;

    public:

    int socket() const { return client_->socket(); }
    std::shared_ptr<connection> &client() { return client_; }

    http_method method() const { return method_; }

    std::string_view  path() const { return path_; }
    const header_map &headers() const { return headers_; }

    template<typename T>
    void set_context(T &&value) {
        context_[typeid(T).hash_code()] = std::move(value);
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

    query_parameters &query() { return query_; }
};
