#pragma once

#include <atomic>
#include <xrpl/basics/TraceLog.h>

namespace xrpl {

class Application;

namespace RPC {

class LegacyPathFind
{
public:
    LegacyPathFind(bool isAdmin, Application& app);
    ~LegacyPathFind();

    [[nodiscard]] bool
    isOk() const
    {
    TRACE_FUNC();
        return isOk_;
    }

private:
    static std::atomic<int> inProgress;

    bool isOk_{false};
};

}  // namespace RPC
}  // namespace xrpl
