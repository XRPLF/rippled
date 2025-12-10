#include <test/jtx.h>

#include <xrpl/protocol/SField.h>
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
    if (arg.create)
        create(*arg.create);
}

MPTTester::MPTTester(
    Env& env,
    Account const& issuer,
    MPTID const& id,
    std::vector<Account> const& holders,
    bool close)
    : env_(env)
    , issuer_(issuer)
    , holders_(makeHolders(holders))
    , id_(id)
    , close_(close)
{
}

static MPTCreate
makeMPTCreate(MPTInitDef const& arg)
{
    if (arg.pay)
        return {
            .maxAmt = arg.maxAmt,
            .transferFee = arg.transferFee,
            .pay = {{arg.holders, *arg.pay}},
            .flags = arg.flags,
            .authHolder = arg.authHolder};
    return {
        .maxAmt = arg.maxAmt,
        .transferFee = arg.transferFee,
        .authorize = arg.holders,
        .flags = arg.flags,
        .authHolder = arg.authHolder};
}

MPTTester::MPTTester(MPTInitDef const& arg)
    : MPTTester{
          arg.env,
          arg.issuer,
          MPTInit{
              .fund = arg.fund,
              .close = arg.close,
              .create = makeMPTCreate(arg)}}
{
}

MPTTester::operator MPT() const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");
    return MPT("", *id_);
}

Json::Value
MPTTester::createjv(MPTCreate const& arg)
{
    if (!arg.issuer)
        Throw<std::runtime_error>("MPTTester::createjv: issuer is not set");
    Json::Value jv;
    jv[sfAccount] = arg.issuer->human();
    if (arg.assetScale)
        jv[sfAssetScale] = *arg.assetScale;
    if (arg.transferFee)
        jv[sfTransferFee] = *arg.transferFee;
    if (arg.metadata)
        jv[sfMPTokenMetadata] = strHex(*arg.metadata);
    if (arg.maxAmt)
        jv[sfMaximumAmount] = std::to_string(*arg.maxAmt);
    if (arg.domainID)
        jv[sfDomainID] = to_string(*arg.domainID);
    if (arg.mutableFlags)
        jv[sfMutableFlags] = *arg.mutableFlags;
    jv[sfTransactionType] = jss::MPTokenIssuanceCreate;

    return jv;
}

void
MPTTester::create(MPTCreate const& arg)
{
    if (id_)
        Throw<std::runtime_error>("MPT can't be reused");
    id_ = makeMptID(env_.seq(issuer_), issuer_);
    Json::Value jv = createjv(
        {.issuer = issuer_,
         .maxAmt = arg.maxAmt,
         .assetScale = arg.assetScale,
         .transferFee = arg.transferFee,
         .metadata = arg.metadata,
         .mutableFlags = arg.mutableFlags,
         .domainID = arg.domainID});
    if (submit(arg, jv) != tesSUCCESS)
    {
        // Verify issuance doesn't exist
        env_.require(requireAny([&]() -> bool {
            return env_.le(keylet::mptIssuance(*id_)) == nullptr;
        }));

        id_.reset();
    }
    else
    {
        env_.require(mptflags(*this, arg.flags.value_or(0)));
        auto authAndPay = [&](auto const& accts, auto const&& getAcct) {
            for (auto const& it : accts)
            {
                authorize({.account = getAcct(it)});
                if ((arg.flags.value_or(0) & tfMPTRequireAuth) &&
                    arg.authHolder)
                    authorize({.account = issuer_, .holder = getAcct(it)});
                if (arg.pay && arg.pay->first.empty())
                    pay(issuer_, getAcct(it), arg.pay->second);
            }
            if (arg.pay)
            {
                for (auto const& p : arg.pay->first)
                    pay(issuer_, p, arg.pay->second);
            }
        };
        if (arg.authorize)
        {
            if (arg.authorize->empty())
                authAndPay(holders_, [](auto const& it) { return it.second; });
            else
                authAndPay(*arg.authorize, [](auto const& it) { return it; });
        }
        else if (arg.pay)
        {
            if (arg.pay->first.empty())
                authAndPay(holders_, [](auto const& it) { return it.second; });
            else
                authAndPay(arg.pay->first, [](auto const& it) { return it; });
        }
    }
}

Json::Value
MPTTester::destroyjv(MPTDestroy const& arg)
{
    Json::Value jv;
    if (!arg.issuer || !arg.id)
        Throw<std::runtime_error>("MPTTester::destroyjv: issuer/id is not set");
    jv[sfAccount] = arg.issuer->human();
    jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    jv[sfTransactionType] = jss::MPTokenIssuanceDestroy;

    return jv;
}

void
MPTTester::destroy(MPTDestroy const& arg)
{
    if (!arg.id && !id_)
        Throw<std::runtime_error>("MPT has not been created");
    Json::Value jv = destroyjv(
        {.issuer = arg.issuer ? arg.issuer : issuer_,
         .id = arg.id ? arg.id : id_});
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

Json::Value
MPTTester::authorizejv(MPTAuthorize const& arg)
{
    Json::Value jv;
    if (!arg.account || !arg.id)
        Throw<std::runtime_error>(
            "MPTTester::authorizejv: issuer/id is not set");
    jv[sfAccount] = arg.account->human();
    jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    if (arg.holder)
        jv[sfHolder] = arg.holder->human();
    jv[sfTransactionType] = jss::MPTokenAuthorize;

    return jv;
}

void
MPTTester::authorize(MPTAuthorize const& arg)
{
    if (!arg.id && !id_)
        Throw<std::runtime_error>("MPT has not been created");
    Json::Value jv = authorizejv({
        .account = arg.account ? arg.account : issuer_,
        .holder = arg.holder,
        .id = arg.id ? arg.id : id_,
    });
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
MPTTester::authorizeHolders(Holders const& holders)
{
    for (auto const& holder : holders)
    {
        authorize({.account = holder});
    }
}

Json::Value
MPTTester::setjv(MPTSet const& arg)
{
    Json::Value jv;
    if (!arg.account || !arg.id)
        Throw<std::runtime_error>("MPTTester::setjv: issuer/id is not set");
    jv[sfAccount] = arg.account->human();
    jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    if (arg.holder)
    {
        std::visit(
            [&jv]<typename T>(T const& holder) {
                if constexpr (std::is_same_v<T, Account>)
                    jv[sfHolder] = holder.human();
                else if constexpr (std::is_same_v<T, AccountID>)
                    jv[sfHolder] = toBase58(holder);
            },
            *arg.holder);
    }

    if (arg.delegate)
        jv[sfDelegate] = arg.delegate->human();
    if (arg.domainID)
        jv[sfDomainID] = to_string(*arg.domainID);
    if (arg.mutableFlags)
        jv[sfMutableFlags] = *arg.mutableFlags;
    if (arg.transferFee)
        jv[sfTransferFee] = *arg.transferFee;
    if (arg.metadata)
        jv[sfMPTokenMetadata] = strHex(*arg.metadata);
    jv[sfTransactionType] = jss::MPTokenIssuanceSet;

    return jv;
}

void
MPTTester::set(MPTSet const& arg)
{
    if (!arg.id && !id_)
        Throw<std::runtime_error>("MPT has not been created");
    Json::Value jv = setjv(
        {.account = arg.account ? arg.account : issuer_,
         .holder = arg.holder,
         .id = arg.id ? arg.id : id_,
         .mutableFlags = arg.mutableFlags,
         .transferFee = arg.transferFee,
         .metadata = arg.metadata,
         .delegate = arg.delegate,
         .domainID = arg.domainID});
    if (submit(arg, jv) == tesSUCCESS &&
        (arg.flags.value_or(0) || arg.mutableFlags))
    {
        auto require = [&](std::optional<Account> const& holder,
                           bool unchanged) {
            auto flags = getFlags(holder);
            if (!unchanged)
            {
                if (arg.flags)
                {
                    if (*arg.flags & tfMPTLock)
                        flags |= lsfMPTLocked;
                    else if (*arg.flags & tfMPTUnlock)
                        flags &= ~lsfMPTLocked;
                }

                if (arg.mutableFlags)
                {
                    if (*arg.mutableFlags & tmfMPTSetCanLock)
                        flags |= lsfMPTCanLock;
                    else if (*arg.mutableFlags & tmfMPTClearCanLock)
                        flags &= ~lsfMPTCanLock;

                    if (*arg.mutableFlags & tmfMPTSetRequireAuth)
                        flags |= lsfMPTRequireAuth;
                    else if (*arg.mutableFlags & tmfMPTClearRequireAuth)
                        flags &= ~lsfMPTRequireAuth;

                    if (*arg.mutableFlags & tmfMPTSetCanEscrow)
                        flags |= lsfMPTCanEscrow;
                    else if (*arg.mutableFlags & tmfMPTClearCanEscrow)
                        flags &= ~lsfMPTCanEscrow;

                    if (*arg.mutableFlags & tmfMPTSetCanClawback)
                        flags |= lsfMPTCanClawback;
                    else if (*arg.mutableFlags & tmfMPTClearCanClawback)
                        flags &= ~lsfMPTCanClawback;

                    if (*arg.mutableFlags & tmfMPTSetCanTrade)
                        flags |= lsfMPTCanTrade;
                    else if (*arg.mutableFlags & tmfMPTClearCanTrade)
                        flags &= ~lsfMPTCanTrade;

                    if (*arg.mutableFlags & tmfMPTSetCanTransfer)
                        flags |= lsfMPTCanTransfer;
                    else if (*arg.mutableFlags & tmfMPTClearCanTransfer)
                        flags &= ~lsfMPTCanTransfer;
                }
            }
            env_.require(mptflags(*this, flags, holder));
        };
        if (arg.account)
            require(std::nullopt, arg.holder.has_value());
        if (auto const account =
                (arg.holder ? std::get_if<Account>(&(*arg.holder)) : nullptr))
            require(*account, false);
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
MPTTester::checkDomainID(std::optional<uint256> expected) const
{
    return forObject([&](SLEP const& sle) -> bool {
        if (sle->isFieldPresent(sfDomainID))
            return expected == sle->getFieldH256(sfDomainID);
        return (!expected.has_value());
    });
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

[[nodiscard]] bool
MPTTester::checkMetadata(std::string const& metadata) const
{
    return forObject([&](SLEP const& sle) -> bool {
        if (sle->isFieldPresent(sfMPTokenMetadata))
            return strHex(sle->getFieldVL(sfMPTokenMetadata)) ==
                strHex(metadata);
        return false;
    });
}

[[nodiscard]] bool
MPTTester::isMetadataPresent() const
{
    return forObject([&](SLEP const& sle) -> bool {
        return sle->isFieldPresent(sfMPTokenMetadata);
    });
}

[[nodiscard]] bool
MPTTester::checkTransferFee(std::uint16_t transferFee) const
{
    return forObject([&](SLEP const& sle) -> bool {
        if (sle->isFieldPresent(sfTransferFee))
            return sle->getFieldU16(sfTransferFee) == transferFee;
        return false;
    });
}

[[nodiscard]] bool
MPTTester::isTransferFeePresent() const
{
    return forObject([&](SLEP const& sle) -> bool {
        return sle->isFieldPresent(sfTransferFee);
    });
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

MPTTester::operator Asset() const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");
    return Asset(*id_);
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
MPTTester::operator[](std::string const& name) const
{
    return MPT(name, issuanceID());
}

PrettyAmount
MPTTester::operator()(std::uint64_t amount) const
{
    return MPT("", issuanceID())(amount);
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
