#include <test/jtx/batch.h>
#include <test/jtx/utility.h>

#include <xrpl/protocol/Batch.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/jss.h>

#include <optional>
#include <sstream>

namespace ripple {
namespace test {
namespace jtx {

namespace batch {

XRPAmount
calcBatchFee(
    test::jtx::Env const& env,
    uint32_t const& numSigners,
    uint32_t const& txns)
{
    XRPAmount const feeDrops = env.current()->fees().base;
    return ((numSigners + 2) * feeDrops) + feeDrops * txns;
}

// Batch.
Json::Value
outer(
    jtx::Account const& account,
    uint32_t seq,
    STAmount const& fee,
    std::uint32_t flags)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::Batch;
    jv[jss::Account] = account.human();
    jv[jss::RawTransactions] = Json::Value{Json::arrayValue};
    jv[jss::Sequence] = seq;
    jv[jss::Flags] = flags;
    jv[jss::Fee] = to_string(fee);
    return jv;
}

void
inner::operator()(Env& env, JTx& jt) const
{
    auto const index = jt.jv[jss::RawTransactions].size();
    Json::Value& batchTransaction = jt.jv[jss::RawTransactions][index];

    // Initialize the batch transaction
    batchTransaction = Json::Value{};
    batchTransaction[jss::RawTransaction] = txn_;
}

void
sig::operator()(Env& env, JTx& jt) const
{
    auto const mySigners = signers;
    std::optional<STObject> st;
    try
    {
        // required to cast the STObject to STTx
        jt.jv[jss::SigningPubKey] = "";
        st = parse(jt.jv);
    }
    catch (parse_error const&)
    {
        env.test.log << pretty(jt.jv) << std::endl;
        Rethrow();
    }
    STTx const& stx = STTx{std::move(*st)};
    auto& js = jt[sfBatchSigners.getJsonName()];
    for (std::size_t i = 0; i < mySigners.size(); ++i)
    {
        auto const& e = mySigners[i];
        auto& jo = js[i][sfBatchSigner.getJsonName()];
        jo[jss::Account] = e.acct.human();
        jo[jss::SigningPubKey] = strHex(e.sig.pk().slice());

        Serializer msg;
        serializeBatch(msg, stx.getFlags(), stx.getBatchTransactionIDs());
        auto const sig = ripple::sign(
            *publicKeyType(e.sig.pk().slice()), e.sig.sk(), msg.slice());
        jo[sfTxnSignature.getJsonName()] =
            strHex(Slice{sig.data(), sig.size()});
    }
}

void
msig::operator()(Env& env, JTx& jt) const
{
    auto const mySigners = signers;
    std::optional<STObject> st;
    try
    {
        // required to cast the STObject to STTx
        jt.jv[jss::SigningPubKey] = "";
        st = parse(jt.jv);
    }
    catch (parse_error const&)
    {
        env.test.log << pretty(jt.jv) << std::endl;
        Rethrow();
    }
    STTx const& stx = STTx{std::move(*st)};
    auto& bs = jt[sfBatchSigners.getJsonName()];
    auto const index = jt[sfBatchSigners.jsonName].size();
    auto& bso = bs[index][sfBatchSigner.getJsonName()];
    bso[jss::Account] = master.human();
    bso[jss::SigningPubKey] = "";
    auto& is = bso[sfSigners.getJsonName()];
    for (std::size_t i = 0; i < mySigners.size(); ++i)
    {
        auto const& e = mySigners[i];
        auto& iso = is[i][sfSigner.getJsonName()];
        iso[jss::Account] = e.acct.human();
        iso[jss::SigningPubKey] = strHex(e.sig.pk().slice());

        Serializer msg;
        serializeBatch(msg, stx.getFlags(), stx.getBatchTransactionIDs());
        finishMultiSigningData(e.acct.id(), msg);
        auto const sig = ripple::sign(
            *publicKeyType(e.sig.pk().slice()), e.sig.sk(), msg.slice());
        iso[sfTxnSignature.getJsonName()] =
            strHex(Slice{sig.data(), sig.size()});
    }
}

}  // namespace batch

}  // namespace jtx
}  // namespace test
}  // namespace ripple
