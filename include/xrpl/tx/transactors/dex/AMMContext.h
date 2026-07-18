#pragma once

#include <xrpl/protocol/AccountID.h>

#include <cstdint>

namespace xrpl {

/**
 * Maintains AMM info per overall payment engine execution and
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
    static constexpr std::uint8_t kMaxIterations = 30;

private:
    // Tx account owner is required to get the AMM trading fee in BookStep
    AccountID accountID_;
    // true if payment has multiple paths
    bool multiPath_{false};
    // Is true if AMM offer is consumed during a payment engine iteration.
    bool ammUsed_{false};
    // Counter of payment engine iterations with consumed AMM
    std::uint16_t ammIters_{0};
    // When true (path_find probes only), BookStep may skip AMMs that already
    // failed a pool invariant this ledger.  Must stay false for payments,
    // offer crossing, and any consensus-critical flow — the failed-AMM
    // blacklist is process-local and non-deterministic across nodes.
    bool excludeFailedAMMs_{false};

public:
    AMMContext(AccountID const& account, bool multiPath, bool excludeFailedAMMs = false)
        : accountID_(account), multiPath_(multiPath), excludeFailedAMMs_(excludeFailedAMMs)
    {
    }
    ~AMMContext() = default;
    AMMContext(AMMContext const&) = delete;
    AMMContext&
    operator=(AMMContext const&) = delete;

    [[nodiscard]] bool
    multiPath() const
    {
        return multiPath_;
    }

    void
    setMultiPath(bool fs)
    {
        multiPath_ = fs;
    }

    /** True only for non-consensus path_find ranking probes. */
    [[nodiscard]] bool
    excludeFailedAMMs() const
    {
        return excludeFailedAMMs_;
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

    [[nodiscard]] bool
    maxItersReached() const
    {
        return ammIters_ >= kMaxIterations;
    }

    [[nodiscard]] std::uint16_t
    curIters() const
    {
        return ammIters_;
    }

    [[nodiscard]] AccountID
    account() const
    {
        return accountID_;
    }

    /**
     * Strand execution may fail. Reset the flag at the start
     * of each payment engine iteration.
     */
    void
    clear()
    {
        ammUsed_ = false;
    }
};

}  // namespace xrpl
