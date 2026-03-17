#pragma once

#include <atomic>

namespace xrpl {

class Application;

namespace RPC {

class LegacyPathFind
{
public:
    LegacyPathFind(bool isAdmin, Application& app);
    ~LegacyPathFind();

    bool
    isOk() const
    {
        return isOk_;
    }

private:
    static std::atomic<int> inProgress;

    bool isOk_;
};

}  // namespace RPC
}  // namespace xrpl
