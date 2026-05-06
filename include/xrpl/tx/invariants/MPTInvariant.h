#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace xrpl {

class ValidMPTIssuance
{
    std::uint32_t mptIssuancesCreated_ = 0;
    std::uint32_t mptIssuancesDeleted_ = 0;

    std::uint32_t mptokensCreated_ = 0;
    std::uint32_t mptokensDeleted_ = 0;
    // non-MPT transactions may attempt to create
    // MPToken by an issuer
    bool mptCreatedByIssuer_ = false;

    // sfReferenceHolding is intended to be set exactly once at vault
    // creation and immutable thereafter. Track violations of either rule.
    bool referenceHoldingSetOnCreate_ = false;
    bool referenceHoldingMutated_ = false;
    // MPTokens and RippleStates deleted during the apply. In finalize we
    // look up each holder's AccountRoot to determine whether it was a
    // vault pseudo-account; deleting such a holding outside VaultDelete
    // trips the invariant. All these checks are gated on fixCleanup3_2_0.
    std::vector<std::shared_ptr<SLE const>> deletedHoldings_;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    [[nodiscard]] bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&) const;
};

/** Verify:
 *    - OutstandingAmount <= MaximumAmount for any MPT
 *    - OutstandingAmount after = OutstandingAmount before +
 *         sum (MPT after - MPT before) - this is total MPT credit/debit
 */
class ValidMPTPayment
{
    enum class Order { Before = 0, After = 1 };
    struct MPTData
    {
        std::array<std::int64_t, 2> outstanding{};
        // sum (MPT after - MPT before)
        std::int64_t mptAmount{0};
    };

    // true if OutstandingAmount > MaximumAmount in after for any MPT
    bool overflow_{false};
    // mptid:MPTData
    hash_map<uint192, MPTData> data_;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
