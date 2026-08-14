#include <xrpld/app/misc/detail/AccountTxPaging.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerPersistence.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/Transaction.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/core/NetworkIDService.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxMeta.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace xrpl {

bool
passesDelegateFilter(STTx const& tx, DelegateFilter const& filter, AccountID const& contextAccount)
{
    if (!tx.isFieldPresent(sfDelegate))
        return false;

    AccountID const txOwner = tx.getAccountID(sfAccount);
    AccountID const txSigner = tx.getAccountID(sfDelegate);

    switch (filter.type)
    {
        case DelegateType::Actor: {
            // Keep txns where the queried account (A) is the owner but
            // another account (C) was the delegatee that signed.
            bool const isDelegated = (txOwner == contextAccount) && (txSigner != contextAccount);
            if (!isDelegated)
                return false;
            return !filter.counterparty || (txSigner == *filter.counterparty);
        }
        case DelegateType::Authorizer: {
            // Keep txns where the queried account (C) is the signer acting
            // on behalf of another account (A, the delegator/owner).
            bool const isActingAsDelegate =
                (txSigner == contextAccount) && (txOwner != contextAccount);
            if (!isActingAsDelegate)
                return false;
            return !filter.counterparty || (txOwner == *filter.counterparty);
        }
    }

    return false;  // LCOV_EXCL_LINE
}

bool
passesDelegateFilter(
    Blob const& rawData,
    DelegateFilter const& filter,
    AccountID const& contextAccount)
{
    SerialIter sit{makeSlice(rawData)};
    STTx const tx{sit};
    return passesDelegateFilter(tx, filter, contextAccount);
}

void
convertBlobsToTxResult(
    RelationalDatabase::AccountTxs& to,
    std::uint32_t ledgerIndex,
    std::string const& status,
    Blob const& rawTxn,
    Blob const& rawMeta,
    Application& app)
{
    SerialIter it(makeSlice(rawTxn));
    auto txn = std::make_shared<STTx const>(it);
    std::string reason;

    auto tr = std::make_shared<Transaction>(txn, reason, app);

    auto metaset = std::make_shared<TxMeta>(tr->getID(), ledgerIndex, rawMeta);

    // if properly formed meta is available we can use it to generate ctid
    if (metaset->getAsObject().isFieldPresent(sfTransactionIndex))
    {
        tr->setStatus(
            Transaction::sqlTransactionStatus(status),
            ledgerIndex,
            metaset->getAsObject().getFieldU32(sfTransactionIndex),
            app.getNetworkIDService().getNetworkID());
    }
    else
    {
        tr->setStatus(Transaction::sqlTransactionStatus(status), ledgerIndex);
    }

    to.emplace_back(std::move(tr), metaset);
};

void
saveLedgerAsync(Application& app, std::uint32_t seq)
{
    if (auto l = app.getLedgerMaster().getLedgerBySeq(seq))
        pendSaveValidated(app, l, false, false);
}

}  // namespace xrpl
