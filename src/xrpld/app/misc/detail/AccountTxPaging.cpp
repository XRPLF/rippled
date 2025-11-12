#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/misc/detail/AccountTxPaging.h>

#include <xrpl/protocol/Serializer.h>

namespace ripple {

void
convertBlobsToTxResult(
    RelationalDatabase::AccountTxs& to,
    std::uint32_t ledger_index,
    std::string const& status,
    Blob const& rawTxn,
    Blob const& rawMeta,
    Application& app)
{
    SerialIter it(makeSlice(rawTxn));
    auto txn = std::make_shared<STTx const>(it);
    std::string reason;

    auto tr = std::make_shared<Transaction>(txn, reason, app);

    auto metaset = std::make_shared<TxMeta>(tr->getID(), ledger_index, rawMeta);

    // if properly formed meta is available we can use it to generate ctid
    if (metaset->getAsObject().isFieldPresent(sfTransactionIndex))
        tr->setStatus(
            Transaction::sqlTransactionStatus(status),
            ledger_index,
            metaset->getAsObject().getFieldU32(sfTransactionIndex),
            app.config().NETWORK_ID);
    else
        tr->setStatus(Transaction::sqlTransactionStatus(status), ledger_index);

    to.emplace_back(std::move(tr), metaset);
};

void
saveLedgerAsync(Application& app, std::uint32_t seq)
{
    if (auto l = app.getLedgerMaster().getLedgerBySeq(seq))
        pendSaveValidated(app, l, false, false);
}

}  // namespace ripple
