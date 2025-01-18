#pragma once

#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include "pluggable.hpp"

template<typename T>
struct parser;

template<typename T>
struct pluggable<std::vector<T>> {
    using value_type = void;
};

class query_parameters {
    std::vector<std::pair<std::string, std::string>> parameters_;

    friend struct parser<query_parameters>;

    public:
    // template<typename T>
    // std::enable_if_t<std::is_same_v<T, typename pluggable<T>::value_type>, std::optional<T>> get(const std::string_view &key) const {
    //     for (const auto &[pkey, value] : parameters_) {
    //         if (key == pkey) {
    //             std::stringstream ss(value);
    //             T                 val;
    //             ss >> val;
    //             return val;
    //         }
    //     }
    //     return std::nullopt;
    // }

    // // Return the first parameter named `key` for array types
    // template<typename T>
    // std::enable_if_t<std::is_same_v<T, std::vector<typename T::value_type>>, std::optional<T>> get(const std::string_view &key) const {
    //     std::vector<typename T::value_type> arr;
    //     for (const auto &[pkey, value] : parameters_) {
    //         if (key == pkey) {
    //             std::stringstream      ss(value);
    //             typename T::value_type val;
    //             ss >> val;
    //             arr.emplace_back(std::move(val));
    //         }
    //     }
    //     if (!arr.empty())
    //         return arr;
    //     else
    //         return std::nullopt;
    // }
};
