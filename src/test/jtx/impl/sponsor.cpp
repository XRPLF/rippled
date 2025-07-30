//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <test/jtx/sponsor.h>
#include <test/jtx/utility.h>

#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

namespace sponsor {

Json::Value
transferAccount(jtx::Account const& account)
{
    Json::Value jv;
    jv[jss::TransactionType] = "SponsorTransfer";
    jv[sfSponsor.jsonName] = account.human();

    return jv;
}

Json::Value
transferObject(uint256 const& id)
{
    Json::Value jv;
    jv[jss::TransactionType] = "SponsorTransfer";
    jv[sfLedgerIndex.jsonName] = to_string(id);
    return jv;
}

void
as::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfSponsor.jsonName][sfAccount.jsonName] = sponsor_.human();
    jt.jv[sfSponsor.jsonName][sfFlags.jsonName] = flags;
}

void
sig::operator()(Env& env, JTx& jt) const
{
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

    jt.jv[sfSponsor.jsonName][sfAccount.jsonName] = signer.acct.human();
    jt.jv[sfSponsor.jsonName][sfSigningPubKey.jsonName] =
        strHex(signer.sig.pk().slice());

    Serializer ss;
    ss.add32(HashPrefix::txSign);
    parse(jt.jv).addWithoutSigningFields(ss);
    auto const sig =
        ripple::sign(signer.acct.pk(), signer.acct.sk(), ss.slice());
    jt.jv[sfSponsor.jsonName][jss::TxnSignature] =
        strHex(Slice{sig.data(), sig.size()});
}

void
msig::operator()(Env& env, JTx& jt) const
{
    auto const mySigners = signers;
    jt.signer = [mySigners, &env](Env&, JTx& jtx) {
        jtx[sfSponsor.getJsonName()][sfSigningPubKey.getJsonName()] = "";
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
        auto& js = jtx[sfSponsor.getJsonName()][sfSigners.getJsonName()];
        for (std::size_t i = 0; i < mySigners.size(); ++i)
        {
            auto const& e = mySigners[i];
            auto& jo = js[i][sfSigner.getJsonName()];
            jo[jss::Account] = e.acct.human();
            jo[jss::SigningPubKey] = strHex(e.sig.pk().slice());

            Serializer ss{buildMultiSigningData(*st, e.acct.id())};
            auto const sig = ripple::sign(
                *publicKeyType(e.sig.pk().slice()), e.sig.sk(), ss.slice());
            jo[sfTxnSignature.getJsonName()] =
                strHex(Slice{sig.data(), sig.size()});
        }
    };
}

}  // namespace sponsor
}  // namespace jtx
}  // namespace test
}  // namespace ripple
