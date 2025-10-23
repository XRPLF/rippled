#ifndef XRPL_TEST_JTX_BATCH_H_INCLUDED
#define XRPL_TEST_JTX_BATCH_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/owners.h>
#include <test/jtx/tags.h>

#include <xrpl/protocol/TxFlags.h>

#include "test/jtx/SignerUtils.h"

#include <concepts>
#include <cstdint>
#include <optional>

namespace ripple {
namespace test {
namespace jtx {

/** Batch operations */
namespace batch {

/** Calculate Batch Fee. */
XRPAmount
calcBatchFee(
    jtx::Env const& env,
    uint32_t const& numSigners,
    uint32_t const& txns = 0);

/** Batch. */
Json::Value
outer(
    jtx::Account const& account,
    uint32_t seq,
    STAmount const& fee,
    std::uint32_t flags);

/** Adds a new Batch Txn on a JTx and autofills. */
class inner
{
private:
    Json::Value txn_;
    std::uint32_t seq_;
    std::optional<std::uint32_t> ticket_;

public:
    inner(
        Json::Value const& txn,
        std::uint32_t const& sequence,
        std::optional<std::uint32_t> const& ticket = std::nullopt,
        std::optional<std::uint32_t> const& fee = std::nullopt)
        : txn_(txn), seq_(sequence), ticket_(ticket)
    {
        txn_[jss::SigningPubKey] = "";
        txn_[jss::Sequence] = seq_;
        txn_[jss::Fee] = "0";
        txn_[jss::Flags] = txn_[jss::Flags].asUInt() | tfInnerBatchTxn;

        // Optionally set ticket sequence
        if (ticket_.has_value())
        {
            txn_[jss::Sequence] = 0;
            txn_[sfTicketSequence.jsonName] = *ticket_;
        }
    }

    void
    operator()(Env&, JTx& jtx) const;

    Json::Value&
    operator[](Json::StaticString const& key)
    {
        return txn_[key];
    }

    void
    removeMember(Json::StaticString const& key)
    {
        txn_.removeMember(key);
    }

    Json::Value const&
    getTxn() const
    {
        return txn_;
    }
};

/** Set a batch signature on a JTx. */
class sig
{
public:
    std::vector<Reg> signers;

    sig(std::vector<Reg> signers_) : signers(std::move(signers_))
    {
        sortSigners(signers);
    }

    template <class AccountType, class... Accounts>
        requires std::convertible_to<AccountType, Reg>
    explicit sig(AccountType&& a0, Accounts&&... aN)
        : signers{std::forward<AccountType>(a0), std::forward<Accounts>(aN)...}
    {
        sortSigners(signers);
    }

    void
    operator()(Env&, JTx& jt) const;
};

/** Set a batch nested multi-signature on a JTx. */
class msig
{
public:
    Account master;
    std::vector<Reg> signers;

    msig(Account const& masterAccount, std::vector<Reg> signers_)
        : master(masterAccount), signers(std::move(signers_))
    {
        sortSigners(signers);
    }

    template <class AccountType, class... Accounts>
        requires std::convertible_to<AccountType, Reg>
    explicit msig(
        Account const& masterAccount,
        AccountType&& a0,
        Accounts&&... aN)
        : master(masterAccount)
        , signers{std::forward<AccountType>(a0), std::forward<Accounts>(aN)...}
    {
        sortSigners(signers);
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace batch

}  // namespace jtx

}  // namespace test
}  // namespace ripple

#endif
