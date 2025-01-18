#include <filesystem>
#include <fstream>
#include <rite/session.hpp>
#include <sstream>
#include <string>

#include <random>

fs::path session::config::SESSION_DIRECTORY = "/tmp/_rite/";
std::string session::config::SESSION_COOKIE_NAME = "SESSID";

std::function<std::string()> session::config::new_session_id = []() {
    // Define the character set to choose from
    const std::string characters =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    constexpr size_t LENGTH = 16;

    // Create a random device and a random number generator
    std::random_device rd;  // Obtain a random number from hardware
    std::mt19937 generator(rd()); // Seed the generator
    std::uniform_int_distribution<> distribution(0, characters.size() - 1); // Define the range

    std::string random_string;
    random_string.reserve(LENGTH); // Reserve space for efficiency

    // Generate random characters
    for (size_t i = 0; i < LENGTH; ++i) {
        random_string += characters[distribution(generator)];
    }

    return random_string;
};

session::session(http_request &request, http_response &response)
  : request_(request)
  , response_(response)
  , values_() {

    if (!fs::exists(session::config::SESSION_DIRECTORY)) {
        fs::create_directory(session::config::SESSION_DIRECTORY);
    }
    id = request.cookies()
           .get<std::string>(session::config::SESSION_COOKIE_NAME)
           .or_else([&response](const auto &) -> std::expected<std::string, http_request::cookie_jar::error> {
               auto id = session::config::new_session_id();
               response.cookies().set(session::config::SESSION_COOKIE_NAME, id);
               return id;
           })
           .value();

    if (fs::exists(session::config::SESSION_DIRECTORY / id)) {
        std::ifstream file(session::config::SESSION_DIRECTORY / id);
        std::string line, key, value;
        while (std::getline(file, line)) {
            std::istringstream entry(line);
            std::getline(entry, key, '=');
            std::getline(entry, value, '=');

            values_[key] = session::value {
                .v = untreated(value),
                .serialize = [](std::any v) { return (std::string) std::any_cast<untreated>(v); }
            };
        }
    }
}

void
session::save() {
    std::ofstream file(session::config::SESSION_DIRECTORY / id);
    for (auto &[k, v] : values_) {
        file << k << '=';
        file << v.serialize(v.v);
        file << '\n';
    }
}

session::~session() {
    save();
}
