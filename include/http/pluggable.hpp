#pragma once

#include <sstream>
#include <vector>
#include <cstdint>
#include <string>

// TODO: Find a better name for this class.
//
// This class provides overloads for the most common data-types,
// allowing for easy & quick de-/serialization from query parameters,
// sessions, cookies, etc.

template<typename T>
class pluggable;

template<>
class pluggable<std::string> {
public:
    static std::string deserialize(const std::string &data) { return data; }
    static std::string serialize(const std::string &data) { return data; }
};

template<typename T>
class pluggable {
public:
    // Serialize the object of type T to a string
    static std::string serialize(const T &data) {
        std::ostringstream oss;
        oss << data; // Use the data parameter
        return oss.str(); // Return the string representation
    }

    // Deserialize a string to an object of type T
    static T deserialize(const std::string &data) {
        std::istringstream iss(data);
        T t;
        iss >> t; // Read the value into t
        return t; // Return the deserialized object
    }
};
