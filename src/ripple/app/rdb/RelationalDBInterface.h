//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_CORE_RELATIONALDBINTERFACE_H_INCLUDED
#define RIPPLE_CORE_RELATIONALDBINTERFACE_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/core/Config.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/peerfinder/impl/Store.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include <boost/variant.hpp>

namespace ripple {

struct LedgerHashPair
{
    uint256 ledgerHash;
    uint256 parentHash;
};

struct LedgerRange
{
    uint32_t min;
    uint32_t max;
};

class RelationalDBInterface
{
public:
    struct CountMinMax
    {
        std::size_t numberOfRows;
        LedgerIndex minLedgerSequence;
        LedgerIndex maxLedgerSequence;
    };

    struct AccountTxMarker
    {
        std::uint32_t ledgerSeq = 0;
        std::uint32_t txnSeq = 0;
    };

    struct AccountTxOptions
    {
        AccountID const& account;
        std::uint32_t minLedger;
        std::uint32_t maxLedger;
        std::uint32_t offset;
        std::uint32_t limit;
        bool bUnlimited;
    };

    struct AccountTxPageOptions
    {
        AccountID const& account;
        std::uint32_t minLedger;
        std::uint32_t maxLedger;
        std::optional<AccountTxMarker> marker;
        std::uint32_t limit;
        bool bAdmin;
    };

    using AccountTx =
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>;
    using AccountTxs = std::vector<AccountTx>;
    using txnMetaLedgerType = std::tuple<Blob, Blob, std::uint32_t>;
    using MetaTxsList = std::vector<txnMetaLedgerType>;

    using LedgerSequence = uint32_t;
    using LedgerHash = uint256;
    using LedgerShortcut = RPC::LedgerShortcut;
    using LedgerSpecifier =
        std::variant<LedgerRange, LedgerShortcut, LedgerSequence, LedgerHash>;

    struct AccountTxArgs
    {
        AccountID account;
        std::optional<LedgerSpecifier> ledger;
        bool binary = false;
        bool forward = false;
        uint32_t limit = 0;
        std::optional<AccountTxMarker> marker;
    };

    struct AccountTxResult
    {
        std::variant<AccountTxs, MetaTxsList> transactions;
        LedgerRange ledgerRange;
        uint32_t limit;
        std::optional<AccountTxMarker> marker;
    };

    /// Struct used to keep track of what to write to transactions and
    /// account_transactions tables in Postgres
    struct AccountTransactionsData
    {
        boost::container::flat_set<AccountID> accounts;
        uint32_t ledgerSequence;
        uint32_t transactionIndex;
        uint256 txHash;
        uint256 nodestoreHash;

        AccountTransactionsData(
            TxMeta& meta,
            uint256&& nodestoreHash,
            beast::Journal& j)
            : accounts(meta.getAffectedAccounts(j))
            , ledgerSequence(meta.getLgrSeq())
            , transactionIndex(meta.getIndex())
            , txHash(meta.getTxID())
            , nodestoreHash(std::move(nodestoreHash))
        {
        }
    };

    /**
     * @brief init Creates and returns appropriate interface based on config.
     * @param app Application object.
     * @param config Config object.
     * @param jobQueue JobQueue object.
     * @return Unique pointer to the interface.
     */
    static std::unique_ptr<RelationalDBInterface>
    init(Application& app, Config const& config, JobQueue& jobQueue);

    virtual ~RelationalDBInterface() = default;

    /**
     * @brief getMinLedgerSeq Returns minimum ledger sequence in Ledgers table.
     * @return Ledger sequence or none if no ledgers exist.
     */
    virtual std::optional<LedgerIndex>
    getMinLedgerSeq() = 0;

    /**
     * @brief getMaxLedgerSeq Returns maximum ledger sequence in Ledgers table.
     * @return Ledger sequence or none if no ledgers exist.
     */
    virtual std::optional<LedgerIndex>
    getMaxLedgerSeq() = 0;

    /**
     * @brief getLedgerInfoByIndex Returns ledger by its sequence.
     * @param ledgerSeq Ledger sequence.
     * @return Ledger or none if ledger not found.
     */
    virtual std::optional<LedgerInfo>
    getLedgerInfoByIndex(LedgerIndex ledgerSeq) = 0;

    /**
     * @brief getNewestLedgerInfo Returns info of newest saved ledger.
     * @return Ledger info or none if ledger not found.
     */
    virtual std::optional<LedgerInfo>
    getNewestLedgerInfo() = 0;

    /**
     * @brief getLedgerInfoByHash Returns info of ledger with given hash.
     * @param ledgerHash Hash of the ledger.
     * @return Ledger or none if ledger not found.
     */
    virtual std::optional<LedgerInfo>
    getLedgerInfoByHash(uint256 const& ledgerHash) = 0;

    /**
     * @brief getHashByIndex Returns hash of ledger with given sequence.
     * @param ledgerIndex Ledger sequence.
     * @return Hash of the ledger.
     */
    virtual uint256
    getHashByIndex(LedgerIndex ledgerIndex) = 0;

    /**
     * @brief getHashesByIndex Returns hash of the ledger and hash of parent
     *        ledger for the ledger of given sequence.
     * @param ledgerIndex Ledger sequence.
     * @return Struct LedgerHashPair which contain hashes of the ledger and
     *         its parent ledger.
     */
    virtual std::optional<LedgerHashPair>
    getHashesByIndex(LedgerIndex ledgerIndex) = 0;

    /**
     * @brief getHashesByIndex Returns hash of the ledger and hash of parent
     *        ledger for all ledgers with sequences from given minimum limit
     *        to given maximum limit.
     * @param minSeq Minimum ledger sequence.
     * @param maxSeq Maximum ledger sequence.
     * @return Map which points sequence number of found ledger to the struct
     *         LedgerHashPair which contains ledger hash and its parent hash.
     */
    virtual std::map<LedgerIndex, LedgerHashPair>
    getHashesByIndex(LedgerIndex minSeq, LedgerIndex maxSeq) = 0;

    /**
     * @brief getTxHistory Returns most recent 20 transactions starting from
     *        given number or entry.
     * @param startIndex First number of returned entry.
     * @return Vector of sharded pointers to transactions sorted in
     *         descending order by ledger sequence.
     */
    virtual std::vector<std::shared_ptr<Transaction>>
    getTxHistory(LedgerIndex startIndex) = 0;

    /**
     * @brief ledgerDbHasSpace Checks if ledger database has available space.
     * @param config Config object.
     * @return True if space is available.
     */
    virtual bool
    ledgerDbHasSpace(Config const& config) = 0;

    /**
     * @brief transactionDbHasSpace Checks if transaction database has
     *        available space.
     * @param config Config object.
     * @return True if space is available.
     */
    virtual bool
    transactionDbHasSpace(Config const& config) = 0;
};

template <class T, class C>
T
rangeCheckedCast(C c)
{
    if ((c > std::numeric_limits<T>::max()) ||
        (!std::numeric_limits<T>::is_signed && c < 0) ||
        (std::numeric_limits<T>::is_signed &&
         std::numeric_limits<C>::is_signed &&
         c < std::numeric_limits<T>::lowest()))
    {
        /* This should never happen */
        assert(0);
        JLOG(debugLog().error())
            << "rangeCheckedCast domain error:"
            << " value = " << c << " min = " << std::numeric_limits<T>::lowest()
            << " max: " << std::numeric_limits<T>::max();
    }

    return static_cast<T>(c);
}

}  // namespace ripple

#endif
