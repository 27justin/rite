#include <filesystem>
struct filesystem : public kana::controller {
    static constexpr ssize_t CHUNK_SIZE = 16384;
    public:
    std::string base_;
    filesystem(const std::string &base)
      : base_(base) {};

    void setup(kana::controller_config &config) {
        // clang-format off
        config.add_endpoint(kana::endpoint{
          .method  = GET,
          .path    = std::regex(".*"),
          .handler = [this](http_request &req) -> http_response {
              std::vector<std::string> fs_path{};
              std::istringstream req_path = std::istringstream(std::string(req.path()));

              std::string slice;
              while(std::getline(req_path, slice, '/')) {
                  fs_path.push_back(slice);
              }

              std::filesystem::path file = std::filesystem::path{base_};
              for(const std::string &slice : fs_path) {
                  file = file / slice;
              }
              // Check that the file is definitely under `base_`, if
              // it isn't we are encountering directory traversal
              // and should error out.
              std::filesystem::path parent = std::filesystem::path{base_};
              std::filesystem::path rel = std::filesystem::relative(file, parent);
              if(rel.empty() || (!rel.empty() && rel.native().starts_with(".."))) {
                  std::print("Prevented directory traversal to: {}\nPath: {}\n", rel.string(), file.string());
                  return http_response(http_status_code::eForbidden, "");
              }

              http_response response;
              if(std::filesystem::exists(file)) {
                  // File
                  if(std::filesystem::is_regular_file(file)) {
                      response.set_content_length(std::filesystem::file_size( file ));
                      response.set_status_code(http_status_code::eOk);
                      req.set_context<std::FILE*>(std::fopen(file.string().c_str(), "rb"));

                      response.event(http_response::event::chunk, [&](http_response &response) -> void {
                          std::unique_ptr<std::byte[]> buffer = std::make_unique_for_overwrite<std::byte[]>(CHUNK_SIZE);
                          auto &fstream = req.context<std::FILE*>().value().get();
                          auto bytes = std::fread((char *) buffer.get(), 1, CHUNK_SIZE, fstream);
                          response.stream(kana::buffer (
                                              std::move(buffer),
                                              static_cast<ssize_t>(bytes),
                                              bytes < CHUNK_SIZE
                                              ));
                          std::this_thread::sleep_for(std::chrono::milliseconds(50));
                      });
                      return response;
                  }if(std::filesystem::is_directory(file)) {
                      // Create index page
                      std::string index("");
                      for(auto &entry : std::filesystem::directory_iterator{file}) {
                          index += std::format("<li class=\"{}\"><a href=\"{}/{}\">{}</a></li>\n", entry.is_directory() ? "directory" : "file", req.path().size() > 1 ? req.path() : "", entry.path().filename().string(), entry.path().filename().string());
                      }
                      std::string page = std::format("<!DOCTYPE html><html><head></head><body><ul>{}</ul></body></html>", index);
                      return http_response(http_status_code::eOk, page);
                  }
              }
              return http_response(http_status_code::eNotFound, "Not found.");
          }
        });
        // clang-format on
    }
};
