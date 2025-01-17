#pragma once
#include <cstdint>

enum http_method : uint16_t { GET = 1 << 0, HEAD = 1 << 1, POST = 1 << 2, PUT = 1 << 3, DELETE = 1 << 4, CONNECT = 1 << 5, OPTIONS = 1 << 6, TRACE = 1 << 7, PATCH = 1 << 8 };
