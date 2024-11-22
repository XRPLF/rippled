//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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
mptbalance::operator()(Env& env) const
{
    env.test.expect(amount_ == tester_.getBalance(account_));
}

void
requireAny::operator()(Env& env) const
{
    env.test.expect(cb_());
}

std::unordered_map<std::string, Account>
MPTTester::makeHolders(std::vector<Account> const& holders)
{
    std::unordered_map<std::string, Account> accounts;
    for (auto const& h : holders)
    {
        if (accounts.find(h.human()) != accounts.cend())
            Throw<std::runtime_error>("Duplicate holder");
        accounts.emplace(h.human(), h);
    }
    return accounts;
}

MPTTester::MPTTester(Env& env, Account const& issuer, MPTInit const& arg)
    : env_(env)
    , issuer_(issuer)
    , holders_(makeHolders(arg.holders))
    , close_(arg.close)
{
    if (arg.fund)
    {
        env_.fund(arg.xrp, issuer_);
        for (auto it : holders_)
            env_.fund(arg.xrpHolders, it.second);
    }
    if (close_)
        env.close();
    if (arg.fund)
    {
        env_.require(owners(issuer_, 0));
        for (auto it : holders_)
        {
            if (issuer_.id() == it.second.id())
                Throw<std::runtime_error>("Issuer can't be holder");
            env_.require(owners(it.second, 0));
        }
    }
}

void
MPTTester::create(const MPTCreate& arg)
{
    if (id_)
        Throw<std::runtime_error>("MPT can't be reused");
    id_ = makeMptID(env_.seq(issuer_), issuer_);
    Json::Value jv;
    jv[sfAccount] = issuer_.human();
    jv[sfTransactionType] = jss::MPTokenIssuanceCreate;
    if (arg.assetScale)
        jv[sfAssetScale] = *arg.assetScale;
    if (arg.transferFee)
        jv[sfTransferFee] = *arg.transferFee;
    if (arg.metadata)
        jv[sfMPTokenMetadata] = strHex(*arg.metadata);
    if (arg.maxAmt)
        jv[sfMaximumAmount] = std::to_string(*arg.maxAmt);
    if (submit(arg, jv) != tesSUCCESS)
    {
        // Verify issuance doesn't exist
        env_.require(requireAny([&]() -> bool {
            return env_.le(keylet::mptIssuance(*id_)) == nullptr;
        }));

        id_.reset();
    }
    else
        env_.require(mptflags(*this, arg.flags.value_or(0)));
}

void
MPTTester::destroy(MPTDestroy const& arg)
{
    Json::Value jv;
    if (arg.issuer)
        jv[sfAccount] = arg.issuer->human();
    else
        jv[sfAccount] = issuer_.human();
    if (arg.id)
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    else
    {
        if (!id_)
            Throw<std::runtime_error>("MPT has not been created");
        jv[sfMPTokenIssuanceID] = to_string(*id_);
    }
    jv[sfTransactionType] = jss::MPTokenIssuanceDestroy;
    submit(arg, jv);
}

Account const&
MPTTester::holder(std::string const& holder_) const
{
    auto const& it = holders_.find(holder_);
    if (it == holders_.cend())
        Throw<std::runtime_error>("Holder is not found");
    return it->second;
}

void
MPTTester::authorize(MPTAuthorize const& arg)
{
    Json::Value jv;
    if (arg.account)
        jv[sfAccount] = arg.account->human();
    else
        jv[sfAccount] = issuer_.human();
    jv[sfTransactionType] = jss::MPTokenAuthorize;
    if (arg.id)
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    else
    {
        if (!id_)
            Throw<std::runtime_error>("MPT has not been created");
        jv[sfMPTokenIssuanceID] = to_string(*id_);
    }
    if (arg.holder)
        jv[sfHolder] = arg.holder->human();
    if (auto const result = submit(arg, jv); result == tesSUCCESS)
    {
        // Issuer authorizes
        if (!arg.account || *arg.account == issuer_)
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
        else if (arg.flags.value_or(0) != tfMPTUnauthorize)
        {
            auto const flags = getFlags(arg.account);
            // holder creates a token
            env_.require(mptflags(*this, flags, arg.account));
            env_.require(mptbalance(*this, *arg.account, 0));
        }
        else
        {
            // Verify that the MPToken doesn't exist.
            forObject(
                [&](SLEP const& sle) { return env_.test.BEAST_EXPECT(!sle); },
                arg.account);
        }
    }
    else if (
        arg.account && *arg.account != issuer_ &&
        arg.flags.value_or(0) != tfMPTUnauthorize && id_)
    {
        if (result == tecDUPLICATE)
        {
            // Verify that MPToken already exists
            env_.require(requireAny([&]() -> bool {
                return env_.le(keylet::mptoken(*id_, arg.account->id())) !=
                    nullptr;
            }));
        }
        else
        {
            // Verify MPToken doesn't exist if holder failed authorizing(unless
            // it already exists)
            env_.require(requireAny([&]() -> bool {
                return env_.le(keylet::mptoken(*id_, arg.account->id())) ==
                    nullptr;
            }));
        }
    }
}

void
MPTTester::set(MPTSet const& arg)
{
    Json::Value jv;
    if (arg.account)
        jv[sfAccount] = arg.account->human();
    else
        jv[sfAccount] = issuer_.human();
    jv[sfTransactionType] = jss::MPTokenIssuanceSet;
    if (arg.id)
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    else
    {
        if (!id_)
            Throw<std::runtime_error>("MPT has not been created");
        jv[sfMPTokenIssuanceID] = to_string(*id_);
    }
    if (arg.holder)
        jv[sfHolder] = arg.holder->human();
    if (submit(arg, jv) == tesSUCCESS && arg.flags.value_or(0))
    {
        auto require = [&](std::optional<Account> const& holder,
                           bool unchanged) {
            auto flags = getFlags(holder);
            if (!unchanged)
            {
                if (*arg.flags & tfMPTLock)
                    flags |= lsfMPTLocked;
                else if (*arg.flags & tfMPTUnlock)
                    flags &= ~lsfMPTLocked;
                else
                    Throw<std::runtime_error>("Invalid flags");
            }
            env_.require(mptflags(*this, flags, holder));
        };
        if (arg.account)
            require(std::nullopt, arg.holder.has_value());
        if (arg.holder)
            require(*arg.holder, false);
    }
}

bool
MPTTester::forObject(
    std::function<bool(SLEP const& sle)> const& cb,
    std::optional<Account> const& holder_) const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");
    auto const key = holder_ ? keylet::mptoken(*id_, holder_->id())
                             : keylet::mptIssuance(*id_);
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
        holder_);
}

[[nodiscard]] bool
MPTTester::checkMPTokenOutstandingAmount(std::int64_t expectedAmount) const
{
    return forObject([&](SLEP const& sle) {
        return expectedAmount == (*sle)[sfOutstandingAmount];
    });
}

[[nodiscard]] bool
MPTTester::checkFlags(
    uint32_t const expectedFlags,
    std::optional<Account> const& holder) const
{
    return expectedFlags == getFlags(holder);
}

void
MPTTester::pay(
    Account const& src,
    Account const& dest,
    std::int64_t amount,
    std::optional<TER> err,
    std::optional<std::vector<std::string>> credentials)
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");
    auto const srcAmt = getBalance(src);
    auto const destAmt = getBalance(dest);
    auto const outstnAmt = getBalance(issuer_);

    if (credentials)
        env_(
            jtx::pay(src, dest, mpt(amount)),
            ter(err.value_or(tesSUCCESS)),
            credentials::ids(*credentials));
    else
        env_(jtx::pay(src, dest, mpt(amount)), ter(err.value_or(tesSUCCESS)));

    if (env_.ter() != tesSUCCESS)
        amount = 0;
    if (close_)
        env_.close();
    if (src == issuer_)
    {
        env_.require(mptbalance(*this, src, srcAmt + amount));
        env_.require(mptbalance(*this, dest, destAmt + amount));
    }
    else if (dest == issuer_)
    {
        env_.require(mptbalance(*this, src, srcAmt - amount));
        env_.require(mptbalance(*this, dest, destAmt - amount));
    }
    else
    {
        STAmount const saAmount = {*id_, amount};
        auto const actual =
            multiply(saAmount, transferRate(*env_.current(), *id_))
                .mpt()
                .value();
        // Sender pays the transfer fee if any
        env_.require(mptbalance(*this, src, srcAmt - actual));
        env_.require(mptbalance(*this, dest, destAmt + amount));
        // Outstanding amount is reduced by the transfer fee if any
        env_.require(mptbalance(*this, issuer_, outstnAmt - (actual - amount)));
    }
}

void
MPTTester::claw(
    Account const& issuer,
    Account const& holder,
    std::int64_t amount,
    std::optional<TER> err)
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");
    auto const issuerAmt = getBalance(issuer);
    auto const holderAmt = getBalance(holder);
    env_(jtx::claw(issuer, mpt(amount), holder), ter(err.value_or(tesSUCCESS)));
    if (env_.ter() != tesSUCCESS)
        amount = 0;
    if (close_)
        env_.close();

    env_.require(
        mptbalance(*this, issuer, issuerAmt - std::min(holderAmt, amount)));
    env_.require(
        mptbalance(*this, holder, holderAmt - std::min(holderAmt, amount)));
}

PrettyAmount
MPTTester::mpt(std::int64_t amount) const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");
    return ripple::test::jtx::MPT(issuer_.name(), *id_)(amount);
}

std::int64_t
MPTTester::getBalance(Account const& account) const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");
    if (account == issuer_)
    {
        if (auto const sle = env_.le(keylet::mptIssuance(*id_)))
            return sle->getFieldU64(sfOutstandingAmount);
    }
    else
    {
        if (auto const sle = env_.le(keylet::mptoken(*id_, account.id())))
            return sle->getFieldU64(sfMPTAmount);
    }
    return 0;
}

std::uint32_t
MPTTester::getFlags(std::optional<Account> const& holder) const
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

MPT
MPTTester::operator[](const std::string& name)
{
    return MPT(name, issuanceID());
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
