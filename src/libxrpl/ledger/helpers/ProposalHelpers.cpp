#include <xrpl/ledger/helpers/ProposalHelpers.h>

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>

#include <algorithm>
#include <array>
#include <cstddef>

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

bool
hasDuplicateSigningAccounts(STObject const& proposedTx)
{
    std::array<AccountID, kSigningAccountFields.size()> seen;
    std::size_t count = 0;
    for (auto const* field : kSigningAccountFields)
    {
        if (!proposedTx.isFieldPresent(*field))
            continue;

        AccountID const account = proposedTx.getAccountID(*field);
        if (std::ranges::contains(seen.begin(), seen.begin() + count, account))
            return true;
        seen[count++] = account;
    }
    return false;
}

}  // namespace xrpl::proposal
