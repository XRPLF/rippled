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

#include <xrpld/app/misc/WasmHostFuncImpl.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>

#include <xrpl/protocol/digest.h>

namespace ripple {

int32_t
WasmHostFunctionsImpl::getLedgerSqn()
{
    return ctx.view().seq();
}

int32_t
WasmHostFunctionsImpl::getParentLedgerTime()
{
    return ctx.view().parentCloseTime().time_since_epoch().count();  // TODO try
}

// TODO remove json code after deciding encoding scheme

std::optional<Bytes>
WasmHostFunctionsImpl::getTxField(const std::string& fname)
{
    auto js = ctx.tx.getJson(JsonOptions::none);
    if (js.isMember(fname))
    {
        auto s = js.get(fname, Json::Value::null).asString();
        return Bytes{s.begin(), s.end()};
    }
    else
        return std::nullopt;
}

std::optional<Bytes>
WasmHostFunctionsImpl::getLedgerEntryField(
    int32_t type,
    Bytes const& kdata,
    const std::string& fname)
{
    auto kl = [&]() -> std::optional<ripple::Keylet> {
        if (type == ltACCOUNT_ROOT)
        {
            std::string s(kdata.begin(), kdata.end());
            auto const account = parseBase58<AccountID>(s);
            if (account)
            {
                return keylet::account(account.value());
            }
        }
        return std::nullopt;
    }();

    if (!kl || !ctx.view().exists(kl.value()))
        return std::nullopt;

    auto js = ctx.view().read(kl.value())->getJson(JsonOptions::none);
    if (js.isMember(fname))
    {
        auto s = js.get(fname, Json::Value::null).asString();
        return Bytes{s.begin(), s.end()};
    }
    else
        return std::nullopt;
}

std::optional<Bytes>
WasmHostFunctionsImpl::getCurrentLedgerEntryField(const std::string& fname)
{
    if (!ctx.view().exists(leKey))
        return std::nullopt;

    auto js = ctx.view().read(leKey)->getJson(JsonOptions::none);
    if (js.isMember(fname))
    {
        auto s = js.get(fname, Json::Value::null).asString();
        return Bytes{s.begin(), s.end()};
    }
    else
        return std::nullopt;
}

std::optional<Bytes>
WasmHostFunctionsImpl::getNFT(
    const std::string& account,
    const std::string& nftId)
{
    auto const accountId = parseBase58<AccountID>(account);
    if (!accountId || accountId->isZero())
    {
        return std::nullopt;
    }

    uint256 nftHash;
    if (!nftHash.parseHex(nftId))
    {
        return std::nullopt;
    }

    auto jv = nft::findToken(ctx.view(), accountId.value(), nftHash);
    if (!jv)
    {
        return std::nullopt;
    }

    Slice const s = (*jv)[sfURI];
    return Bytes{s.begin(), s.end()};
}

bool
WasmHostFunctionsImpl::updateData(const Bytes& data)
{
    if (!ctx.view().exists(leKey))
        return false;
    auto sle = ctx.view().peek(leKey);
    sle->setFieldVL(sfData, data);
    ctx.view().update(sle);
    return true;
}

Hash
WasmHostFunctionsImpl::computeSha512HalfHash(const Bytes& data)
{
    auto const hash = sha512Half(data);
    return uint256::fromVoid(hash.data());
}

std::optional<Bytes>
WasmHostFunctionsImpl::escrowKeylet(
    const std::string& account,
    const std::string& seq)
{
    auto const accountId = parseBase58<AccountID>(account);
    if (!accountId || accountId->isZero())
    {
        return std::nullopt;
    }

    std::uint32_t seqNum;
    try
    {
        seqNum = static_cast<std::uint32_t>(std::stoul(seq));
    }
    catch (const std::exception& e)
    {
        return std::nullopt;
    }

    auto keylet = keylet::escrow(*accountId, seqNum).key;
    if (!keylet)
    {
        return std::nullopt;
    }

    return Bytes{keylet.begin(), keylet.end()};
}
}  // namespace ripple
