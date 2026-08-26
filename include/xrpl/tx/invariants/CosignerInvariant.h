#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <map>
#include <vector>

namespace xrpl {

class ReadView;

/**
 * @brief Invariants for TransactionProposal ledger entries.
 *
 * - A proposal's payload and metadata are immutable except for signature
 *   fields and PreviousTxn bookkeeping.
 * - Proposal reserve changes match proposal creation and deletion.
 * - Only transaction types implementing proposal lifecycle operations may
 *   create, modify, or delete proposals.
 * - Collected signer arrays remain sorted, unique, and bounded.
 */
class ValidTransactionProposal
{
    struct Change
    {
        bool isDelete;
        SLE::const_pointer before;
        SLE::const_pointer after;
    };

    std::vector<Change> changes_;
    std::map<AccountID, std::int64_t> ownerCountDelta_;
    std::map<AccountID, std::int64_t> sponsoredOwnerCountDelta_;
    std::map<AccountID, std::int64_t> sponsoringOwnerCountDelta_;
    std::map<AccountID, std::int64_t> expectedOwnerCountDelta_;
    std::map<AccountID, std::int64_t> expectedSponsoredOwnerCountDelta_;
    std::map<AccountID, std::int64_t> expectedSponsoringOwnerCountDelta_;
    std::uint32_t created_ = 0;
    std::uint32_t modified_ = 0;
    std::uint32_t deleted_ = 0;
    bool invalidSignerArrays_ = false;

public:
    void
    visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after);

    [[nodiscard]] bool
    finalize(STTx const& tx, TER result, XRPAmount, ReadView const& view, beast::Journal const& j)
        const;
};

}  // namespace xrpl
