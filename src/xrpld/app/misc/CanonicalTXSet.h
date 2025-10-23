#ifndef XRPL_APP_MISC_CANONICALTXSET_H_INCLUDED
#define XRPL_APP_MISC_CANONICALTXSET_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/RippleLedgerHash.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>

namespace ripple {

/** Holds transactions which were deferred to the next pass of consensus.

    "Canonical" refers to the order in which transactions are applied.

    - Puts transactions from the same account in SeqProxy order

*/
// VFALCO TODO rename to SortedTxSet
class CanonicalTXSet : public CountedObject<CanonicalTXSet>
{
private:
    class Key
    {
    public:
        Key(uint256 const& account, SeqProxy seqProx, uint256 const& id)
            : account_(account), txId_(id), seqProxy_(seqProx)
        {
        }

        friend bool
        operator<(Key const& lhs, Key const& rhs);

        inline friend bool
        operator>(Key const& lhs, Key const& rhs)
        {
            return rhs < lhs;
        }

        inline friend bool
        operator<=(Key const& lhs, Key const& rhs)
        {
            return !(lhs > rhs);
        }

        inline friend bool
        operator>=(Key const& lhs, Key const& rhs)
        {
            return !(lhs < rhs);
        }

        inline friend bool
        operator==(Key const& lhs, Key const& rhs)
        {
            return lhs.txId_ == rhs.txId_;
        }

        inline friend bool
        operator!=(Key const& lhs, Key const& rhs)
        {
            return !(lhs == rhs);
        }

        uint256 const&
        getAccount() const
        {
            return account_;
        }

        uint256 const&
        getTXID() const
        {
            return txId_;
        }

    private:
        uint256 account_;
        uint256 txId_;
        SeqProxy seqProxy_;
    };

    friend bool
    operator<(Key const& lhs, Key const& rhs);

    // Calculate the salted key for the given account
    uint256
    accountKey(AccountID const& account);

public:
    using const_iterator =
        std::map<Key, std::shared_ptr<STTx const>>::const_iterator;

public:
    explicit CanonicalTXSet(LedgerHash const& saltHash) : salt_(saltHash)
    {
    }

    void
    insert(std::shared_ptr<STTx const> const& txn);

    // Pops the next transaction on account that follows seqProx in the
    // sort order.  Normally called when a transaction is successfully
    // applied to the open ledger so the next transaction can be resubmitted
    // without waiting for ledger close.
    //
    // The return value is often null, when an account has no more
    // transactions.
    std::shared_ptr<STTx const>
    popAcctTransaction(std::shared_ptr<STTx const> const& tx);

    void
    reset(LedgerHash const& salt)
    {
        salt_ = salt;
        map_.clear();
    }

    const_iterator
    erase(const_iterator const& it)
    {
        return map_.erase(it);
    }

    const_iterator
    begin() const
    {
        return map_.begin();
    }

    const_iterator
    end() const
    {
        return map_.end();
    }

    size_t
    size() const
    {
        return map_.size();
    }
    bool
    empty() const
    {
        return map_.empty();
    }

    uint256 const&
    key() const
    {
        return salt_;
    }

private:
    std::map<Key, std::shared_ptr<STTx const>> map_;

    // Used to salt the accounts so people can't mine for low account numbers
    uint256 salt_;
};

}  // namespace ripple

#endif
