#include <test/jtx.h>
#include <test/jtx/mpt.h>

#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <openssl/rand.h>

#include <cstdint>
#include <string>

namespace xrpl {
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
    : env_(env), issuer_(issuer), holders_(makeHolders(arg.holders)), auditor_(arg.auditor), close_(arg.close)
{
    if (arg.fund)
    {
        env_.fund(arg.xrp, issuer_);
        for (auto it : holders_)
            env_.fund(arg.xrpHolders, it.second);

        if (arg.auditor)
            env_.fund(arg.xrp, *arg.auditor);
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

        if (arg.auditor)
            env_.require(owners(*arg.auditor, 0));
    }
    if (arg.create)
        create(*arg.create);
}

MPTTester::MPTTester(Env& env, Account const& issuer, MPTID const& id, std::vector<Account> const& holders, bool close)
    : env_(env), issuer_(issuer), holders_(makeHolders(holders)), id_(id), close_(close)
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
              .auditor = arg.auditor,
              .fund = arg.fund,
              .close = arg.close,
              .create = makeMPTCreate(arg),
          }}
{
}

MPTTester::operator MPT() const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");
    return MPT("", *id_);
}

Json::Value
MPTTester::createJV(MPTCreate const& arg)
{
    if (!arg.issuer)
        Throw<std::runtime_error>("MPTTester::createJV: issuer is not set");
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
    Json::Value jv = createJV(
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
        env_.require(requireAny([&]() -> bool { return env_.le(keylet::mptIssuance(*id_)) == nullptr; }));

        id_.reset();
    }
    else
    {
        env_.require(mptflags(*this, arg.flags.value_or(0)));
        auto authAndPay = [&](auto const& accts, auto const&& getAcct) {
            for (auto const& it : accts)
            {
                authorize({.account = getAcct(it)});
                if ((arg.flags.value_or(0) & tfMPTRequireAuth) && arg.authHolder)
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
MPTTester::destroyJV(MPTDestroy const& arg)
{
    Json::Value jv;
    if (!arg.issuer || !arg.id)
        Throw<std::runtime_error>("MPTTester::destroyJV: issuer/id is not set");
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
    Json::Value jv = destroyJV({.issuer = arg.issuer ? arg.issuer : issuer_, .id = arg.id ? arg.id : id_});
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
MPTTester::authorizeJV(MPTAuthorize const& arg)
{
    Json::Value jv;
    if (!arg.account || !arg.id)
        Throw<std::runtime_error>("MPTTester::authorizeJV: account/id is not set");
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
    Json::Value jv = authorizeJV({
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
                env_.require(mptflags(*this, flags | lsfMPTAuthorized, arg.holder));
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
            forObject([&](SLEP const& sle) { return env_.test.BEAST_EXPECT(!sle); }, arg.account);
        }
    }
    else if (arg.account && *arg.account != issuer_ && arg.flags.value_or(0) != tfMPTUnauthorize && id_)
    {
        if (result == tecDUPLICATE)
        {
            // Verify that MPToken already exists
            env_.require(
                requireAny([&]() -> bool { return env_.le(keylet::mptoken(*id_, arg.account->id())) != nullptr; }));
        }
        else
        {
            // Verify MPToken doesn't exist if holder failed authorizing(unless
            // it already exists)
            env_.require(
                requireAny([&]() -> bool { return env_.le(keylet::mptoken(*id_, arg.account->id())) == nullptr; }));
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
MPTTester::setJV(MPTSet const& arg)
{
    Json::Value jv;
    if (!arg.account || !arg.id)
        Throw<std::runtime_error>("MPTTester::setJV: account and/or id is not set");
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
    if (arg.issuerPubKey)
        jv[sfIssuerElGamalPublicKey] = strHex(*arg.issuerPubKey);
    if (arg.auditorPubKey)
        jv[sfAuditorElGamalPublicKey] = strHex(*arg.auditorPubKey);
    jv[sfTransactionType] = jss::MPTokenIssuanceSet;

    return jv;
}

void
MPTTester::set(MPTSet const& arg)
{
    if (!arg.id && !id_)
        Throw<std::runtime_error>("MPT has not been created");
    Json::Value jv = setJV(
        {.account = arg.account ? arg.account : issuer_,
         .holder = arg.holder,
         .id = arg.id ? arg.id : id_,
         .mutableFlags = arg.mutableFlags,
         .transferFee = arg.transferFee,
         .metadata = arg.metadata,
         .delegate = arg.delegate,
         .domainID = arg.domainID,
         .issuerPubKey = arg.issuerPubKey,
         .auditorPubKey = arg.auditorPubKey});
    if (submit(arg, jv) == tesSUCCESS)
    {
        if ((arg.flags.value_or(0) || arg.mutableFlags))
        {
            auto require = [&](std::optional<Account> const& holder, bool unchanged) {
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

                        if (*arg.mutableFlags & tmfMPTSetPrivacy)
                            flags |= lsfMPTCanPrivacy;
                        else if (*arg.mutableFlags & tmfMPTClearPrivacy)
                            flags &= ~lsfMPTCanPrivacy;
                    }
                }
                env_.require(mptflags(*this, flags, holder));
            };
            if (arg.account)
                require(std::nullopt, arg.holder.has_value());
            if (auto const account = (arg.holder ? std::get_if<Account>(&(*arg.holder)) : nullptr))
                require(*account, false);
        }

        if (arg.issuerPubKey)
        {
            env_.require(requireAny([&]() -> bool {
                return forObject([&](SLEP const& sle) -> bool {
                    if (sle)
                    {
                        auto const issuerPubKey = getPubKey(issuer_);
                        if (!issuerPubKey)
                            Throw<std::runtime_error>("MPTTester::set: issuer's pubkey is not set");

                        return strHex((*sle)[sfIssuerElGamalPublicKey]) == strHex(*issuerPubKey);
                    }
                    return false;
                });
            }));
        }

        if (arg.auditorPubKey)
        {
            env_.require(requireAny([&]() -> bool {
                return forObject([&](SLEP const& sle) -> bool {
                    if (sle)
                    {
                        if (!auditor_.has_value())
                            Throw<std::runtime_error>("MPTTester::set: auditor is not set");

                        auto const auditorPubKey = getPubKey(*auditor_);
                        if (!auditorPubKey)
                            Throw<std::runtime_error>("MPTTester::set: auditor's pubkey is not set");

                        return strHex((*sle)[sfAuditorElGamalPublicKey]) == strHex(*auditorPubKey);
                    }
                    return false;
                });
            }));
        }
    }
}

bool
MPTTester::forObject(std::function<bool(SLEP const& sle)> const& cb, std::optional<Account> const& holder_) const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");
    auto const key = holder_ ? keylet::mptoken(*id_, holder_->id()) : keylet::mptIssuance(*id_);
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
MPTTester::printMPT(Account const& holder_) const
{
    return forObject(
        [&](SLEP const& sle) -> bool {
            std::cout << "\n" << sle->getJson();
            return true;
        },
        holder_);
}

[[nodiscard]] bool
MPTTester::checkMPTokenAmount(Account const& holder_, std::int64_t expectedAmount) const
{
    return forObject([&](SLEP const& sle) { return expectedAmount == (*sle)[sfMPTAmount]; }, holder_);
}

[[nodiscard]] bool
MPTTester::checkMPTokenOutstandingAmount(std::int64_t expectedAmount) const
{
    return forObject([&](SLEP const& sle) { return expectedAmount == (*sle)[sfOutstandingAmount]; });
}

[[nodiscard]] bool
MPTTester::checkIssuanceConfidentialBalance(std::int64_t expectedAmount) const
{
    return forObject(
        [&](SLEP const& sle) { return expectedAmount == (*sle)[~sfConfidentialOutstandingAmount].value_or(0); });
}

[[nodiscard]] bool
MPTTester::checkFlags(uint32_t const expectedFlags, std::optional<Account> const& holder) const
{
    return expectedFlags == getFlags(holder);
}

[[nodiscard]] bool
MPTTester::checkMetadata(std::string const& metadata) const
{
    return forObject([&](SLEP const& sle) -> bool {
        if (sle->isFieldPresent(sfMPTokenMetadata))
            return strHex(sle->getFieldVL(sfMPTokenMetadata)) == strHex(metadata);
        return false;
    });
}

[[nodiscard]] bool
MPTTester::isMetadataPresent() const
{
    return forObject([&](SLEP const& sle) -> bool { return sle->isFieldPresent(sfMPTokenMetadata); });
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
    return forObject([&](SLEP const& sle) -> bool { return sle->isFieldPresent(sfTransferFee); });
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
    auto const outstandingAmt = getBalance(issuer_);

    if (credentials)
        env_(jtx::pay(src, dest, mpt(amount)), ter(err.value_or(tesSUCCESS)), credentials::ids(*credentials));
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
        auto const actual = multiply(saAmount, transferRate(*env_.current(), *id_)).mpt().value();
        // Sender pays the transfer fee if any
        env_.require(mptbalance(*this, src, srcAmt - actual));
        env_.require(mptbalance(*this, dest, destAmt + amount));
        // Outstanding amount is reduced by the transfer fee if any
        env_.require(mptbalance(*this, issuer_, outstandingAmt - (actual - amount)));
    }
}

void
MPTTester::claw(Account const& issuer, Account const& holder, std::int64_t amount, std::optional<TER> err)
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

    env_.require(mptbalance(*this, issuer, issuerAmt - std::min(holderAmt, amount)));
    env_.require(mptbalance(*this, holder, holderAmt - std::min(holderAmt, amount)));
}

PrettyAmount
MPTTester::mpt(std::int64_t amount) const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");
    return xrpl::test::jtx::MPT(issuer_.name(), *id_)(amount);
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

std::int64_t
MPTTester::getIssuanceConfidentialBalance() const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");

    if (auto const sle = env_.le(keylet::mptIssuance(*id_)))
        return (*sle)[~sfConfidentialOutstandingAmount].value_or(0);

    return 0;
}

std::optional<Buffer>
MPTTester::getClawbackProof(
    Account const& holder,
    std::uint64_t amount,
    Buffer const& privateKey,
    uint256 const& contextHash) const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");

    auto const sleHolder = env_.le(keylet::mptoken(*id_, holder.id()));
    auto const sleIssuance = env_.le(keylet::mptIssuance(*id_));

    if (!sleHolder || !sleIssuance)
        return std::nullopt;

    auto const ciphertextBlob = sleHolder->getFieldVL(sfIssuerEncryptedBalance);
    if (ciphertextBlob.size() != ecGamalEncryptedTotalLength)
        return std::nullopt;

    auto const pubKeyBlob = sleIssuance->getFieldVL(sfIssuerElGamalPublicKey);
    if (pubKeyBlob.size() != ecPubKeyLength)
        return std::nullopt;

    secp256k1_pubkey c1, c2, pk;
    auto const ctx = secp256k1Context();

    if (!secp256k1_ec_pubkey_parse(ctx, &c1, ciphertextBlob.data(), ecGamalEncryptedLength))
    {
        return std::nullopt;
    }

    if (!secp256k1_ec_pubkey_parse(ctx, &c2, ciphertextBlob.data() + ecGamalEncryptedLength, ecGamalEncryptedLength))
    {
        return std::nullopt;
    }

    if (!secp256k1_ec_pubkey_parse(ctx, &pk, pubKeyBlob.data(), ecPubKeyLength))
    {
        return std::nullopt;
    }

    Buffer proof(ecEqualityProofLength);

    if (secp256k1_equality_plaintext_prove(
            ctx, proof.data(), &pk, &c2, &c1, amount, privateKey.data(), contextHash.data()) != 1)
    {
        return std::nullopt;
    }

    return proof;
}

std::optional<Buffer>
MPTTester::getSchnorrProof(Account const& account, uint256 const& ctxHash) const
{
    auto const pubKey = getPubKey(account);
    if (!pubKey || pubKey->size() != ecPubKeyLength)
        return std::nullopt;

    auto const privKey = getPrivKey(account);
    if (privKey->size() != ecPrivKeyLength)
        return std::nullopt;

    secp256k1_pubkey pk;
    if (secp256k1_ec_pubkey_parse(secp256k1Context(), &pk, pubKey->data(), ecPubKeyLength) != 1)
        return std::nullopt;

    Buffer proof(ecSchnorrProofLength);

    if (secp256k1_mpt_pok_sk_prove(secp256k1Context(), proof.data(), &pk, privKey->data(), ctxHash.data()) != 1)
    {
        return std::nullopt;
    }

    return proof;
}

std::optional<Buffer>
MPTTester::getConfidentialSendProof(
    Account const& sender,
    std::uint64_t const amount,
    std::vector<ConfidentialRecipient> const& recipients,
    Slice const& blindingFactor,
    std::size_t const nRecipients,
    uint256 const& contextHash,
    PedersenProofParams const& amountParams,
    PedersenProofParams const& balanceParams) const
{
    if (recipients.size() != nRecipients)
        return std::nullopt;

    if (blindingFactor.size() != ecBlindingFactorLength)
        return std::nullopt;

    auto const senderPubKey = getPubKey(sender);
    if (!senderPubKey)
        return std::nullopt;

    auto const ctx = secp256k1Context();

    std::vector<secp256k1_pubkey> r(nRecipients);
    std::vector<secp256k1_pubkey> s(nRecipients);
    std::vector<secp256k1_pubkey> pk(nRecipients);

    std::vector<unsigned char> sr;
    sr.reserve(nRecipients * ecBlindingFactorLength);

    for (size_t i = 0; i < nRecipients; ++i)
    {
        auto const& recipient = recipients[i];
        auto const* ctData = recipient.encryptedAmount.data();

        if (recipient.encryptedAmount.size() != ecGamalEncryptedTotalLength)
            return std::nullopt;

        if (recipient.publicKey.size() != ecPubKeyLength)
            return std::nullopt;

        if (!secp256k1_ec_pubkey_parse(ctx, &r[i], ctData, ecGamalEncryptedLength))
        {
            return std::nullopt;
        }

        if (!secp256k1_ec_pubkey_parse(ctx, &s[i], ctData + ecGamalEncryptedLength, ecGamalEncryptedLength))
        {
            return std::nullopt;
        }

        if (!secp256k1_ec_pubkey_parse(ctx, &pk[i], recipient.publicKey.data(), ecPubKeyLength))
            return std::nullopt;

        sr.insert(sr.end(), blindingFactor.data(), blindingFactor.data() + ecBlindingFactorLength);
    }

    size_t sizeEquality = secp256k1_mpt_prove_same_plaintext_multi_size(nRecipients);
    Buffer equalityProof(sizeEquality);

    // Get the multi-ciphertext equality proof
    if (secp256k1_mpt_prove_same_plaintext_multi(
            ctx,
            equalityProof.data(),
            &sizeEquality,
            amount,
            nRecipients,
            r.data(),
            s.data(),
            pk.data(),
            sr.data(),
            contextHash.data()) != 1)
    {
        return std::nullopt;
    }

    auto const amountLinkageProof = getAmountLinkageProof(
        *senderPubKey, Buffer(blindingFactor.data(), ecBlindingFactorLength), contextHash, amountParams);

    auto const balanceLinkageProof = getBalanceLinkageProof(sender, contextHash, *senderPubKey, balanceParams);

    std::uint64_t const remainingBalance = balanceParams.amt - amount;

    // Compute the blinding factor for the remaining balance: rho_rem = rho_balance - rho_amount
    unsigned char rho_rem[32];
    unsigned char neg_rho_m[32];

    secp256k1_mpt_scalar_negate(neg_rho_m, amountParams.blindingFactor.data());
    secp256k1_mpt_scalar_add(rho_rem, balanceParams.blindingFactor.data(), neg_rho_m);

    // Generate bulletproof for the amount and remaining balance
    Buffer const bulletproof =
        getBulletproof({amount, remainingBalance}, {amountParams.blindingFactor, Buffer(rho_rem, 32)}, contextHash);

    OPENSSL_cleanse(neg_rho_m, 32);
    OPENSSL_cleanse(rho_rem, 32);

    auto const sizeAmountLinkage = amountLinkageProof.size();
    auto const sizeBalanceLinkage = balanceLinkageProof.size();
    auto const sizeBulletproof = bulletproof.size();

    size_t const proofSize = sizeEquality + sizeAmountLinkage + sizeBalanceLinkage + sizeBulletproof;
    Buffer proof(proofSize);

    auto ptr = proof.data();
    std::memcpy(ptr, equalityProof.data(), sizeEquality);
    ptr += sizeEquality;

    std::memcpy(ptr, amountLinkageProof.data(), sizeAmountLinkage);
    ptr += sizeAmountLinkage;

    std::memcpy(ptr, balanceLinkageProof.data(), sizeBalanceLinkage);
    ptr += sizeBalanceLinkage;

    std::memcpy(ptr, bulletproof.data(), sizeBulletproof);

    return proof;
}

Buffer
MPTTester::getPedersenCommitment(std::uint64_t const amount, Buffer const& pedersenBlindingFactor)
{
    // Blinding factor (rho) must be a 32-byte scalar
    if (pedersenBlindingFactor.size() != ecBlindingFactorLength)
        Throw<std::runtime_error>("Invalid blinding factor size");

    // secp256k1_mpt_pedersen_commit doesn't handle amount 0, return a trivial
    // valid commitment for test purposes
    if (amount == 0)
    {
        Buffer buf(ecPedersenCommitmentLength);
        std::memset(buf.data(), 0, ecPedersenCommitmentLength);
        buf.data()[0] = 0x02;
        buf.data()[ecPedersenCommitmentLength - 1] = 0x01;
        return buf;
    }

    secp256k1_pubkey commitment;
    auto const ctx = secp256k1Context();

    // Compute PC = m*G + rho*H
    if (secp256k1_mpt_pedersen_commit(ctx, &commitment, amount, pedersenBlindingFactor.data()) != 1)
    {
        Throw<std::runtime_error>("Pedersen commitment generation failed");
    }

    // Serialize commitment to compressed format (33 bytes)
    unsigned char compressedCommitment[ecPedersenCommitmentLength];
    size_t outLen = ecPedersenCommitmentLength;
    if (secp256k1_ec_pubkey_serialize(ctx, compressedCommitment, &outLen, &commitment, SECP256K1_EC_COMPRESSED) != 1 ||
        outLen != ecPedersenCommitmentLength)
    {
        Throw<std::runtime_error>("Pedersen commitment serialization failed");
    }

    return Buffer{compressedCommitment, ecPedersenCommitmentLength};
}

Buffer
MPTTester::getConvertBackProof(
    Account const& holder,
    std::uint64_t const amount,
    uint256 const& contextHash,
    PedersenProofParams const& pcParams) const
{
    // Expected total proof length: pedersen proof + single bulletproof
    std::size_t constexpr expectedProofLength = ecPedersenProofLength + ecSingleBulletproofLength;

    auto const sleMptoken = env_.le(keylet::mptoken(*id_, holder.id()));
    if (!sleMptoken || !sleMptoken->isFieldPresent(sfConfidentialBalanceSpending))
        return makeZeroBuffer(expectedProofLength);

    auto const holderPubKey = getPubKey(holder);

    if (!holderPubKey)
        return makeZeroBuffer(expectedProofLength);

    Buffer const pedersenProof = getBalanceLinkageProof(holder, contextHash, *holderPubKey, pcParams);

    // Generate bulletproof for the remaining balance (balance - amount)
    // Use the same blinding factor as the one used to generate the PC_balance
    std::uint64_t const remainingBalance = pcParams.amt - amount;
    Buffer const bulletproof = getBulletproof({remainingBalance}, {pcParams.blindingFactor}, contextHash);

    // Combine pedersen proof and bulletproof
    Buffer combinedProof(pedersenProof.size() + bulletproof.size());
    std::memcpy(combinedProof.data(), pedersenProof.data(), pedersenProof.size());
    std::memcpy(combinedProof.data() + pedersenProof.size(), bulletproof.data(), bulletproof.size());

    return combinedProof;
}

std::optional<Buffer>
MPTTester::getEncryptedBalance(Account const& account, EncryptedBalanceType option) const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");

    if (auto const sle = env_.le(keylet::mptoken(*id_, account.id())))
    {
        if (option == HOLDER_ENCRYPTED_INBOX && sle->isFieldPresent(sfConfidentialBalanceInbox))
            return Buffer((*sle)[sfConfidentialBalanceInbox].data(), (*sle)[sfConfidentialBalanceInbox].size());
        if (option == HOLDER_ENCRYPTED_SPENDING && sle->isFieldPresent(sfConfidentialBalanceSpending))
            return Buffer((*sle)[sfConfidentialBalanceSpending].data(), (*sle)[sfConfidentialBalanceSpending].size());
        if (option == ISSUER_ENCRYPTED_BALANCE && sle->isFieldPresent(sfIssuerEncryptedBalance))
            return Buffer((*sle)[sfIssuerEncryptedBalance].data(), (*sle)[sfIssuerEncryptedBalance].size());
        if (option == AUDITOR_ENCRYPTED_BALANCE && sle->isFieldPresent(sfAuditorEncryptedBalance))
            return Buffer((*sle)[sfAuditorEncryptedBalance].data(), (*sle)[sfAuditorEncryptedBalance].size());
    }

    return {};
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
MPTTester::operator()(std::int64_t amount) const
{
    return MPT("", issuanceID())(amount);
}

template <typename T>
void
MPTTester::fillConversionCiphertexts(
    T const& arg,
    Json::Value& jv,
    Buffer& holderCiphertext,
    Buffer& issuerCiphertext,
    std::optional<Buffer>& auditorCiphertext,
    Buffer& blindingFactor) const
{
    blindingFactor = arg.blindingFactor ? *arg.blindingFactor : generateBlindingFactor();

    // Handle Holder
    if (arg.holderEncryptedAmt)
        holderCiphertext = *arg.holderEncryptedAmt;
    else
        holderCiphertext = encryptAmount(*arg.account, *arg.amt, blindingFactor);

    jv[sfHolderEncryptedAmount.jsonName] = strHex(holderCiphertext);

    // Handle Issuer
    if (arg.issuerEncryptedAmt)
        issuerCiphertext = *arg.issuerEncryptedAmt;
    else
        issuerCiphertext = encryptAmount(issuer_, *arg.amt, blindingFactor);

    jv[sfIssuerEncryptedAmount.jsonName] = strHex(issuerCiphertext);

    // Handle Auditor
    if (arg.auditorEncryptedAmt)
        auditorCiphertext = *arg.auditorEncryptedAmt;
    else if (auditor_.has_value() && *arg.fillAuditorEncryptedAmt)
        auditorCiphertext = encryptAmount(*auditor_, *arg.amt, blindingFactor);

    // Update auditor JSON only if ciphertext exists
    if (auditorCiphertext)
        jv[sfAuditorEncryptedAmount.jsonName] = strHex(*auditorCiphertext);
}

void
MPTTester::convert(MPTConvert const& arg)
{
    Json::Value jv;
    if (arg.account)
        jv[sfAccount] = arg.account->human();
    else
        Throw<std::runtime_error>("Account not specified");

    jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
    if (arg.id)
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    else
    {
        if (!id_)
            Throw<std::runtime_error>("MPT has not been created");
        jv[sfMPTokenIssuanceID] = to_string(*id_);
    }

    if (arg.amt)
        jv[sfMPTAmount.jsonName] = std::to_string(*arg.amt);
    if (arg.holderPubKey)
        jv[sfHolderElGamalPublicKey.jsonName] = strHex(*arg.holderPubKey);

    Buffer holderCiphertext;
    Buffer issuerCiphertext;
    std::optional<Buffer> auditorCiphertext;
    Buffer blindingFactor;

    fillConversionCiphertexts(arg, jv, holderCiphertext, issuerCiphertext, auditorCiphertext, blindingFactor);

    jv[sfBlindingFactor.jsonName] = strHex(blindingFactor);
    if (arg.proof)
        jv[sfZKProof.jsonName] = *arg.proof;
    else if (arg.fillSchnorrProof.value_or(arg.holderPubKey.has_value()))
    {
        // whether to automatically generate and attach a Schnorr proof:
        // if fillSchnorrProof is explicitly set, follow its value;
        // otherwise, default to generating the proof only if holder pub key is
        // present.
        auto const contextHash = getConvertContextHash(arg.account->id(), env_.seq(*arg.account), *id_, *arg.amt);

        auto const proof = getSchnorrProof(*arg.account, contextHash);
        if (proof)
            jv[sfZKProof.jsonName] = strHex(*proof);
        else
            jv[sfZKProof.jsonName] = strHex(makeZeroBuffer(ecSchnorrProofLength));
    }

    auto const holderAmt = getBalance(*arg.account);
    auto const prevConfidentialOutstanding = getIssuanceConfidentialBalance();

    auto const prevInboxBalance = getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_INBOX);
    auto const prevSpendingBalance = getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);
    auto const prevIssuerBalance = getDecryptedBalance(*arg.account, ISSUER_ENCRYPTED_BALANCE);

    if (!prevInboxBalance || !prevSpendingBalance || !prevIssuerBalance)
        Throw<std::runtime_error>("Failed to get Pre-convert balance");

    std::optional<uint64_t> prevAuditorBalance;
    if (arg.auditorEncryptedAmt || auditor_)
    {
        prevAuditorBalance = getDecryptedBalance(*arg.account, AUDITOR_ENCRYPTED_BALANCE);
        if (!prevAuditorBalance)
            Throw<std::runtime_error>("Failed to get Pre-convert balance");
    }

    if (submit(arg, jv) == tesSUCCESS)
    {
        auto const postConfidentialOutstanding = getIssuanceConfidentialBalance();
        env_.require(mptbalance(*this, *arg.account, holderAmt - *arg.amt));
        env_.require(requireAny(
            [&]() -> bool { return prevConfidentialOutstanding + *arg.amt == postConfidentialOutstanding; }));

        auto const postInboxBalance = getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_INBOX);
        auto const postIssuerBalance = getDecryptedBalance(*arg.account, ISSUER_ENCRYPTED_BALANCE);
        auto const postSpendingBalance = getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);

        if (!postInboxBalance || !postIssuerBalance || !postSpendingBalance)
            Throw<std::runtime_error>("Failed to get post-convert balance");

        if (arg.auditorEncryptedAmt || auditor_)
        {
            auto const postAuditorBalance = getDecryptedBalance(*arg.account, AUDITOR_ENCRYPTED_BALANCE);

            if (!postAuditorBalance)
                Throw<std::runtime_error>("Failed to get post-convert auditor balance");

            // auditor's encrypted balance is updated correctly
            env_.require(requireAny([&]() -> bool { return *prevAuditorBalance + *arg.amt == *postAuditorBalance; }));
        }
        // spending balance should not change
        env_.require(requireAny([&]() -> bool { return *postSpendingBalance == *prevSpendingBalance; }));

        // issuer's encrypted balance is updated correctly
        env_.require(requireAny([&]() -> bool { return *prevIssuerBalance + *arg.amt == *postIssuerBalance; }));

        // holder's inbox balance is updated correctly
        env_.require(requireAny([&]() -> bool { return *prevInboxBalance + *arg.amt == *postInboxBalance; }));

        // sum of holder's inbox and spending balance should equal to issuer's
        // encrypted balance
        env_.require(
            requireAny([&]() -> bool { return *postInboxBalance + *postSpendingBalance == *postIssuerBalance; }));

        if (arg.holderPubKey)
        {
            env_.require(requireAny([&]() -> bool {
                return forObject(
                    [&](SLEP const& sle) -> bool {
                        if (sle)
                        {
                            auto const holderPubKey = getPubKey(*arg.account);
                            if (!holderPubKey)
                                Throw<std::runtime_error>(
                                    "MPTTester::convert: holder's pubkey is "
                                    "not set");

                            return strHex((*sle)[sfHolderElGamalPublicKey]) == strHex(*holderPubKey);
                        }
                        return false;
                    },
                    *arg.account);
            }));
        }
    }
}

void
MPTTester::send(MPTConfidentialSend const& arg)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::ConfidentialMPTSend;

    if (arg.account)
        jv[sfAccount] = arg.account->human();
    else
        Throw<std::runtime_error>("Account not specified");

    if (arg.dest)
        jv[sfDestination] = arg.dest->human();
    else
        Throw<std::runtime_error>("Destination not specified");

    if (!arg.amt)
        Throw<std::runtime_error>("Amount not specified for testing purposes");

    if (arg.id)
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    else
    {
        if (!id_)
            Throw<std::runtime_error>("MPT has not been created");
        jv[sfMPTokenIssuanceID] = to_string(*id_);
    }

    Buffer const blindingFactor = arg.blindingFactor ? *arg.blindingFactor : generateBlindingFactor();

    // fill in the encrypted amounts if not provided
    auto const senderAmt =
        arg.senderEncryptedAmt ? *arg.senderEncryptedAmt : encryptAmount(*arg.account, *arg.amt, blindingFactor);
    auto const destAmt =
        arg.destEncryptedAmt ? *arg.destEncryptedAmt : encryptAmount(*arg.dest, *arg.amt, blindingFactor);
    auto const issuerAmt =
        arg.issuerEncryptedAmt ? *arg.issuerEncryptedAmt : encryptAmount(issuer_, *arg.amt, blindingFactor);

    std::optional<Buffer> auditorAmt;
    if (arg.auditorEncryptedAmt)
        auditorAmt = arg.auditorEncryptedAmt;
    else if (auditor_.has_value())
        auditorAmt = encryptAmount(*auditor_, *arg.amt, blindingFactor);

    jv[sfSenderEncryptedAmount] = strHex(senderAmt);
    jv[sfDestinationEncryptedAmount] = strHex(destAmt);
    jv[sfIssuerEncryptedAmount] = strHex(issuerAmt);
    if (auditorAmt)
        jv[sfAuditorEncryptedAmount] = strHex(*auditorAmt);

    if (arg.credentials)
    {
        auto& arr(jv[sfCredentialIDs.jsonName] = Json::arrayValue);
        for (auto const& hash : *arg.credentials)
            arr.append(hash);
    }

    // Sender's previous confidential state
    auto const prevSenderInbox = getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_INBOX);
    auto const prevSenderSpending = getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);
    auto const prevSenderIssuer = getDecryptedBalance(*arg.account, ISSUER_ENCRYPTED_BALANCE);
    if (!prevSenderInbox || !prevSenderSpending || !prevSenderIssuer)
        Throw<std::runtime_error>("Failed to get Pre-send balance");

    std::optional<uint64_t> prevSenderAuditor;
    if (arg.auditorEncryptedAmt || auditor_)
    {
        prevSenderAuditor = getDecryptedBalance(*arg.account, AUDITOR_ENCRYPTED_BALANCE);
        if (!prevSenderAuditor)
            Throw<std::runtime_error>("Failed to get Pre-send balance");
    }

    // Destination's previous confidential state
    auto const prevDestInbox = getDecryptedBalance(*arg.dest, HOLDER_ENCRYPTED_INBOX);
    auto const prevDestSpending = getDecryptedBalance(*arg.dest, HOLDER_ENCRYPTED_SPENDING);
    auto const prevDestIssuer = getDecryptedBalance(*arg.dest, ISSUER_ENCRYPTED_BALANCE);
    if (!prevDestInbox || !prevDestSpending || !prevDestIssuer)
        Throw<std::runtime_error>("Failed to get Pre-send balance");

    std::optional<uint64_t> prevDestAuditor;
    if (arg.auditorEncryptedAmt || auditor_)
    {
        prevDestAuditor = getDecryptedBalance(*arg.dest, AUDITOR_ENCRYPTED_BALANCE);
        if (!prevDestAuditor)
            Throw<std::runtime_error>("Failed to get Pre-send balance");
    }

    // Fill in the commitment if not provided
    Buffer amountCommitment, balanceCommitment;
    auto const amountBlindingFactor = generateBlindingFactor();
    if (arg.amountCommitment)
        amountCommitment = *arg.amountCommitment;
    else
        amountCommitment = getPedersenCommitment(*arg.amt, amountBlindingFactor);

    jv[sfAmountCommitment] = strHex(amountCommitment);

    auto const balanceBlindingFactor = generateBlindingFactor();
    if (arg.balanceCommitment)
        balanceCommitment = *arg.balanceCommitment;
    else
        balanceCommitment = getPedersenCommitment(*prevSenderSpending, balanceBlindingFactor);

    jv[sfBalanceCommitment] = strHex(balanceCommitment);

    // Fill in the proof if not provided
    if (arg.proof)
        jv[sfZKProof] = *arg.proof;
    else
    {
        auto const version = getMPTokenVersion(*arg.account);
        auto const ctxHash =
            getSendContextHash(arg.account->id(), env_.seq(*arg.account), *id_, arg.dest->id(), version);

        auto const nRecipients = getConfidentialRecipientCount(auditorAmt.has_value());
        std::vector<ConfidentialRecipient> recipients;

        auto const senderPubKey = getPubKey(*arg.account);
        auto const destPubKey = getPubKey(*arg.dest);
        auto const issuerPubKey = getPubKey(issuer_);

        // If a key is missing, we skip adding the recipient. This intentionally
        // causes proof generation to fail (due to recipient count mismatch),
        // triggering the dummy proof fallback.
        if (senderPubKey)
            recipients.push_back({Slice(*senderPubKey), senderAmt});
        if (destPubKey)
            recipients.push_back({Slice(*destPubKey), destAmt});
        if (issuerPubKey)
            recipients.push_back({Slice(*issuerPubKey), issuerAmt});

        std::optional<Buffer> auditorPubKey;
        if (auditorAmt)
        {
            if (!auditor_)
                Throw<std::runtime_error>("Auditor not registered");

            auditorPubKey = getPubKey(*auditor_);
            if (auditorPubKey)
                recipients.push_back({Slice(*auditorPubKey), *auditorAmt});
        }

        auto const prevEncryptedSenderSpending = getEncryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);

        std::optional<Buffer> proof;

        // Skip proof generation if encrypted balance is missing (e.g.,
        // feature disabled), or when the sender and destination are the
        // same (malformed case causing pcm to be zero). This prevents a
        // crash and allows certain error cases to be tested.
        if (arg.account != arg.dest && prevEncryptedSenderSpending)
        {
            proof = getConfidentialSendProof(
                *arg.account,
                *arg.amt,
                recipients,
                blindingFactor,
                nRecipients,
                ctxHash,
                {.pedersenCommitment = amountCommitment,
                 .amt = *arg.amt,
                 .encryptedAmt = senderAmt,
                 .blindingFactor = amountBlindingFactor},
                {.pedersenCommitment = balanceCommitment,
                 .amt = *prevSenderSpending,
                 .encryptedAmt = *prevEncryptedSenderSpending,
                 .blindingFactor = balanceBlindingFactor});
        }

        if (proof)
            jv[sfZKProof.jsonName] = strHex(*proof);
        else
        {
            size_t const sizeEquality = secp256k1_mpt_prove_same_plaintext_multi_size(nRecipients);
            size_t const dummySize = sizeEquality + 2 * ecPedersenProofLength + ecDoubleBulletproofLength;

            jv[sfZKProof.jsonName] = strHex(makeZeroBuffer(dummySize));
        }
    }

    auto const senderPubAmt = getBalance(*arg.account);
    auto const destPubAmt = getBalance(*arg.dest);
    auto const prevCOA = getIssuanceConfidentialBalance();
    auto const prevOA = getIssuanceOutstandingBalance();

    if (submit(arg, jv) == tesSUCCESS)
    {
        auto const postCOA = getIssuanceConfidentialBalance();
        auto const postOA = getIssuanceOutstandingBalance();

        // Sender's post confidential state
        auto const postSenderInbox = getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_INBOX);
        auto const postSenderSpending = getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);
        auto const postSenderIssuer = getDecryptedBalance(*arg.account, ISSUER_ENCRYPTED_BALANCE);

        if (!postSenderInbox || !postSenderSpending || !postSenderIssuer)
            Throw<std::runtime_error>("Failed to get Post-send balance");

        // Destination's post confidential state
        auto const postDestInbox = getDecryptedBalance(*arg.dest, HOLDER_ENCRYPTED_INBOX);
        auto const postDestSpending = getDecryptedBalance(*arg.dest, HOLDER_ENCRYPTED_SPENDING);
        auto const postDestIssuer = getDecryptedBalance(*arg.dest, ISSUER_ENCRYPTED_BALANCE);

        if (!postDestInbox || !postDestSpending || !postDestIssuer)
            Throw<std::runtime_error>("Failed to get Post-send balance");

        // Public balances unchanged
        env_.require(mptbalance(*this, *arg.account, senderPubAmt));
        env_.require(mptbalance(*this, *arg.dest, destPubAmt));

        // OA and COA unchanged
        env_.require(requireAny([&]() -> bool { return prevOA == postOA; }));
        env_.require(requireAny([&]() -> bool { return prevCOA == postCOA; }));

        // Verify sender changes
        env_.require(requireAny([&]() -> bool {
            return *prevSenderSpending >= *arg.amt && *postSenderSpending == *prevSenderSpending - *arg.amt;
        }));
        env_.require(requireAny([&]() -> bool { return postSenderInbox == prevSenderInbox; }));
        env_.require(requireAny([&]() -> bool {
            return *prevSenderIssuer >= *arg.amt && *postSenderIssuer == *prevSenderIssuer - *arg.amt;
        }));

        // Verify destination changes
        env_.require(requireAny([&]() -> bool { return *postDestInbox == *prevDestInbox + *arg.amt; }));
        env_.require(requireAny([&]() -> bool { return *postDestSpending == *prevDestSpending; }));
        env_.require(requireAny([&]() -> bool { return *postDestIssuer == *prevDestIssuer + *arg.amt; }));

        // Cross checks
        env_.require(requireAny([&]() -> bool { return *postSenderInbox + *postSenderSpending == *postSenderIssuer; }));
        env_.require(requireAny([&]() -> bool { return *postDestInbox + *postDestSpending == *postDestIssuer; }));

        if (arg.auditorEncryptedAmt || auditor_)
        {
            auto const postSenderAuditor = getDecryptedBalance(*arg.account, AUDITOR_ENCRYPTED_BALANCE);
            auto const postDestAuditor = getDecryptedBalance(*arg.dest, AUDITOR_ENCRYPTED_BALANCE);
            if (!postSenderAuditor || !postDestAuditor)
                Throw<std::runtime_error>("Failed to get Post-send balance");

            env_.require(requireAny([&]() -> bool {
                return *postSenderAuditor == *postSenderIssuer && *postDestAuditor == *postDestIssuer;
            }));

            // verify sender
            env_.require(requireAny([&]() -> bool {
                return prevSenderAuditor >= *arg.amt && *postSenderAuditor == *prevSenderAuditor - *arg.amt;
            }));

            // verify dest
            env_.require(requireAny([&]() -> bool { return *postDestAuditor == *prevDestAuditor + *arg.amt; }));
        }
    }
}

void
MPTTester::confidentialClaw(MPTConfidentialClawback const& arg)
{
    Json::Value jv;
    auto const account = arg.account ? *arg.account : issuer_;
    jv[sfAccount] = account.human();

    if (arg.holder)
        jv[sfHolder] = arg.holder->human();
    else
        Throw<std::runtime_error>("Holder not specified");

    jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
    if (arg.id)
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    else if (id_)
        jv[sfMPTokenIssuanceID] = to_string(*id_);
    else
        Throw<std::runtime_error>("MPT has not been created");

    if (arg.amt)
        jv[sfMPTAmount] = std::to_string(*arg.amt);

    if (arg.proof)
        jv[sfZKProof] = *arg.proof;
    else
    {
        std::uint32_t const seq = env_.seq(account);
        uint256 const contextHash = getClawbackContextHash(account.id(), seq, *id_, *arg.amt, arg.holder->id());

        auto const privKey = getPrivKey(account);
        if (!privKey || privKey->size() != ecPrivKeyLength)
            Throw<std::runtime_error>("Failed to get clawback private key");

        auto const proof = getClawbackProof(*arg.holder, *arg.amt, *privKey, contextHash);

        if (proof)
            jv[sfZKProof] = strHex(*proof);
        else
            jv[sfZKProof] = strHex(makeZeroBuffer(ecEqualityProofLength));
    }

    auto const holderPubAmt = getBalance(*arg.holder);
    auto const prevCOA = getIssuanceConfidentialBalance();
    auto const prevOA = getIssuanceOutstandingBalance();

    if (submit(arg, jv) == tesSUCCESS)
    {
        auto const postCOA = getIssuanceConfidentialBalance();
        auto const postOA = getIssuanceOutstandingBalance();

        // Verify holder's public balance is unchanged
        env_.require(mptbalance(*this, *arg.holder, holderPubAmt));

        // Verify COA and OA are reduced correctly
        env_.require(requireAny([&]() -> bool { return prevCOA >= *arg.amt && postCOA == prevCOA - *arg.amt; }));
        env_.require(requireAny([&]() -> bool { return prevOA >= *arg.amt && postOA == prevOA - *arg.amt; }));

        // Verify holder's confidential balances are zeroed out
        env_.require(
            requireAny([&]() -> bool { return getDecryptedBalance(*arg.holder, HOLDER_ENCRYPTED_INBOX) == 0; }));
        env_.require(
            requireAny([&]() -> bool { return getDecryptedBalance(*arg.holder, HOLDER_ENCRYPTED_SPENDING) == 0; }));
        env_.require(
            requireAny([&]() -> bool { return getDecryptedBalance(*arg.holder, ISSUER_ENCRYPTED_BALANCE) == 0; }));
        env_.require(
            requireAny([&]() -> bool { return getDecryptedBalance(*arg.holder, AUDITOR_ENCRYPTED_BALANCE) == 0; }));
    }
}

void
MPTTester::generateKeyPair(Account const& account)
{
    unsigned char privKey[ecPrivKeyLength];
    secp256k1_pubkey pubKey;
    if (!secp256k1_elgamal_generate_keypair(secp256k1Context(), privKey, &pubKey))
        Throw<std::runtime_error>("failed to generate key pair");

    // Serialize public key to compressed format (33 bytes)
    unsigned char compressedPubKey[ecPubKeyLength];
    size_t outLen = ecPubKeyLength;
    if (secp256k1_ec_pubkey_serialize(
            secp256k1Context(), compressedPubKey, &outLen, &pubKey, SECP256K1_EC_COMPRESSED) != 1 ||
        outLen != ecPubKeyLength)
        Throw<std::runtime_error>("failed to serialize public key");

    pubKeys.insert({account.id(), Buffer{compressedPubKey, ecPubKeyLength}});
    privKeys.insert({account.id(), Buffer{privKey, ecPrivKeyLength}});
}

std::optional<Buffer>
MPTTester::getPubKey(Account const& account) const
{
    auto it = pubKeys.find(account.id());
    if (it != pubKeys.end())
    {
        return it->second;
    }

    return std::nullopt;
}

std::optional<Buffer>
MPTTester::getPrivKey(Account const& account) const
{
    auto it = privKeys.find(account.id());
    if (it != privKeys.end())
    {
        return it->second;
    }

    return std::nullopt;
}

Buffer
MPTTester::encryptAmount(Account const& account, uint64_t const amt, Buffer const& blindingFactor) const
{
    if (auto const pubKey = getPubKey(account))
    {
        if (auto const result = xrpl::encryptAmount(amt, *pubKey, blindingFactor))
            return *result;
    }

    // Return a dummy buffer on failure to allow testing of
    // failures that occur prior to encryption.
    return makeZeroBuffer(ecGamalEncryptedTotalLength);
}

std::optional<uint64_t>
MPTTester::decryptAmount(Account const& account, Buffer const& amt) const
{
    if (amt.size() != ecGamalEncryptedTotalLength)
        return std::nullopt;

    secp256k1_pubkey c1;
    secp256k1_pubkey c2;

    if (!makeEcPair(amt, c1, c2))
        return std::nullopt;

    auto const privKey = getPrivKey(account);
    if (!privKey || privKey->size() != ecPrivKeyLength)
        return std::nullopt;

    uint64_t decryptedAmt;
    if (!secp256k1_elgamal_decrypt(secp256k1Context(), &decryptedAmt, &c1, &c2, privKey->data()))
    {
        return std::nullopt;
    }

    return decryptedAmt;
}

std::optional<uint64_t>
MPTTester::getDecryptedBalance(Account const& account, EncryptedBalanceType balanceType) const

{
    auto encryptedAmt = getEncryptedBalance(account, balanceType);

    // Return zero to test cases like Feature Disabled, where the ledger object
    // does not exist.
    if (!encryptedAmt)
        return 0;

    Account decryptor = account;

    if (balanceType == ISSUER_ENCRYPTED_BALANCE)
        decryptor = issuer_;
    else if (balanceType == AUDITOR_ENCRYPTED_BALANCE)
    {
        if (!auditor_)
            return std::nullopt;
        decryptor = *auditor_;
    }

    return decryptAmount(decryptor, *encryptedAmt);
};

void
MPTTester::mergeInbox(MPTMergeInbox const& arg)
{
    Json::Value jv;
    if (arg.account)
        jv[sfAccount] = arg.account->human();
    else
        Throw<std::runtime_error>("Account not specified");
    if (arg.id)
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    else
    {
        if (!id_)
            Throw<std::runtime_error>("MPT has not been created");
        jv[sfMPTokenIssuanceID] = to_string(*id_);
    }

    jv[sfTransactionType] = jss::ConfidentialMPTMergeInbox;
    auto const prevInboxBalance = getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_INBOX);
    auto const prevSpendingBalance = getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);
    auto const prevIssuerBalance = getDecryptedBalance(*arg.account, ISSUER_ENCRYPTED_BALANCE);

    if (!prevInboxBalance || !prevSpendingBalance || !prevIssuerBalance)
        Throw<std::runtime_error>("Failed to get pre-mergeInbox balances");

    if (submit(arg, jv) == tesSUCCESS)
    {
        auto const postInboxBalance = getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_INBOX);
        auto const postSpendingBalance = getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);
        auto const postIssuerBalance = getDecryptedBalance(*arg.account, ISSUER_ENCRYPTED_BALANCE);

        if (!postInboxBalance || !postSpendingBalance || !postIssuerBalance)
            Throw<std::runtime_error>("Failed to get post-mergeInbox balances");

        env_.require(requireAny([&]() -> bool {
            return *postSpendingBalance == *prevInboxBalance + *prevSpendingBalance && *postInboxBalance == 0;
        }));

        env_.require(requireAny([&]() -> bool { return *prevIssuerBalance == *postIssuerBalance; }));

        env_.require(
            requireAny([&]() -> bool { return *postSpendingBalance + *postInboxBalance == *postIssuerBalance; }));
    }
}

std::int64_t
MPTTester::getIssuanceOutstandingBalance() const
{
    if (!id_)
        Throw<std::runtime_error>("Issuance ID does not exist");

    auto const sle = env_.current()->read(keylet::mptIssuance(*id_));

    if (!sle || !sle->isFieldPresent(sfOutstandingAmount))
        Throw<std::runtime_error>("Issuance object does not contain outstanding amount");

    return (*sle)[sfOutstandingAmount];
}

std::uint32_t
MPTTester::getMPTokenVersion(Account const account) const
{
    if (!id_)
        Throw<std::runtime_error>("Issuance ID does not exist");

    auto const sle = env_.current()->read(keylet::mptoken(*id_, account));

    // return 0 here instead of throwing an exception since tests for
    // preclaim will check if the MPToken exists
    if (!sle)
        return 0;

    return (*sle)[~sfConfidentialBalanceVersion].value_or(0);
}

void
MPTTester::convertBack(MPTConvertBack const& arg)
{
    Json::Value jv;
    if (arg.account)
        jv[sfAccount] = arg.account->human();
    else
        Throw<std::runtime_error>("Account not specified");

    jv[jss::TransactionType] = jss::ConfidentialMPTConvertBack;
    if (arg.id)
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    else
    {
        if (!id_)
            Throw<std::runtime_error>("MPT has not been created");
        jv[sfMPTokenIssuanceID] = to_string(*id_);
    }

    if (arg.amt)
        jv[sfMPTAmount.jsonName] = std::to_string(*arg.amt);

    Buffer holderCiphertext;
    Buffer issuerCiphertext;
    std::optional<Buffer> auditorCiphertext;
    Buffer blindingFactor;

    fillConversionCiphertexts(arg, jv, holderCiphertext, issuerCiphertext, auditorCiphertext, blindingFactor);

    jv[sfBlindingFactor] = strHex(blindingFactor);

    auto const prevInboxBalance = getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_INBOX);
    auto const prevSpendingBalance = getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);
    auto const prevIssuerBalance = getDecryptedBalance(*arg.account, ISSUER_ENCRYPTED_BALANCE);

    if (!prevInboxBalance || !prevSpendingBalance || !prevIssuerBalance)
        Throw<std::runtime_error>("Failed to get Pre-convertBack balance");

    Buffer pedersenCommitment;
    Buffer pcBlindingFactor = generateBlindingFactor();
    if (arg.pedersenCommitment)
        pedersenCommitment = *arg.pedersenCommitment;
    else
        pedersenCommitment = getPedersenCommitment(*prevSpendingBalance, pcBlindingFactor);

    jv[sfBalanceCommitment] = strHex(pedersenCommitment);

    if (arg.proof)
        jv[sfZKProof.jsonName] = strHex(*arg.proof);
    else
    {
        auto const version = getMPTokenVersion(*arg.account);

        // if the caller generated ciphertexts themselves, they should also
        // generate the proof themselves from the blinding factor
        uint256 const contextHash =
            getConvertBackContextHash(arg.account->id(), env_.seq(*arg.account), *id_, *arg.amt, version);
        auto const prevEncryptedSpendingBalance = getEncryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);

        Buffer proof;
        // generate a dummy proof if no encrypted amount field, so that other
        // preflight/preclaim are checked
        if (!prevEncryptedSpendingBalance)
            proof = makeZeroBuffer(ecPedersenProofLength + ecSingleBulletproofLength);
        else
        {
            proof = getConvertBackProof(
                *arg.account,
                *arg.amt,
                contextHash,
                {
                    .pedersenCommitment = pedersenCommitment,
                    .amt = *prevSpendingBalance,
                    .encryptedAmt = *prevEncryptedSpendingBalance,
                    .blindingFactor = pcBlindingFactor,
                });
        }
        jv[sfZKProof] = strHex(proof);
    }

    auto const holderAmt = getBalance(*arg.account);
    auto const prevConfidentialOutstanding = getIssuanceConfidentialBalance();

    std::optional<uint64_t> prevAuditorBalance;
    if (arg.auditorEncryptedAmt || auditor_)
    {
        prevAuditorBalance = getDecryptedBalance(*arg.account, AUDITOR_ENCRYPTED_BALANCE);
        if (!prevAuditorBalance)
            Throw<std::runtime_error>("Failed to get Pre-convertBack balance");
    }

    if (submit(arg, jv) == tesSUCCESS)
    {
        auto const postConfidentialOutstanding = getIssuanceConfidentialBalance();
        env_.require(mptbalance(*this, *arg.account, holderAmt + *arg.amt));
        env_.require(requireAny(
            [&]() -> bool { return prevConfidentialOutstanding - *arg.amt == postConfidentialOutstanding; }));

        auto const postInboxBalance = getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_INBOX);
        auto const postIssuerBalance = getDecryptedBalance(*arg.account, ISSUER_ENCRYPTED_BALANCE);
        auto const postSpendingBalance = getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);

        if (!postInboxBalance || !postIssuerBalance || !postSpendingBalance)
            Throw<std::runtime_error>("Failed to get post-convertBack balance");

        if (arg.auditorEncryptedAmt || auditor_)
        {
            auto const postAuditorBalance = getDecryptedBalance(*arg.account, AUDITOR_ENCRYPTED_BALANCE);

            if (!postAuditorBalance)
                Throw<std::runtime_error>("Failed to get post-convertBack balance");

            // auditor's encrypted balance is updated correctly
            env_.require(requireAny([&]() -> bool { return *prevAuditorBalance - *arg.amt == *postAuditorBalance; }));
        }

        // inbox balance should not change
        env_.require(requireAny([&]() -> bool { return *postInboxBalance == *prevInboxBalance; }));

        // issuer's encrypted balance is updated correctly
        env_.require(requireAny([&]() -> bool { return *prevIssuerBalance - *arg.amt == *postIssuerBalance; }));

        // holder's spending balance is updated correctly
        env_.require(requireAny([&]() -> bool { return *prevSpendingBalance - *arg.amt == *postSpendingBalance; }));

        // sum of holder's inbox and spending balance should equal to issuer's
        // encrypted balance
        env_.require(
            requireAny([&]() -> bool { return *postInboxBalance + *postSpendingBalance == *postIssuerBalance; }));
    }
}

Buffer
MPTTester::getAmountLinkageProof(
    Buffer const& pubKey,
    Buffer const& blindingFactor,
    uint256 const& contextHash,
    PedersenProofParams const& params) const
{
    if (params.blindingFactor.size() != ecBlindingFactorLength ||
        params.pedersenCommitment.size() != ecPedersenCommitmentLength || pubKey.size() != ecPubKeyLength ||
        params.encryptedAmt.size() != ecGamalEncryptedTotalLength || blindingFactor.size() != ecBlindingFactorLength)
        return makeZeroBuffer(ecPedersenProofLength);

    secp256k1_pubkey c1, c2;
    auto const ctx = secp256k1Context();
    if (!secp256k1_ec_pubkey_parse(ctx, &c1, params.encryptedAmt.data(), ecGamalEncryptedLength) ||
        !secp256k1_ec_pubkey_parse(
            ctx, &c2, params.encryptedAmt.data() + ecGamalEncryptedLength, ecGamalEncryptedLength))
    {
        return Buffer();
    }

    secp256k1_pubkey pk;
    if (secp256k1_ec_pubkey_parse(ctx, &pk, pubKey.data(), ecPubKeyLength) != 1)
        return Buffer();

    secp256k1_pubkey pcm;
    if (secp256k1_ec_pubkey_parse(ctx, &pcm, params.pedersenCommitment.data(), ecPedersenCommitmentLength) != 1)
        return Buffer();

    Buffer proof(ecPedersenProofLength);
    if (secp256k1_elgamal_pedersen_link_prove(
            ctx,
            proof.data(),
            &c1,
            &c2,
            &pk,
            &pcm,
            params.amt,
            blindingFactor.data(),
            params.blindingFactor.data(),
            contextHash.data()) != 1)
    {
        Throw<std::runtime_error>("Amount Linkage Proof generation failed");
    }

    return proof;
}

Buffer
MPTTester::getBalanceLinkageProof(
    Account const& account,
    uint256 const& contextHash,
    Buffer const& pubKey,
    PedersenProofParams const& params) const
{
    if (params.blindingFactor.size() != ecBlindingFactorLength ||
        params.pedersenCommitment.size() != ecPedersenCommitmentLength || pubKey.size() != ecPubKeyLength ||
        params.encryptedAmt.size() != ecGamalEncryptedTotalLength)
        return makeZeroBuffer(ecPedersenProofLength);

    secp256k1_pubkey c1, c2;
    auto const ctx = secp256k1Context();
    if (!secp256k1_ec_pubkey_parse(ctx, &c1, params.encryptedAmt.data(), ecGamalEncryptedLength) ||
        !secp256k1_ec_pubkey_parse(
            ctx, &c2, params.encryptedAmt.data() + ecGamalEncryptedLength, ecGamalEncryptedLength))
    {
        return makeZeroBuffer(ecPedersenProofLength);
    }

    secp256k1_pubkey pk;
    if (secp256k1_ec_pubkey_parse(ctx, &pk, pubKey.data(), ecPubKeyLength) != 1)
        return Buffer();

    secp256k1_pubkey pcm;
    if (secp256k1_ec_pubkey_parse(ctx, &pcm, params.pedersenCommitment.data(), ecPedersenCommitmentLength) != 1)
        return Buffer();

    Buffer proof(ecPedersenProofLength);

    auto const privKey = getPrivKey(account);
    if (!privKey || privKey->size() != ecPrivKeyLength)
        Throw<std::runtime_error>("Failed to get Pedersen proof private key");

    if (secp256k1_elgamal_pedersen_link_prove(
            ctx,
            proof.data(),
            &pk,
            &c2,
            &c1,
            &pcm,
            params.amt,
            privKey->data(),
            params.blindingFactor.data(),
            contextHash.data()) != 1)
        Throw<std::runtime_error>("Pedersen proof generation failed");

    return proof;
}

Buffer
MPTTester::getBulletproof(
    std::vector<std::uint64_t> const& values,
    std::vector<Buffer> const& blindingFactors,
    uint256 const& contextHash) const
{
    std::size_t const m = values.size();

    if (m == 0 || m > 2 || m != blindingFactors.size())
        Throw<std::runtime_error>("getBulletproof: invalid input parameters");

    for (auto const& bf : blindingFactors)
    {
        if (bf.size() != ecBlindingFactorLength)
            Throw<std::runtime_error>("Invalid blinding factor length");
    }

    // Flatten blinding factors into contiguous memory (m * 32 bytes)
    std::vector<unsigned char> blindingsFlat(m * ecBlindingFactorLength);
    for (std::size_t i = 0; i < m; ++i)
        std::memcpy(
            blindingsFlat.data() + i * ecBlindingFactorLength, blindingFactors[i].data(), ecBlindingFactorLength);

    secp256k1_pubkey pk_base;
    if (secp256k1_mpt_get_h_generator(secp256k1Context(), &pk_base) != 1)
        Throw<std::runtime_error>("Failed to get H generator");

    // Proof size scales with m; use safe upper bound
    Buffer bulletproof(4096);
    std::size_t proofLen = 4096;

    if (secp256k1_bulletproof_prove_agg(
            secp256k1Context(),
            bulletproof.data(),
            &proofLen,
            values.data(),
            blindingsFlat.data(),
            m,
            &pk_base,
            contextHash.data()) != 1)
    {
        Throw<std::runtime_error>("Bulletproof generation failed");
    }

    std::size_t const expectedLen = (m == 1) ? ecSingleBulletproofLength : ecDoubleBulletproofLength;
    if (proofLen != expectedLen)
        Throw<std::runtime_error>("Unexpected bulletproof length");

    return Buffer(bulletproof.data(), proofLen);
}

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
