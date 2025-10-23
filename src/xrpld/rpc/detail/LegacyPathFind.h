#ifndef XRPL_RPC_LEGACYPATHFIND_H_INCLUDED
#define XRPL_RPC_LEGACYPATHFIND_H_INCLUDED

#include <atomic>

namespace ripple {

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
}  // namespace ripple

#endif
