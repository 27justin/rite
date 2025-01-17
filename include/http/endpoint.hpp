#pragma once

#include <functional>
#include <future>
#include <list>
#include <optional>
#include <regex>
#include <variant>
#include <iostream>

#include "http/method.hpp"
#include "http/request.hpp"
#include "http/response.hpp"

namespace rite::http {
class path {
public:
    struct result {
        std::unordered_map<std::string, std::string> parameters; // Extracted parameters
        // Template method to get a parameter by name
        template<typename T>
        std::optional<T> get(const std::string &name) const {
            auto it = parameters.find(name);
            if (it != parameters.end()) {
                // Convert the string to the desired type
                if constexpr (std::is_same_v<T, uint64_t>) {
                    try {
                        return std::stoull(it->second);
                    } catch (...) {
                        return std::nullopt; // Return nullopt on conversion failure
                    }
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return it->second; // Return the string directly
                }
                // Add more type conversions as needed
            }
            return std::nullopt; // Return nullopt if parameter not found
        }
    };

    // Constructor that takes a path string and prepares the regex
    path(const std::string &path_str) {
        std::string regex_str;
        std::smatch param_matches;
        std::string::const_iterator search_start(path_str.cbegin());

        // Iterate through the path string to build the regex
        while (std::regex_search(search_start, path_str.cend(), param_matches, std::regex(R"(\{(\w+)(?::([^}]+))?\})"))) {
            // Append the part before the match
            regex_str.append(search_start, param_matches[0].first);

            // Extract the parameter name and optional regex
            std::string param_name = param_matches[1].str();
            std::string param_regex = param_matches[2].str();

            // If no custom regex is provided, use the default
            if (param_regex.empty()) {
                param_regex = "([^/]+)"; // Default to match any character except a slash
            }
            regex_str += "(" + param_regex + ")"; // Add the regex group

            // Move past the last match
            search_start = param_matches[0].second;
            parameter_names.push_back(param_name); // Store the parameter name
        }

        // Append any remaining part of the path
        regex_str.append(search_start, path_str.cend());
        regex_str += R"(/?)"; // Allow for an optional trailing slash

        regex_pattern = std::regex(regex_str); // Compile the regex pattern
    }

    // Method to match a given URL against the regex and extract parameters
    std::optional<result> match(const std::string &url) {
        std::smatch matches;
        std::optional<result> result;
        if (std::regex_match(url, matches, regex_pattern)) {
            result = rite::http::path::result {};
            // Extract parameters based on the regex groups
            for (size_t i = 1; i < matches.size(); ++i) {
                if (i - 1 < parameter_names.size()) {
                    result->parameters[parameter_names[i - 1]] = matches[i].str();
                }
            }
        }
        return result;
    }
private:
    std::regex regex_pattern; // Compiled regex pattern
    std::vector<std::string> parameter_names; // Names of the parameters
};


struct endpoint {
    public:
    int              method; // A bit-set representing the HTTP methods (e.g., GET, POST) that this endpoint supports.
    rite::http::path path;

    // The handler that runs when the endpoint is called.
    std::function<http_response(http_request &, rite::http::path::result)> handler;

    // The maximum number of concurrent requests is limited by the
    // number of `worker_threads` in `rite::server`. Setting this
    // flag to `true` allows the endpoint to bypass this limit by
    // spawning a new thread for each incoming request, enabling
    // greater concurrency at the cost of increased resource usage.
    bool asynchronous = false;

    // The `thread_pool` field allows you to specify a custom thread pool for processing requests
    // associated with this endpoint. Instead of spawning a new thread for each request, the handler
    // function will be dispatched into the provided `jt::mpsc` channel, where it can be picked up
    // and executed by your own thread pool.
    //
    // This is particularly useful for endpoints that may have long-running operations or for
    // handling server-sent events (SSE) that keep a connection open for streaming data. By using
    // your own thread pool, you can limit the number of concurrent threads to a fixed size (N),
    // preventing the potential for resource exhaustion that could occur with the default `asynchronous`
    // behavior, which may spawn an unbounded number of threads.
    std::optional<jt::mpsc<std::function<void()>>::producer> thread_pool;
    // A list of middleware functions that will be applied to requests
    // before reaching the handler, allowing for pre-processing,
    // authentication, logging, etc.
    std::vector<std::string> middlewares;
};
}
