#pragma once

#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include "pluggable.hpp"

template<typename T>
struct parser;

// Type trait to check if T is a std::vector
template<typename T>
struct is_vector : std::false_type {};

template<typename T>
struct is_vector<std::vector<T>> : std::true_type {};


class query_parameters {
    std::vector<std::pair<std::string, std::string>> parameters_;

    friend struct parser<query_parameters>;

    public:
    template<typename T>
    std::enable_if_t<!is_vector<T>::value, std::optional<T>>
    get(const std::string &key) const {
        for (const auto &[pkey, value] : parameters_) {
            if (key == pkey) {
                return pluggable<T>::deserialize(value);
            }
        }
        return std::nullopt;
    }

    // Return the first parameter named `key` for array types

template<typename T>
std::enable_if_t<is_vector<T>::value, std::optional<T>>
get(const std::string &key) const {
    using ValueType = typename T::value_type;
    std::vector<ValueType> arr;
    for (const auto &[pkey, value] : parameters_) {
        if (key == pkey) {
            ValueType val = pluggable<ValueType>::deserialize(value);
            arr.emplace_back(std::move(val));
        }
    }
    if (!arr.empty())
        return arr;
    else
        return std::nullopt;
}

};
