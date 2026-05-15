#include <xrpld/app/ledger/TransactionMaster.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/Transaction.h>

#include <xrpl/basics/RangeSet.h>
#include <xrpl/basics/TaggedCache.ipp>  // IWYU pragma: keep
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/TxSearched.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

namespace xrpl {

TransactionMaster::TransactionMaster(Application& app)
    : app_(app)
    , cache_(
          "TransactionCache",
          65536,
          std::chrono::minutes{30},
          stopwatch(),
          app_.getJournal("TaggedCache"))
{
}

bool
TransactionMaster::inLedger(
    uint256 const& hash,
    std::uint32_t ledger,
    std::optional<uint32_t> tseq,
    std::optional<uint32_t> netID)
{
    auto txn = cache_.fetch(hash);

    if (!txn)
        return false;

    txn->setStatus(TransStatus::COMMITTED, ledger, tseq, netID);
    return true;
}

std::shared_ptr<Transaction>
TransactionMaster::fetchFromCache(uint256 const& txnID)
{
    return cache_.fetch(txnID);
}

std::variant<std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>, TxSearched>
TransactionMaster::fetch(uint256 const& txnID, ErrorCodeI& ec)
{
    using TxPair = std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>;

    if (auto txn = fetchFromCache(txnID); txn && !txn->isValidated())
        return std::pair{std::move(txn), nullptr};

    auto v = Transaction::load(txnID, app_, ec);

    if (std::holds_alternative<TxSearched>(v))
        return v;

    auto [txn, txnMeta] = std::get<TxPair>(v);

    if (txn)
        cache_.canonicalizeReplaceClient(txnID, txn);

    return std::pair{std::move(txn), std::move(txnMeta)};
}

std::variant<std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>, TxSearched>
TransactionMaster::fetch(
    uint256 const& txnID,
    ClosedInterval<uint32_t> const& range,
    ErrorCodeI& ec)
{
    using TxPair = std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>;

    if (auto txn = fetchFromCache(txnID); txn && !txn->isValidated())
        return std::pair{std::move(txn), nullptr};

    auto v = Transaction::load(txnID, app_, range, ec);

    if (std::holds_alternative<TxSearched>(v))
        return v;

    auto [txn, txnMeta] = std::get<TxPair>(v);

    if (txn)
        cache_.canonicalizeReplaceClient(txnID, txn);

    return std::pair{std::move(txn), std::move(txnMeta)};
}

std::shared_ptr<STTx const>
TransactionMaster::fetch(
    boost::intrusive_ptr<SHAMapItem> const& item,
    SHAMapNodeType type,
    std::uint32_t uCommitLedger)
{
    std::shared_ptr<STTx const> txn;
    auto iTx = fetchFromCache(item->key());

    if (!iTx)
    {
        if (type == SHAMapNodeType::TnTransactionNm)
        {
            SerialIter sit(item->slice());
            txn = std::make_shared<STTx const>(std::ref(sit));
        }
        else if (type == SHAMapNodeType::TnTransactionMd)
        {
            auto blob = SerialIter{item->slice()}.getVL();
            txn = std::make_shared<STTx const>(SerialIter{blob.data(), blob.size()});
        }
    }
    else
    {
        if (uCommitLedger != 0u)
            iTx->setStatus(TransStatus::COMMITTED, uCommitLedger);

        txn = iTx->getSTransaction();
    }

    return txn;
}

void
TransactionMaster::canonicalize(std::shared_ptr<Transaction>* pTransaction)
{
    uint256 const tid = (*pTransaction)->getID();
    if (tid != beast::kZero)
    {
        auto txn = *pTransaction;
        // VFALCO NOTE canonicalize can change the value of txn!
        cache_.canonicalizeReplaceClient(tid, txn);
        *pTransaction = txn;
    }
}

void
TransactionMaster::sweep(void)
{
    cache_.sweep();
}

TaggedCache<uint256, Transaction>&
TransactionMaster::getCache()
{
    return cache_;
}

}  // namespace xrpl
