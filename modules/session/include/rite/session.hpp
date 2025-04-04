#pragma once

#include <any>
#include <http/behaviour.hpp>
#include <http/request.hpp>
#include <http/response.hpp>

#include <expected>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace rite::extensions {
class session : public rite::http::extension {
    public:
    struct config {
        static fs::path                     SESSION_DIRECTORY;
        static std::string                  SESSION_COOKIE_NAME;
        static std::function<std::string()> new_session_id;
    };

    session();

    // Fetches session from disk
    void on_request(http_request &) override;

    // Persists session to disk
    void pre_send(http_request &, http_response &) override;
};
}

namespace rite::http {
class session_handle {
    std::string    id_;

    using untreated = std::string;
    struct value {
        std::any                             v;
        std::function<std::string(std::any)> serialize;
    };

    std::map<std::string, value> values_;

    public:
    enum class error { eCast, eNotFound };

    session_handle(std::string id);
    session_handle(session_handle &&);
    ~session_handle();

    template<typename T>
    void set(const std::string &key, const T &v) {
        values_[key] = value{ .v = v, .serialize = [](const std::any &v) {
                                 return pluggable<T>::serialize(std::any_cast<T>(v));
                             } };
    }

    template<typename T>
    std::expected<T, error> get(const std::string &key) {
        if (!values_.contains(key))
            return std::unexpected(error::eNotFound);

        try {
            // Try to cast the value of `key` to T
            auto val = std::any_cast<T>(values_[key].v);
            return val;
        } catch (const std::bad_any_cast &) {
            // Okay, the values is not T, check for `untreated`.
            // When we load the session into memory, every key
            // is untreated since we do not know it's type.
            //
            // If its `untreated`, we can deserialize using pluggable<T>
            try {
                auto str = std::any_cast<untreated>(values_[key].v);
                auto conversion = pluggable<T>::deserialize(str);
                values_[key].v = conversion;

                // Set the serializer so that it properly flushes later.
                values_[key].serialize = [](const std::any &v) {
                    return pluggable<T>::serialize(std::any_cast<T>(v));
                };

                return conversion;
            } catch (const std::bad_any_cast &) {
                return std::unexpected(error::eCast);
            }
        }
    }

    const std::string &id() const;

    // Flush the session to the disk.  You can call this function
    // yourself, but it will also be invoked on destruction of the
    // object.
    void save();
};
    using session = std::shared_ptr<session_handle>;
}

