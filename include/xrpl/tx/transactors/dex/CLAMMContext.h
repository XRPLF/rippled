#pragma once

#include <xrpl/protocol/AccountID.h>

#include <cstdint>

namespace xrpl {

/** Maintains CLAMM info per overall payment engine execution and
 * individual iteration.
 * Only one instance of this class is created in Flow.cpp::flow().
 * The reference is percolated through calls to CLAMMLiquidity class,
 * which handles CLAMM offer generation.
 * Mirrors AMMContext for consistency.
 */
class CLAMMContext
{
public:
    // Restrict number of CLAMM offers. If this restriction is removed
    // then need to restrict in some other way because CLAMM offers are
    // not counted in the BookStep offer counter.
    constexpr static std::uint8_t MaxIterations = 30;

private:
    AccountID account_;
    bool multiPath_{false};
    bool clammUsed_{false};
    std::uint16_t clammIters_{0};

public:
    CLAMMContext(AccountID const& account, bool multiPath)
        : account_(account), multiPath_(multiPath)
    {
    }
    ~CLAMMContext() = default;
    CLAMMContext(CLAMMContext const&) = delete;
    CLAMMContext&
    operator=(CLAMMContext const&) = delete;

    bool
    multiPath() const
    {
        return multiPath_;
    }

    void
    setMultiPath(bool fs)
    {
        multiPath_ = fs;
    }

    void
    setCLAMMUsed()
    {
        clammUsed_ = true;
    }

    void
    update()
    {
        if (clammUsed_)
            ++clammIters_;
        clammUsed_ = false;
    }

    bool
    maxItersReached() const
    {
        return clammIters_ >= MaxIterations;
    }

    std::uint16_t
    curIters() const
    {
        return clammIters_;
    }

    AccountID
    account() const
    {
        return account_;
    }

    void
    clear()
    {
        clammUsed_ = false;
    }
};

}  // namespace xrpl
