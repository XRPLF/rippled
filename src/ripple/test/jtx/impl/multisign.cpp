//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <ripple/test/jtx/multisign.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/JsonFields.h>

namespace ripple {
namespace test {
namespace jtx {

Json::Value
signers (Account const& account,
    std::uint32_t quorum,
        std::vector<signer> const& v)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = "SignerListSet";
    jv["SignerQuorum"] = quorum;
    auto& ja = jv["SignerEntries"];
    ja.resize(v.size());
    for(std::size_t i = 0; i < v.size(); ++i)
    {
        auto const& e = v[i];
        auto& je = ja[i]["SignerEntry"];
        je[jss::Account] = e.account.human();
        je["SignerWeight"] = e.weight;
    }
    return jv;
}

Json::Value
signers (Account const& account, none_t)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = "SignerListSet";
    return jv;
}

//------------------------------------------------------------------------------

void
msig::operator()(Env const& env, JTx& jt) const
{
    // VFALCO Inefficient pre-C++14
    auto accounts = accounts_;
    std::sort(accounts.begin(), accounts.end(),
        [](Account const& lhs, Account const& rhs)
        {
            return lhs.id() < rhs.id();
        });
    jt.signer = [accounts, &env](Env&, JTx& jt)
    {
        jt["SigningPubKey"] = "";
        boost::optional<STObject> st;
        try
        {
            st = parse(jt.jv);
        }
        catch(parse_error const&)
        {
            env.test.log << pretty(jt.jv);
            throw;
        }
        auto const signingForID = [](Json::Value const& jv)
            {
                RippleAddress ra;
                ra.setAccountID(jv[jss::Account].asString());
                return ra.getAccountID();
            }(jt.jv);
        auto& jv = jt["MultiSigners"][0u]["SigningFor"];
        jv[jss::Account] = jt[jss::Account];
        auto& js = jv["SigningAccounts"];
        js.resize(accounts.size());
        for(std::size_t i = 0; i < accounts.size(); ++i)
        {
            auto const& e = accounts[i];
            auto& jo = js[i]["SigningAccount"];
            jo[jss::Account] = e.human();
            jo[jss::SigningPubKey] = strHex(make_Slice(
                e.pk().getAccountPublic()));

            Serializer ss;
            ss.add32 (HashPrefix::txMultiSign);
            st->addWithoutSigningFields(ss);
            ss.add160(signingForID);
            ss.add160(e.id());
            jo["MultiSignature"] = strHex(make_Slice(
                e.sk().accountPrivateSign(ss.getData())));

        }
    };
}

msig2_t::msig2_t (std::vector<std::pair<
    Account, Account>> sigs)
{
    for (auto& sig : sigs)
    {
        auto result = sigs_.emplace(
            std::piecewise_construct,
                std::make_tuple(std::move(sig.first)),
                    std::make_tuple());
        result.first->second.emplace(
            std::move(sig.second));
    }
}

void
msig2_t::operator()(Env const& env, JTx& jt) const
{
    // VFALCO Inefficient pre-C++14
    auto const sigs = sigs_;
    jt.signer = [sigs, &env](Env&, JTx& jt)
    {
        jt["SigningPubKey"] = "";
        boost::optional<STObject> st;
        try
        {
            st = parse(jt.jv);
        }
        catch(parse_error const&)
        {
            env.test.log << pretty(jt.jv);
            throw;
        }
        auto& ja = jt["MultiSigners"];
        ja.resize(sigs.size());
        for (auto i = std::make_pair(0, sigs.begin());
            i.first < sigs.size(); ++i.first, ++i.second)
        {
            auto const& sign_for = i.second->first;
            auto const& list = i.second->second;
            auto& ji = ja[i.first]["SigningFor"];
            ji[jss::Account] = sign_for.human();
            auto& js = ji["SigningAccounts"];
            js.resize(list.size());
            for (auto j = std::make_pair(0, list.begin());
                j.first < list.size(); ++j.first, ++j.second)
            {
                auto& jj = js[j.first]["SigningAccount"];
                jj[jss::Account] = j.second->human();
                jj[jss::SigningPubKey] = strHex(make_Slice(
                    j.second->pk().getAccountPublic()));

                Serializer ss;
                ss.add32 (HashPrefix::txMultiSign);
                st->addWithoutSigningFields(ss);
                ss.add160(sign_for.id());
                ss.add160(j.second->id());
                jj["MultiSignature"] = strHex(make_Slice(
                    j.second->sk().accountPrivateSign(
                        ss.getData())));
            }
        }
    };
}

} // jtx
} // test
} // ripple
