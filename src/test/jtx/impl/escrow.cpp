#include <test/jtx/escrow.h>

#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

/** Escrow operations. */
namespace escrow {

Json::Value
create(AccountID const& account, AccountID const& to, STAmount const& amount)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::EscrowCreate;
    jv[jss::Flags] = tfFullyCanonicalSig;
    jv[jss::Account] = to_string(account);
    jv[jss::Destination] = to_string(to);
    jv[jss::Amount] = amount.getJson(JsonOptions::none);
    return jv;
}

Json::Value
finish(AccountID const& account, AccountID const& from, std::uint32_t seq)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::EscrowFinish;
    jv[jss::Flags] = tfFullyCanonicalSig;
    jv[jss::Account] = to_string(account);
    jv[sfOwner.jsonName] = to_string(from);
    jv[sfOfferSequence.jsonName] = seq;
    return jv;
}

Json::Value
cancel(AccountID const& account, Account const& from, std::uint32_t seq)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::EscrowCancel;
    jv[jss::Flags] = tfFullyCanonicalSig;
    jv[jss::Account] = to_string(account);
    jv[sfOwner.jsonName] = from.human();
    jv[sfOfferSequence.jsonName] = seq;
    return jv;
}

Rate
rate(Env& env, Account const& account, std::uint32_t const& seq)
{
    auto const sle = env.le(keylet::escrow(account.id(), seq));
    if (sle->isFieldPresent(sfTransferRate))
        return ripple::Rate((*sle)[sfTransferRate]);
    return Rate{0};
}

}  // namespace escrow

}  // namespace jtx

}  // namespace test
}  // namespace ripple
