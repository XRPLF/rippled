//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <test/jtx/mpt.h>

#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

void
mptflags::operator()(Env& env) const
{
    env.test.expect(tester_.checkFlags(flags_, holder_));
}

void
mptpay::operator()(Env& env) const
{
    env.test.expect(amount_ == tester_.getAmount(account_));
}

void
requireAny::operator()(Env& env) const
{
    env.test.expect(cb_());
}

std::unordered_map<std::string, AccountP>
MPTTester::makeHolders(std::vector<AccountP> const& holders)
{
    std::unordered_map<std::string, AccountP> accounts;
    for (auto const& h : holders)
    {
        assert(h && holders_.find(h->human()) == accounts.cend());
        accounts.emplace(h->human(), h);
    }
    return accounts;
}

MPTTester::MPTTester(Env& env, Account const& issuer, MPTConstr const& arg)
    : env_(env)
    , issuer_(issuer)
    , holders_(makeHolders(arg.holders))
    , close_(arg.close)
{
    if (arg.fund)
    {
        env_.fund(arg.xrp, issuer_);
        for (auto it : holders_)
            env_.fund(arg.xrpHolders, *it.second);
    }
    if (close_)
        env.close();
    if (arg.fund)
    {
        env_.require(owners(issuer_, 0));
        for (auto it : holders_)
        {
            assert(issuer_.id() != it.second->id());
            env_.require(owners(*it.second, 0));
        }
    }
}

void
MPTTester::create(const MPTCreate& arg)
{
    if (issuanceKey_)
        Throw<std::runtime_error>("MPT can't be reused");
    id_ = getMptID(issuer_.id(), env_.seq(issuer_));
    issuanceKey_ = keylet::mptIssuance(*id_).key;
    Json::Value jv;
    jv[sfAccount.jsonName] = issuer_.human();
    jv[sfTransactionType.jsonName] = jss::MPTokenIssuanceCreate;
    if (arg.assetScale)
        jv[sfAssetScale.jsonName] = *arg.assetScale;
    if (arg.transferFee)
        jv[sfTransferFee.jsonName] = *arg.transferFee;
    if (arg.metadata)
        jv[sfMPTokenMetadata.jsonName] = strHex(*arg.metadata);
    if (arg.maxAmt)
        jv[sfMaximumAmount.jsonName] = *arg.maxAmt;
    if (submit(arg, jv) != tesSUCCESS)
    {
        // Verify issuance doesn't exist
        env_.require(requireAny([&]() -> bool {
            return env_.le(keylet::mptIssuance(*id_)) == nullptr;
        }));

        id_.reset();
        issuanceKey_.reset();
    }
    else if (arg.flags)
        env_.require(mptflags(*this, *arg.flags));
}

void
MPTTester::destroy(MPTDestroy const& arg)
{
    Json::Value jv;
    if (arg.issuer)
        jv[sfAccount.jsonName] = arg.issuer->human();
    else
        jv[sfAccount.jsonName] = issuer_.human();
    if (arg.id)
        jv[sfMPTokenIssuanceID.jsonName] = to_string(*arg.id);
    else
    {
        assert(id_);
        jv[sfMPTokenIssuanceID.jsonName] = to_string(*id_);
    }
    jv[sfTransactionType.jsonName] = jss::MPTokenIssuanceDestroy;
    submit(arg, jv);
}

Account const&
MPTTester::holder(std::string const& holder_) const
{
    auto const& it = holders_.find(holder_);
    assert(it != holders_.cend());
    if (it == holders_.cend())
        Throw<std::runtime_error>("Holder is not found");
    return *it->second;
}

void
MPTTester::authorize(MPTAuthorize const& arg)
{
    Json::Value jv;
    if (arg.account)
        jv[sfAccount.jsonName] = arg.account->human();
    else
        jv[sfAccount.jsonName] = issuer_.human();
    jv[sfTransactionType.jsonName] = jss::MPTokenAuthorize;
    if (arg.id)
        jv[sfMPTokenIssuanceID.jsonName] = to_string(*arg.id);
    else
    {
        assert(id_);
        jv[sfMPTokenIssuanceID.jsonName] = to_string(*id_);
    }
    if (arg.holder)
        jv[sfMPTokenHolder.jsonName] = arg.holder->human();
    if (auto const result = submit(arg, jv); result == tesSUCCESS)
    {
        // Issuer authorizes
        if (arg.account == nullptr || *arg.account == issuer_)
        {
            auto const flags = getFlags(arg.holder);
            // issuer un-authorizes the holder
            if (arg.flags.value_or(0) == tfMPTUnauthorize)
                env_.require(mptflags(*this, flags, arg.holder));
            // issuer authorizes the holder
            else
                env_.require(
                    mptflags(*this, flags | lsfMPTAuthorized, arg.holder));
        }
        // Holder authorizes
        else if (arg.flags.value_or(0) == 0)
        {
            auto const flags = getFlags(arg.account);
            // holder creates a token
            env_.require(mptflags(*this, flags, arg.account));
            env_.require(mptpay(*this, *arg.account, 0));
        }
    }
    else if (
        arg.account != nullptr && *arg.account != issuer_ &&
        arg.flags.value_or(0) == 0 && issuanceKey_)
    {
        if (result == tecMPTOKEN_EXISTS)
        {
            // Verify that MPToken already exists
            env_.require(requireAny([&]() -> bool {
                return env_.le(keylet::mptoken(
                           *issuanceKey_, arg.account->id())) != nullptr;
            }));
        }
        else
        {
            // Verify MPToken doesn't exist if holder failed authorizing(unless
            // it already exists)
            env_.require(requireAny([&]() -> bool {
                return env_.le(keylet::mptoken(
                           *issuanceKey_, arg.account->id())) == nullptr;
            }));
        }
    }
}

void
MPTTester::set(MPTSet const& arg)
{
    Json::Value jv;
    if (arg.account)
        jv[sfAccount.jsonName] = arg.account->human();
    else
        jv[sfAccount.jsonName] = issuer_.human();
    jv[sfTransactionType.jsonName] = jss::MPTokenIssuanceSet;
    if (arg.id)
        jv[sfMPTokenIssuanceID.jsonName] = to_string(*arg.id);
    else
    {
        assert(id_);
        jv[sfMPTokenIssuanceID.jsonName] = to_string(*id_);
    }
    if (arg.holder)
        jv[sfMPTokenHolder.jsonName] = arg.holder->human();
    if (submit(arg, jv) == tesSUCCESS && arg.flags.value_or(0))
    {
        auto require = [&](AccountP holder, bool unchanged) {
            auto flags = getFlags(holder);
            if (!unchanged)
            {
                if (*arg.flags & tfMPTLock)
                    flags |= lsfMPTLocked;
                else if (*arg.flags & tfMPTUnlock)
                    flags &= ~lsfMPTLocked;
                else
                    assert(0);
            }
            env_.require(mptflags(*this, flags, holder));
        };
        if (arg.account)
            require(nullptr, arg.holder != nullptr);
        if (arg.holder)
            require(arg.holder, false);
    }
}

bool
MPTTester::forObject(
    std::function<bool(SLEP const& sle)> const& cb,
    AccountP holder_) const
{
    assert(issuanceKey_);
    auto const key = [&]() {
        if (holder_)
            return keylet::mptoken(*issuanceKey_, holder_->id());
        return keylet::mptIssuance(*issuanceKey_);
    }();
    if (auto const sle = env_.le(key))
        return cb(sle);
    return false;
}

[[nodiscard]] bool
MPTTester::checkMPTokenAmount(
    Account const& holder_,
    std::int64_t expectedAmount) const
{
    return forObject(
        [&](SLEP const& sle) { return expectedAmount == (*sle)[sfMPTAmount]; },
        &holder_);
}

[[nodiscard]] bool
MPTTester::checkMPTokenOutstandingAmount(std::int64_t expectedAmount) const
{
    return forObject([&](SLEP const& sle) {
        return expectedAmount == (*sle)[sfOutstandingAmount];
    });
}

[[nodiscard]] bool
MPTTester::checkFlags(uint32_t const expectedFlags, AccountP holder) const
{
    return expectedFlags == getFlags(holder);
}

void
MPTTester::pay(
    Account const& src,
    Account const& dest,
    std::int64_t amount,
    std::optional<TER> err)
{
    assert(id_);
    auto const srcAmt = getAmount(src);
    auto const destAmt = getAmount(dest);
    auto const outstnAmt = getAmount(issuer_);
    if (err)
        env_(jtx::pay(src, dest, mpt(amount)), ter(*err));
    else
        env_(jtx::pay(src, dest, mpt(amount)));
    if (env_.ter() != tesSUCCESS)
        amount = 0;
    if (close_)
        env_.close();
    if (src == issuer_)
    {
        env_.require(mptpay(*this, src, srcAmt + amount));
        env_.require(mptpay(*this, dest, destAmt + amount));
    }
    else if (dest == issuer_)
    {
        env_.require(mptpay(*this, src, srcAmt - amount));
        env_.require(mptpay(*this, dest, destAmt - amount));
    }
    else
    {
        STAmount const saAmount = {*id_, amount};
        STAmount const saActual =
            multiply(saAmount, transferRate(*env_.current(), *id_));
        // Sender pays the transfer fee if any
        env_.require(mptpay(*this, src, srcAmt - saActual.mpt().value()));
        env_.require(mptpay(*this, dest, destAmt + amount));
        // Outstanding amount is reduced by the transfer fee if any
        env_.require(mptpay(
            *this, issuer_, outstnAmt - (saActual - saAmount).mpt().value()));
    }
}

void
MPTTester::claw(
    Account const& issuer,
    Account const& holder,
    std::int64_t amount,
    std::optional<TER> err)
{
    assert(id_);
    auto const issuerAmt = getAmount(issuer);
    auto const holderAmt = getAmount(holder);
    if (err)
        env_(jtx::claw(issuer, mpt(amount), holder), ter(*err));
    else
        env_(jtx::claw(issuer, mpt(amount), holder));
    if (env_.ter() != tesSUCCESS)
        amount = 0;
    if (close_)
        env_.close();

    env_.require(
        mptpay(*this, issuer, issuerAmt - std::min(holderAmt, amount)));
    env_.require(
        mptpay(*this, holder, holderAmt - std::min(holderAmt, amount)));
}

PrettyAmount
MPTTester::mpt(std::int64_t amount) const
{
    assert(id_);
    return ripple::test::jtx::MPT(issuer_.name(), *id_)(amount);
}

std::int64_t
MPTTester::getAmount(Account const& account) const
{
    assert(issuanceKey_);
    if (account == issuer_)
    {
        if (auto const sle = env_.le(keylet::mptIssuance(*issuanceKey_)))
            return sle->getFieldU64(sfOutstandingAmount);
    }
    else
    {
        if (auto const sle =
                env_.le(keylet::mptoken(*issuanceKey_, account.id())))
            return sle->getFieldU64(sfMPTAmount);
    }
    return 0;
}

std::uint32_t
MPTTester::getFlags(ripple::test::jtx::AccountP holder) const
{
    std::uint32_t flags = 0;
    if (!forObject(
            [&](SLEP const& sle) {
                flags = sle->getFlags();
                return true;
            },
            holder))
        Throw<std::runtime_error>("Failed to get the flags");
    return flags;
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
