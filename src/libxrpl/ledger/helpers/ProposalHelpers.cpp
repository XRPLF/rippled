#include <xrpl/ledger/helpers/ProposalHelpers.h>

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>

namespace xrpl::proposal {

bool
isValidProposal(STObject const& proposedTx)
{
    if (isProposalTx(proposedTx))
        return false;

    if (isPseudoTx(proposedTx))
        return false;

    if (proposedTx.isFieldPresent(sfFlags) &&
        (proposedTx.getFieldU32(sfFlags) & tfInnerBatchTxn) != 0u)
        return false;

    if (proposedTx.getFieldU16(sfTransactionType) == ttBATCH &&
        proposedTx.isFieldPresent(sfRawTransactions))
    {
        STArray const& innerTxns = proposedTx.getFieldArray(sfRawTransactions);
        for (STObject const& inner : innerTxns)
        {
            if (isProposalTx(inner) || isPseudoTx(inner))
                return false;
        }
    }

    return true;
}

}  // namespace xrpl::proposal
