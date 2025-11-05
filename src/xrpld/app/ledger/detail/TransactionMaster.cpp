#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/Transaction.h>

#include <xrpl/basics/TaggedCache.ipp>
#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/STTx.h>

namespace ripple {

TransactionMaster::TransactionMaster(Application& app)
    : mApp(app)
    , mCache(
          "TransactionCache",
          65536,
          std::chrono::minutes{30},
          stopwatch(),
          mApp.journal("TaggedCache"))
{
}

bool
TransactionMaster::inLedger(
    uint256 const& hash,
    std::uint32_t ledger,
    std::optional<uint32_t> tseq,
    std::optional<uint32_t> netID)
{
    auto txn = mCache.fetch(hash);

    if (!txn)
        return false;

    txn->setStatus(COMMITTED, ledger, tseq, netID);
    return true;
}

std::shared_ptr<Transaction>
TransactionMaster::fetch_from_cache(uint256 const& txnID)
{
    return mCache.fetch(txnID);
}

std::variant<
    std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
    TxSearched>
TransactionMaster::fetch(uint256 const& txnID, error_code_i& ec)
{
    using TxPair =
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>;

    if (auto txn = fetch_from_cache(txnID); txn && !txn->isValidated())
        return std::pair{std::move(txn), nullptr};

    auto v = Transaction::load(txnID, mApp, ec);

    if (std::holds_alternative<TxSearched>(v))
        return v;

    auto [txn, txnMeta] = std::get<TxPair>(v);

    if (txn)
        mCache.canonicalize_replace_client(txnID, txn);

    return std::pair{std::move(txn), std::move(txnMeta)};
}

std::variant<
    std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
    TxSearched>
TransactionMaster::fetch(
    uint256 const& txnID,
    ClosedInterval<uint32_t> const& range,
    error_code_i& ec)
{
    using TxPair =
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>;

    if (auto txn = fetch_from_cache(txnID); txn && !txn->isValidated())
        return std::pair{std::move(txn), nullptr};

    auto v = Transaction::load(txnID, mApp, range, ec);

    if (std::holds_alternative<TxSearched>(v))
        return v;

    auto [txn, txnMeta] = std::get<TxPair>(v);

    if (txn)
        mCache.canonicalize_replace_client(txnID, txn);

    return std::pair{std::move(txn), std::move(txnMeta)};
}

std::shared_ptr<STTx const>
TransactionMaster::fetch(
    boost::intrusive_ptr<SHAMapItem> const& item,
    SHAMapNodeType type,
    std::uint32_t uCommitLedger)
{
    std::shared_ptr<STTx const> txn;
    auto iTx = fetch_from_cache(item->key());

    if (!iTx)
    {
        if (type == SHAMapNodeType::tnTRANSACTION_NM)
        {
            SerialIter sit(item->slice());
            txn = std::make_shared<STTx const>(std::ref(sit));
        }
        else if (type == SHAMapNodeType::tnTRANSACTION_MD)
        {
            auto blob = SerialIter{item->slice()}.getVL();
            txn = std::make_shared<STTx const>(
                SerialIter{blob.data(), blob.size()});
        }
    }
    else
    {
        if (uCommitLedger)
            iTx->setStatus(COMMITTED, uCommitLedger);

        txn = iTx->getSTransaction();
    }

    return txn;
}

void
TransactionMaster::canonicalize(std::shared_ptr<Transaction>* pTransaction)
{
    uint256 const tid = (*pTransaction)->getID();
    if (tid != beast::zero)
    {
        auto txn = *pTransaction;
        // VFALCO NOTE canonicalize can change the value of txn!
        mCache.canonicalize_replace_client(tid, txn);
        *pTransaction = txn;
    }
}

void
TransactionMaster::sweep(void)
{
    mCache.sweep();
}

TaggedCache<uint256, Transaction>&
TransactionMaster::getCache()
{
    return mCache;
}

}  // namespace ripple
