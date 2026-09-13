#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <vector>

namespace xrpl {

/**
 * Invariants for TokenIssuance objects:
 *
 * - created/deleted only by transactions carrying the matching privilege,
 *   and the privileged transactions create/delete exactly one
 * - MaximumAmount, TokenScale, Issuer, and Currency never change; the
 *   MPT binding is write-once
 * - a capped issuance never exceeds
 *   ceil(IssuedAmount * 10^TokenScale) + MPT OutstandingAmount
 *   <= MaximumAmount after application
 * - deletion only with IssuedAmount == 0
 */
class ValidTokenIssuance
{
    std::uint32_t created_ = 0;
    std::uint32_t deleted_ = 0;
    bool immutableChanged_ = false;
    std::vector<SLE::const_pointer> after_;
    std::vector<SLE::const_pointer> deletedIssuances_;

public:
    void
    visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after);

    [[nodiscard]] bool
    finalize(
        STTx const& tx,
        TER const result,
        XRPAmount const fee,
        ReadView const& view,
        beast::Journal const& j);
};

}  // namespace xrpl
