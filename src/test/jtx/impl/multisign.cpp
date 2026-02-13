#include <test/jtx/multisign.h>
#include <test/jtx/utility.h>

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/jss.h>

#include <optional>

namespace xrpl {
namespace test {
namespace jtx {

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

static void
sortSignersRecursive(std::vector<std::shared_ptr<Reg>>& signers)
{
    std::sort(signers.begin(), signers.end(), [](auto const& lhs, auto const& rhs) { return lhs->id() < rhs->id(); });

    for (auto& signer : signers)
        if (signer->isNested())
            sortSignersRecursive(signer->nested);
}

// Primary constructor — everything delegates here
msig::msig(SField const* subField_, std::vector<std::shared_ptr<Reg>> signers_)
    : signers(std::move(signers_)), subField(subField_)
{
    sortSignersRecursive(signers);
}

msig::msig(SField const* subField_, std::vector<Reg> signers_) : subField(subField_)
{
    signers.reserve(signers_.size());
    for (auto& s : signers_)
        signers.push_back(std::make_shared<Reg>(s));
    sortSignersRecursive(signers);
}

void
msig::operator()(Env& env, JTx& jt) const
{
    auto const mySigners = signers;
    auto callback = [subField = subField, mySigners, &env](Env&, JTx& jtx) {
        auto& sigObject = subField ? jtx[*subField] : jtx.jv;

        if (!subField)
            sigObject[sfSigningPubKey] = "";
        else if (sigObject.isNull())
            sigObject = Json::Value(Json::objectValue);

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

        std::function<Json::Value(std::shared_ptr<Reg> const&)> buildSignerJson;
        buildSignerJson = [&](std::shared_ptr<Reg> const& signer) -> Json::Value {
            Json::Value jo;
            jo[jss::Account] = signer->acct.human();

            if (signer->isNested())
            {
                auto& subJs = jo[sfSigners.getJsonName()];
                for (std::size_t i = 0; i < signer->nested.size(); ++i)
                {
                    subJs[i][sfSigner.getJsonName()] = buildSignerJson(signer->nested[i]);
                }
            }
            else
            {
                jo[jss::SigningPubKey] = strHex(signer->sig->pk().slice());

                Serializer ss{buildMultiSigningData(*st, signer->acct.id())};
                auto const sig = xrpl::sign(*publicKeyType(signer->sig->pk().slice()), signer->sig->sk(), ss.slice());
                jo[sfTxnSignature.getJsonName()] = strHex(Slice{sig.data(), sig.size()});
            }

            return jo;
        };

        auto& js = sigObject[sfSigners];
        for (std::size_t i = 0; i < mySigners.size(); ++i)
        {
            js[i][sfSigner.getJsonName()] = buildSignerJson(mySigners[i]);
        }
    };

    if (!subField)
        jt.mainSigners.emplace_back(callback);
    else
        jt.postSigners.emplace_back(callback);
}

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
