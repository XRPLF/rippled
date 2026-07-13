#include <test/jtx/paychan.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/PayChan.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <optional>

/**
 * Paychan operations.
 */
namespace xrpl::test::jtx::paychan {

json::Value
create(
    AccountID const& account,
    AccountID const& to,
    STAmount const& amount,
    NetClock::duration const& settleDelay,
    PublicKey const& pk,
    std::optional<NetClock::time_point> const& cancelAfter,
    std::optional<std::uint32_t> const& dstTag)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::PaymentChannelCreate;
    jv[jss::Flags] = tfFullyCanonicalSig;
    jv[jss::Account] = to_string(account);
    jv[jss::Destination] = to_string(to);
    jv[jss::Amount] = amount.getJson(JsonOptions::Values::None);
    jv[jss::SettleDelay] = settleDelay.count();
    jv[sfPublicKey.fieldName] = strHex(pk.slice());
    if (cancelAfter)
        jv[sfCancelAfter.fieldName] = cancelAfter->time_since_epoch().count();
    if (dstTag)
        jv[sfDestinationTag.fieldName] = *dstTag;
    return jv;
}

json::Value
fund(
    AccountID const& account,
    uint256 const& channel,
    STAmount const& amount,
    std::optional<NetClock::time_point> const& expiration)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::PaymentChannelFund;
    jv[jss::Flags] = tfFullyCanonicalSig;
    jv[jss::Account] = to_string(account);
    jv[sfChannel.fieldName] = to_string(channel);
    jv[jss::Amount] = amount.getJson(JsonOptions::Values::None);
    if (expiration)
        jv[sfExpiration.fieldName] = expiration->time_since_epoch().count();
    return jv;
}

json::Value
claim(
    AccountID const& account,
    uint256 const& channel,
    std::optional<STAmount> const& balance,
    std::optional<STAmount> const& amount,
    std::optional<Slice> const& signature,
    std::optional<PublicKey> const& pk)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::PaymentChannelClaim;
    jv[jss::Flags] = tfFullyCanonicalSig;
    jv[jss::Account] = to_string(account);
    jv["Channel"] = to_string(channel);
    if (amount)
        jv[jss::Amount] = amount->getJson(JsonOptions::Values::None);
    if (balance)
        jv["Balance"] = balance->getJson(JsonOptions::Values::None);
    if (signature)
        jv["Signature"] = strHex(*signature);
    if (pk)
        jv["PublicKey"] = strHex(pk->slice());
    return jv;
}

uint256
channel(AccountID const& account, AccountID const& dst, std::uint32_t seqProxyValue)
{
    auto const k = keylet::payChannel(account, dst, seqProxyValue);
    return k.key;
}

STAmount
channelBalance(ReadView const& view, uint256 const& chan)
{
    auto const slep = view.read({ltPAYCHAN, chan});
    if (!slep)
        return XRPAmount{-1};
    return (*slep)[sfBalance];
}

STAmount
channelAmount(ReadView const& view, uint256 const& chan)
{
    auto const slep = view.read({ltPAYCHAN, chan});
    if (!slep)
        return XRPAmount{-1};
    return (*slep)[sfAmount];
}

bool
channelExists(ReadView const& view, uint256 const& chan)
{
    auto const slep = view.read({ltPAYCHAN, chan});
    return bool(slep);
}

Buffer
signClaimAuth(
    PublicKey const& pk,
    SecretKey const& sk,
    uint256 const& channel,
    STAmount const& authAmt)
{
    Serializer msg;
    serializePayChanAuthorization(msg, channel, authAmt);
    return sign(pk, sk, msg.slice());
}

Rate
rate(Env& env, Account const& account, Account const& dest, std::uint32_t const& seq)
{
    auto const sle = env.le(keylet::payChannel(account.id(), dest.id(), seq));
    if (sle->isFieldPresent(sfTransferRate))
        return xrpl::Rate((*sle)[sfTransferRate]);
    return Rate{0};
}

}  // namespace xrpl::test::jtx::paychan
