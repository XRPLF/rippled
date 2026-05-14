/** @file
 *  Implements `CanonicalTXSet`: the deterministically ordered transaction queue
 *  used to retry deferred transactions between consensus passes.
 *
 *  The ordering guarantee — identical iteration order on every validator for
 *  a given salt — is the mechanism that lets the network converge to the same
 *  ledger state when replaying retried transactions.
 */
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

/** Three-level composite ordering for `CanonicalTXSet::Key`.
 *
 *  Compares in the order: salted account ID → `SeqProxy` → transaction ID.
 *  This groups all transactions from the same account contiguously, orders
 *  them within the account by `SeqProxy` (sequences always sort before tickets,
 *  regardless of numeric value), and uses the transaction hash as a
 *  deterministic tiebreaker.
 *
 *  @note The `SeqProxy` ordering enforces the dependency that a
 *      `TicketCreate` transaction (sequence-based) must appear before any
 *      transaction that consumes one of its tickets.
 */
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
    uint256 ret = beast::kZERO;
    memcpy(ret.begin(), account.begin(), account.size());
    ret ^= salt_;
    return ret;
}

void
CanonicalTXSet::insert(std::shared_ptr<STTx const> const& txn)
{
    map_.insert(
        std::make_pair(
            Key(accountKey(txn->getAccountID(sfAccount)),
                txn->getSeqProxy(),
                txn->getTransactionID()),
            txn));
}

std::shared_ptr<STTx const>
CanonicalTXSet::popAcctTransaction(std::shared_ptr<STTx const> const& tx)
{
    std::shared_ptr<STTx const> result;
    uint256 const effectiveAccount{accountKey(tx->getAccountID(sfAccount))};

    auto const seqProxy = tx->getSeqProxy();
    Key const after(effectiveAccount, seqProxy, beast::kZERO);
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
