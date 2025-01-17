#pragma once

#include "http/request.hpp"
#include "http/response.hpp"

#include <functional>

namespace rite {
class middleware {
    public:
    virtual ~middleware() = default;
    virtual std::optional<http_response> run(http_request &request) = 0;
};
}
