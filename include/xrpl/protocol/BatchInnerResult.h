#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl {

/**
 * @brief Outcome of one inner transaction of a Batch, recorded while the outer Batch was
 *        applied.
 *
 * Inner transactions that fail with a ter/tem/tef result are never applied and leave no
 * metadata in the ledger, and a tfAllOrNothing rollback discards the ones that did apply.
 * This record is the only place those outcomes survive. It is node-local and not part of
 * the ledger hash.
 */
struct BatchInnerResult
{
    uint256 parentBatchId;
    uint256 innerTxId;

    /**
     * @brief Position in the outer transaction's RawTransactions.
     */
    std::uint32_t index = 0;

    TER ter = tesSUCCESS;

    /**
     * @brief True if the inner transaction's changes ended up in the ledger.
     */
    bool applied = false;
};

}  // namespace xrpl
