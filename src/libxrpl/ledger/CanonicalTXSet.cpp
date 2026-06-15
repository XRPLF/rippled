#include <xrpl/ledger/CanonicalTXSet.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>

#include <cstring>
#include <memory>
#include <utility>

namespace xrpl {

bool
operator<(CanonicalTXSet::Key const& lhs, CanonicalTXSet::Key const& rhs)
{
    if (lhs.account_ < rhs.account_)
        return true;

    if (lhs.account_ > rhs.account_)
        return false;

    if (lhs.seqProxy_ < rhs.seqProxy_)
        return true;

    if (lhs.seqProxy_ > rhs.seqProxy_)
        return false;

    return lhs.txId_ < rhs.txId_;
}

uint256
CanonicalTXSet::accountKey(AccountID const& account)
{
    uint256 ret = beast::kZero;
    memcpy(ret.begin(), account.begin(), account.size());
    ret ^= salt_;
    return ret;
}

void
CanonicalTXSet::insert(std::shared_ptr<STTx const> txn)
{
    Key key(accountKey(txn->getAccountID(sfAccount)), txn->getSeqProxy(), txn->getTransactionID());
    map_.emplace(key, std::move(txn));
}

std::shared_ptr<STTx const>
CanonicalTXSet::popAcctTransaction(std::shared_ptr<STTx const> const& tx)
{
    // Determining the next viable transaction for an account with Tickets:
    //
    //  1. Prioritize transactions with Sequences over transactions with
    //     Tickets.
    //
    //  2. For transactions not using Tickets, look for consecutive Sequence
    //     numbers. For transactions using Tickets, don't worry about
    //     consecutive Sequence numbers. Tickets can process out of order.
    //
    //  3. After handling all transactions with Sequences, return Tickets
    //     with the lowest Ticket ID first.
    std::shared_ptr<STTx const> result;
    uint256 const effectiveAccount{accountKey(tx->getAccountID(sfAccount))};

    auto const seqProxy = tx->getSeqProxy();
    Key const after(effectiveAccount, seqProxy, beast::kZero);
    auto const itrNext{map_.lower_bound(after)};
    if (itrNext != map_.end() && itrNext->first.getAccount() == effectiveAccount &&
        (!itrNext->second->getSeqProxy().isSeq() ||
         itrNext->second->getSeqProxy().value() == seqProxy.value() + 1))
    {
        result = std::move(itrNext->second);
        map_.erase(itrNext);
    }

    return result;
}

}  // namespace xrpl
