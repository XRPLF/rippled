#include <xrpl/tx/transactors/proposal/ProposalHelpers.h>

#include <xrpl/protocol/STArray.h>  // IWYU pragma: keep (range-for over getFieldArray)
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFlags.h>

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
        for (STObject const& inner : proposedTx.getFieldArray(sfRawTransactions))
        {
            if (isProposalTx(inner) || isPseudoTx(inner))
                return false;
        }
    }

    return true;
}

}  // namespace xrpl::proposal
