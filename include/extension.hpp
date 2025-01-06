#pragma once

namespace kana {
class server;
class extension {
    public:
    virtual ~extension();

    virtual void on_load(kana::server &server);
};
}
