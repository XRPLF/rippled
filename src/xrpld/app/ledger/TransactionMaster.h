#pragma once

#include <xrpld/app/misc/Transaction.h>

#include <xrpl/basics/RangeSet.h>
#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/TxSearched.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

namespace xrpl {

class Application;
class STTx;

// Tracks all transactions in memory

class TransactionMaster
{
public:
    TransactionMaster(Application& app);
    TransactionMaster(TransactionMaster const&) = delete;
    TransactionMaster&
    operator=(TransactionMaster const&) = delete;

    std::shared_ptr<Transaction>
    fetchFromCache(uint256 const&);

    std::variant<std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>, TxSearched>
    fetch(uint256 const&, ErrorCodeI& ec);

    /**
     * Fetch transaction from the cache or database.
     *
     * @return A std::variant that contains either a
     *         pair of shared_pointer to the retrieved transaction
     *         and its metadata or an enum indicating whether or not
     *         the all ledgers in the provided range were present in
     *         the database while the search was conducted.
     */
    std::variant<std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>, TxSearched>
    fetch(uint256 const&, ClosedInterval<uint32_t> const& range, ErrorCodeI& ec);

    std::shared_ptr<STTx const>
    fetch(
        boost::intrusive_ptr<SHAMapItem> const& item,
        SHAMapNodeType type,
        std::uint32_t uCommitLedger);

    // return value: true = we had the transaction already
    bool
    inLedger(
        uint256 const& hash,
        std::uint32_t ledger,
        std::optional<uint32_t> tseq,
        std::optional<uint32_t> netID);

    void
    canonicalize(std::shared_ptr<Transaction>* pTransaction);

    void
    sweep(void);

    TaggedCache<uint256, Transaction>&
    getCache();

private:
    Application& app_;
    TaggedCache<uint256, Transaction> cache_;
};

}  // namespace xrpl
