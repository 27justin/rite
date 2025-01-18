#include <filesystem>
#include <fstream>
#include <rite/session.hpp>
#include <sstream>
#include <string>

#include <random>

namespace rite::extensions {
fs::path                     session::config::SESSION_DIRECTORY = "/tmp/_rite/";
std::string                  session::config::SESSION_COOKIE_NAME = "SESSID";
std::function<std::string()> session::config::new_session_id = []() {
    // Define the character set to choose from
    const std::string characters = "0123456789"
                                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                   "abcdefghijklmnopqrstuvwxyz";
    constexpr size_t  LENGTH = 16;

    // Create a random device and a random number generator
    std::random_device              rd;                                     // Obtain a random number from hardware
    std::mt19937                    generator(rd());                        // Seed the generator
    std::uniform_int_distribution<> distribution(0, characters.size() - 1); // Define the range

    std::string random_string;
    random_string.reserve(LENGTH); // Reserve space for efficiency

    // Generate random characters
    for (size_t i = 0; i < LENGTH; ++i) {
        random_string += characters[distribution(generator)];
    }

    return random_string;
};

session::session() {}

void
session::on_request(http_request &request) {

    if (!fs::exists(session::config::SESSION_DIRECTORY)) {
        fs::create_directory(session::config::SESSION_DIRECTORY);
    }
    std::string id = request.cookies().get<std::string>(session::config::SESSION_COOKIE_NAME).value_or(session::config::new_session_id());
    request.set_context(std::make_shared<rite::http::session_handle>(id));
}

void
session::pre_send(http_request &request, http_response &response) {
    // Set the `Set-Cookie` header for the session (if it does not exist yet)
    auto sess = request.context<rite::http::session>().value();

    if (!request.cookies().has(session::config::SESSION_COOKIE_NAME)) {
        response.set_header("Set-Cookie", std::format("{}={}", session::config::SESSION_COOKIE_NAME, sess.get()->id()));
    }
    // Persisting the session will be automatically handled after the handler finished.
}

}

namespace rite::http {
session_handle::session_handle(std::string id)
  : id_(id) {
    if (fs::exists(rite::extensions::session::config::SESSION_DIRECTORY / id)) {
        std::ifstream file(rite::extensions::session::config::SESSION_DIRECTORY / id);
        std::string   line, key, value;
        while (std::getline(file, line)) {
            std::istringstream entry(line);
            std::getline(entry, key, '=');
            std::getline(entry, value, '=');

            values_[key] = session_handle::value { .v = untreated(value), .serialize = [](std::any v) {
                                              return (std::string)std::any_cast<untreated>(v);
                                          } };
        }
    }
}

void
session_handle::save() {
    fs::path session_file = rite::extensions::session::config::SESSION_DIRECTORY / id_;
    // Delete empty sessions
    if (values_.size() == 0) {
        if(fs::exists(session_file)) {
            fs::remove(session_file);
        }
        return;
    }

    // Flush the session using each entries serializer.
    std::ofstream file(session_file);
    for (auto &[k, v] : values_) {
        file << k << '=';
        file << v.serialize(v.v);
        file << '\n';
    }
}

const std::string &
session_handle::id() const {
    return id_;
}

session_handle::~session_handle() {
    save();
}

session_handle::session_handle(session_handle &&other)
  : id_(std::move(other.id_))
  , values_(std::move(other.values_)) {
}
}
