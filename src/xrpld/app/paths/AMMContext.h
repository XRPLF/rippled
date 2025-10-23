#ifndef XRPL_APP_PATHS_AMMCONTEXT_H_INCLUDED
#define XRPL_APP_PATHS_AMMCONTEXT_H_INCLUDED

#include <xrpl/protocol/AccountID.h>

#include <cstdint>

namespace ripple {

/** Maintains AMM info per overall payment engine execution and
 * individual iteration.
 * Only one instance of this class is created in Flow.cpp::flow().
 * The reference is percolated through calls to AMMLiquidity class,
 * which handles AMM offer generation.
 */
class AMMContext
{
public:
    // Restrict number of AMM offers. If this restriction is removed
    // then need to restrict in some other way because AMM offers are
    // not counted in the BookStep offer counter.
    constexpr static std::uint8_t MaxIterations = 30;

private:
    // Tx account owner is required to get the AMM trading fee in BookStep
    AccountID account_;
    // true if payment has multiple paths
    bool multiPath_{false};
    // Is true if AMM offer is consumed during a payment engine iteration.
    bool ammUsed_{false};
    // Counter of payment engine iterations with consumed AMM
    std::uint16_t ammIters_{0};

public:
    AMMContext(AccountID const& account, bool multiPath)
        : account_(account), multiPath_(multiPath)
    {
    }
    ~AMMContext() = default;
    AMMContext(AMMContext const&) = delete;
    AMMContext&
    operator=(AMMContext const&) = delete;

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
    setAMMUsed()
    {
        ammUsed_ = true;
    }

    void
    update()
    {
        if (ammUsed_)
            ++ammIters_;
        ammUsed_ = false;
    }

    bool
    maxItersReached() const
    {
        return ammIters_ >= MaxIterations;
    }

    std::uint16_t
    curIters() const
    {
        return ammIters_;
    }

    AccountID
    account() const
    {
        return account_;
    }

    /** Strand execution may fail. Reset the flag at the start
     * of each payment engine iteration.
     */
    void
    clear()
    {
        ammUsed_ = false;
    }
};

}  // namespace ripple

#endif  // XRPL_APP_PATHS_AMMCONTEXT_H_INCLUDED
