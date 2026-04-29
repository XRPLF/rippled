#include <test/jtx/multisign.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/tags.h>
#include <test/jtx/utility.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/jss.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <vector>

namespace xrpl::test::jtx {

Json::Value
signers(Account const& account, std::uint32_t quorum, std::vector<signer> const& v)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = jss::SignerListSet;
    jv[sfSignerQuorum.getJsonName()] = quorum;
    auto& ja = jv[sfSignerEntries.getJsonName()];
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        auto const& e = v[i];
        auto& je = ja[i][sfSignerEntry.getJsonName()];
        je[jss::Account] = e.account.human();
        je[sfSignerWeight.getJsonName()] = e.weight;
        if (e.tag)
            je[sfWalletLocator.getJsonName()] = to_string(*e.tag);
    }
    return jv;
}

Json::Value
signers(Account const& account, none_t)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = jss::SignerListSet;
    jv[sfSignerQuorum.getJsonName()] = 0;
    return jv;
}

//------------------------------------------------------------------------------

void
msig::operator()(Env& env, JTx& jt) const
{
    auto const mySigners = signers;
    auto callback = [subField = subField, mySigners, &env](Env&, JTx& jtx) {
        // Where to put the signature. Supports sfCounterPartySignature.
        auto& sigObject = subField ? jtx[*subField] : jtx.jv;

        // The signing pub key is only required at the top level.
        if (!subField)
        {
            sigObject[sfSigningPubKey] = "";
        }
        else if (sigObject.isNull())
        {
            sigObject = Json::Value(Json::objectValue);
        }
        std::optional<STObject> st;
        try
        {
            st = parse(jtx.jv);
        }
        catch (parse_error const&)
        {
            env.test.log << pretty(jtx.jv) << std::endl;
            Rethrow();
        }
        auto& js = sigObject[sfSigners];
        for (std::size_t i = 0; i < mySigners.size(); ++i)
        {
            auto const& e = mySigners[i];
            auto& jo = js[i][sfSigner.getJsonName()];
            jo[jss::Account] = e.acct.human();
            jo[jss::SigningPubKey] = strHex(e.sig.pk().slice());

            Serializer const ss{buildMultiSigningData(*st, e.acct.id())};
            auto const sig = xrpl::sign(*publicKeyType(e.sig.pk().slice()), e.sig.sk(), ss.slice());
            jo[sfTxnSignature.getJsonName()] = strHex(Slice{sig.data(), sig.size()});
        }
    };
    if (subField == nullptr)
    {
        jt.mainSigners.emplace_back(callback);
    }
    else
    {
        jt.postSigners.emplace_back(callback);
    }
}

}  // namespace xrpl::test::jtx
