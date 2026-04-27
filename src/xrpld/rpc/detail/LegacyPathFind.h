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

    [[nodiscard]] bool
    isOk() const
    {
        return m_isOk;
    }

private:
    static std::atomic<int> inProgress;

    bool m_isOk{false};
};

}  // namespace RPC
}  // namespace xrpl
