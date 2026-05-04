#pragma once

#include <xrpl/shamap/SHAMap.h>

namespace xrpl {

/** Represents a transaction in RCLConsensus.

    RCLCxTx is a thin wrapper over the SHAMapItem that corresponds to the
    transaction.
*/
class RCLCxTx
{
public:
    //! Unique identifier/hash of transaction
    using ID = uint256;

    /** Constructor

        @param txn The transaction to wrap
    */
    RCLCxTx(boost::intrusive_ptr<SHAMapItem const> txn) : tx(std::move(txn))
    {
    }

    //! The unique identifier/hash of the transaction
    [[nodiscard]] ID const&
    id() const
    {
        return tx->key();
    }

    //! The SHAMapItem that represents the transaction.
    boost::intrusive_ptr<SHAMapItem const> tx;
};

/** Represents a set of transactions in RCLConsensus.

    RCLTxSet is a thin wrapper over a SHAMap that stores the set of
    transactions.
*/
class RCLTxSet
{
public:
    //! Unique identifier/hash of the set of transactions
    using ID = uint256;
    //! The type that corresponds to a single transaction
    using Tx = RCLCxTx;

    //< Provide a mutable view of a TxSet
    class MutableTxSet
    {
        friend class RCLTxSet;
        //! The SHAMap representing the transactions.
        std::shared_ptr<SHAMap> map_;

    public:
        MutableTxSet(RCLTxSet const& src) : map_{src.map->snapShot(true)}
        {
        }

        /** Insert a new transaction into the set.

        @param t The transaction to insert.
        @return Whether the transaction took place.
        */
        bool
        insert(Tx const& t)
        {
            return map_->addItem(SHAMapNodeType::TnTransactionNm, t.tx);
        }

        /** Remove a transaction from the set.

        @param entry The ID of the transaction to remove.
        @return Whether the transaction was removed.
        */
        bool
        erase(Tx::ID const& entry)
        {
            return map_->delItem(entry);
        }
    };

    /** Constructor

        @param m SHAMap to wrap
    */
    RCLTxSet(std::shared_ptr<SHAMap> m) : map{std::move(m)}
    {
        XRPL_ASSERT(map, "xrpl::RCLTxSet::MutableTxSet::RCLTxSet : non-null input");
    }

    /** Constructor from a previously created MutableTxSet

        @param m MutableTxSet that will become fixed
     */
    RCLTxSet(MutableTxSet const& m) : map{m.map_->snapShot(false)}
    {
    }

    /** Test if a transaction is in the set.

        @param entry The ID of transaction to test.
        @return Whether the transaction is in the set.
    */
    [[nodiscard]] bool
    exists(Tx::ID const& entry) const
    {
        return map->hasItem(entry);
    }

    /** Lookup a transaction.

        @param entry The ID of the transaction to find.
        @return A shared pointer to the SHAMapItem.

        @note Since find may not succeed, this returns a
              `std::shared_ptr<const SHAMapItem>` rather than a Tx, which
              cannot refer to a missing transaction.  The generic consensus
              code uses the shared_ptr semantics to know whether the find
              was successful and properly creates a Tx as needed.
    */
    [[nodiscard]] boost::intrusive_ptr<SHAMapItem const> const&
    find(Tx::ID const& entry) const
    {
        return map->peekItem(entry);
    }

    //! The unique ID/hash of the transaction set
    [[nodiscard]] ID
    id() const
    {
        return map->getHash().asUint256();
    }

    /** Find transactions not in common between this and another transaction
       set.

        @param j The set to compare with
        @return Map of transactions in this set and `j` but not both. The key
                is the transaction ID and the value is a bool of the transaction
                exists in this set.
    */
    [[nodiscard]] std::map<Tx::ID, bool>
    compare(RCLTxSet const& j) const
    {
        SHAMap::Delta delta;

        // Bound the work we do in case of a malicious
        // map from a trusted validator
        map->compare(*(j.map), delta, 65536);

        std::map<uint256, bool> ret;
        for (auto const& [k, v] : delta)
        {
            XRPL_ASSERT(
                (v.first && !v.second) || (v.second && !v.first),
                "xrpl::RCLTxSet::compare : either side is set");

            ret[k] = static_cast<bool>(v.first);
        }
        return ret;
    }

    //! The SHAMap representing the transactions.
    std::shared_ptr<SHAMap> map;
};
}  // namespace xrpl
