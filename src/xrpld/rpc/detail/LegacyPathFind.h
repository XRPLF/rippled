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
        return m_isOk;
    }

private:
    static std::atomic<int> inProgress;

    bool m_isOk;
};

}  // namespace RPC
}  // namespace xrpl
