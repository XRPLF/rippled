#include <test/jtx.h>

#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include "test/jtx/mpt.h"
#include <openssl/rand.h>

#include <cstdint>
#include <string>

namespace ripple {
namespace test {
namespace jtx {

ripple::Buffer
generatePlaceholderCiphertext()
{
    Buffer buf(ecGamalEncryptedTotalLength);
    std::memset(buf.data(), 0, ecGamalEncryptedTotalLength);

    buf.data()[0] = 0x02;
    buf.data()[ecGamalEncryptedLength] = 0x02;

    buf.data()[ecGamalEncryptedLength - 1] = 0x01;
    buf.data()[ecGamalEncryptedTotalLength - 1] = 0x01;

    return buf;
}

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
    , auditor_(arg.auditor)
    , close_(arg.close)
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
    Json::Value jv = setjv(
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
            if (auto const account =
                    (arg.holder ? std::get_if<Account>(&(*arg.holder))
                                : nullptr))
                require(*account, false);
        }

        if (arg.issuerPubKey)
        {
            env_.require(requireAny([&]() -> bool {
                return forObject([&](SLEP const& sle) -> bool {
                    if (sle)
                    {
                        return strHex((*sle)[sfIssuerElGamalPublicKey]) ==
                            strHex(getPubKey(issuer_));
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
                        if (!auditor_)
                            Throw<std::runtime_error>(
                                "MPTTester::set: auditor is not set");
                        return strHex((*sle)[sfAuditorElGamalPublicKey]) ==
                            strHex(getPubKey(*auditor_));
                    }
                    return false;
                });
            }));
        }
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
MPTTester::checkIssuanceConfidentialBalance(std::int64_t expectedAmount) const
{
    return forObject([&](SLEP const& sle) {
        return expectedAmount ==
            (*sle)[~sfConfidentialOutstandingAmount].value_or(0);
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

std::int64_t
MPTTester::getIssuanceConfidentialBalance() const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");

    if (auto const sle = env_.le(keylet::mptIssuance(*id_)))
        return (*sle)[~sfConfidentialOutstandingAmount].value_or(0);

    return 0;
}

Buffer
MPTTester::getClawbackProof(
    Account const& holder,
    std::uint64_t amount,
    Buffer const& privateKey,
    uint256 const& ctxHash) const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");

    auto const sleHolder = env_.le(keylet::mptoken(*id_, holder.id()));
    auto const sleIssuance = env_.le(keylet::mptIssuance(*id_));

    // helper to generate a dummy proof, so that other preclaim tests can
    // proceed
    auto const getDummyProof = []() {
        Buffer dummy(ecEqualityProofLength);
        std::memset(dummy.data(), 0, ecEqualityProofLength);
        return dummy;
    };

    if (!sleHolder)
        return getDummyProof();

    if (!sleIssuance)
        Throw<std::runtime_error>("Issuance object not found");

    auto const ciphertextBlob = sleHolder->getFieldVL(sfIssuerEncryptedBalance);
    if (ciphertextBlob.size() == 0)
        return getDummyProof();

    auto const pubKeyBlob = sleIssuance->getFieldVL(sfIssuerElGamalPublicKey);
    Slice const ciphertext(ciphertextBlob.data(), ciphertextBlob.size());
    Slice const pubKey(pubKeyBlob.data(), pubKeyBlob.size());

    if (ciphertextBlob.size() != ecGamalEncryptedTotalLength)
        Throw<std::runtime_error>("Invalid Ciphertext length");

    secp256k1_pubkey c1, c2;
    auto const ctx = secp256k1Context();
    if (!secp256k1_ec_pubkey_parse(
            ctx, &c1, ciphertextBlob.data(), ecGamalEncryptedLength) ||
        !secp256k1_ec_pubkey_parse(
            ctx,
            &c2,
            ciphertextBlob.data() + ecGamalEncryptedLength,
            ecGamalEncryptedLength))
    {
        Throw<std::runtime_error>("Invalid Ciphertext");
    }

    secp256k1_pubkey pk;
    std::memcpy(pk.data, pubKeyBlob.data(), ecPubKeyLength);
    Buffer proof(ecEqualityProofLength);

    if (secp256k1_equality_plaintext_prove(
            ctx,
            proof.data(),
            &pk,
            &c2,
            &c1,
            amount,
            privateKey.data(),
            ctxHash.data()) != 1)
    {
        Throw<std::runtime_error>("Proof generation failed");
    }

    return proof;
}

Buffer
MPTTester::getSchnorrProof(Account const& account, uint256 const& ctxHash) const
{
    auto const pubKey = getPubKey(account);
    auto const privKey = getPrivKey(account);

    if (pubKey.size() != ecPubKeyLength)
        Throw<std::runtime_error>("Invalid public key size");

    secp256k1_pubkey pk;
    std::memcpy(pk.data, pubKey.data(), ecPubKeyLength);

    Buffer proof(ecSchnorrProofLength);

    if (secp256k1_mpt_pok_sk_prove(
            secp256k1Context(),
            proof.data(),
            &pk,
            privKey.data(),
            ctxHash.data()) != 1)
    {
        Throw<std::runtime_error>("Schnorr Proof generation failed");
    }

    return proof;
}

Buffer
MPTTester::getConvertBackProof(
    Account const& holder,
    std::uint64_t amount,
    uint256 const& ctxHash,
    Buffer const& holderCiphertext,
    Buffer const& issuerCiphertext,
    std::optional<Buffer> const& auditorCiphertext,
    Buffer const& blindingFactor) const
{
    // todo: incoporate pederson and range proof

    return Buffer{};
}

std::optional<Buffer>
MPTTester::getEncryptedBalance(
    Account const& account,
    EncryptedBalanceType option) const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");

    if (auto const sle = env_.le(keylet::mptoken(*id_, account.id())))
    {
        if (option == HOLDER_ENCRYPTED_INBOX &&
            sle->isFieldPresent(sfConfidentialBalanceInbox))
            return Buffer(
                (*sle)[sfConfidentialBalanceInbox].data(),
                (*sle)[sfConfidentialBalanceInbox].size());
        if (option == HOLDER_ENCRYPTED_SPENDING &&
            sle->isFieldPresent(sfConfidentialBalanceSpending))
            return Buffer(
                (*sle)[sfConfidentialBalanceSpending].data(),
                (*sle)[sfConfidentialBalanceSpending].size());
        if (option == ISSUER_ENCRYPTED_BALANCE &&
            sle->isFieldPresent(sfIssuerEncryptedBalance))
            return Buffer(
                (*sle)[sfIssuerEncryptedBalance].data(),
                (*sle)[sfIssuerEncryptedBalance].size());
        if (option == AUDITOR_ENCRYPTED_BALANCE &&
            sle->isFieldPresent(sfAuditorEncryptedBalance))
            return Buffer(
                (*sle)[sfAuditorEncryptedBalance].data(),
                (*sle)[sfAuditorEncryptedBalance].size());
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
MPTTester::operator()(std::uint64_t amount) const
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
    blindingFactor =
        arg.blindingFactor ? *arg.blindingFactor : generateBlindingFactor();

    // Handle Holder
    if (arg.holderEncryptedAmt)
        holderCiphertext = *arg.holderEncryptedAmt;
    else
        holderCiphertext =
            encryptAmount(*arg.account, *arg.amt, blindingFactor);

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
    else if (auditor())
        auditorCiphertext = encryptAmount(*auditor(), *arg.amt, blindingFactor);

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

    jv[jss::TransactionType] = jss::ConfidentialConvert;
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

    fillConversionCiphertexts(
        arg,
        jv,
        holderCiphertext,
        issuerCiphertext,
        auditorCiphertext,
        blindingFactor);

    jv[sfBlindingFactor.jsonName] = strHex(blindingFactor);
    if (arg.proof)
        jv[sfZKProof.jsonName] = *arg.proof;
    else if (arg.fillSchnorrProof.value_or(arg.holderPubKey.has_value()))
    {
        // whether to automatically generate and attach a Schnorr proof:
        // if fillSchnorrProof is explicitly set, follow its value;
        // otherwise, default to generating the proof only if holder pub key is
        // present.
        auto const ctxHash = getConvertContextHash(
            arg.account->id(), env_.seq(*arg.account), *id_, *arg.amt);

        Buffer proof = getSchnorrProof(*arg.account, ctxHash);
        jv[sfZKProof.jsonName] = strHex(proof);
    }

    auto const holderAmt = getBalance(*arg.account);
    auto const prevConfidentialOutstanding = getIssuanceConfidentialBalance();

    uint64_t prevInboxBalance =
        getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_INBOX);
    uint64_t prevSpendingBalance =
        getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);
    uint64_t prevIssuerBalance =
        getDecryptedBalance(*arg.account, ISSUER_ENCRYPTED_BALANCE);
    [[maybe_unused]] uint64_t prevAuditorBalance =
        getDecryptedBalance(*arg.account, AUDITOR_ENCRYPTED_BALANCE);

    if (submit(arg, jv) == tesSUCCESS)
    {
        auto const postConfidentialOutstanding =
            getIssuanceConfidentialBalance();
        env_.require(mptbalance(*this, *arg.account, holderAmt - *arg.amt));
        env_.require(requireAny([&]() -> bool {
            return prevConfidentialOutstanding + *arg.amt ==
                postConfidentialOutstanding;
        }));

        uint64_t postInboxBalance =
            getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_INBOX);
        uint64_t postIssuerBalance =
            getDecryptedBalance(*arg.account, ISSUER_ENCRYPTED_BALANCE);
        uint64_t postSpendingBalance =
            getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);

        if (arg.auditorEncryptedAmt || auditor_)
        {
            uint64_t postAuditorBalance =
                getDecryptedBalance(*arg.account, AUDITOR_ENCRYPTED_BALANCE);
            // auditor's encrypted balance is updated correctly
            env_.require(requireAny([&]() -> bool {
                return prevAuditorBalance + *arg.amt == postAuditorBalance;
            }));
        }
        // spending balance should not change
        env_.require(requireAny([&]() -> bool {
            return postSpendingBalance == prevSpendingBalance;
        }));

        // issuer's encrypted balance is updated correctly
        env_.require(requireAny([&]() -> bool {
            return prevIssuerBalance + *arg.amt == postIssuerBalance;
        }));

        // holder's inbox balance is updated correctly
        env_.require(requireAny([&]() -> bool {
            return prevInboxBalance + *arg.amt == postInboxBalance;
        }));

        // sum of holder's inbox and spending balance should equal to issuer's
        // encrypted balance
        env_.require(requireAny([&]() -> bool {
            return postInboxBalance + postSpendingBalance == postIssuerBalance;
        }));

        if (arg.holderPubKey)
        {
            env_.require(requireAny([&]() -> bool {
                return forObject(
                    [&](SLEP const& sle) -> bool {
                        if (sle)
                        {
                            return strHex((*sle)[sfHolderElGamalPublicKey]) ==
                                strHex(getPubKey(*arg.account));
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
    if (arg.account)
        jv[sfAccount] = arg.account->human();
    else
        Throw<std::runtime_error>("Account not specified");

    if (arg.dest)
        jv[sfDestination] = arg.dest->human();
    else
        Throw<std::runtime_error>("Destination not specified");

    jv[jss::TransactionType] = jss::ConfidentialSend;
    if (arg.id)
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    else
    {
        if (!id_)
            Throw<std::runtime_error>("MPT has not been created");
        jv[sfMPTokenIssuanceID] = to_string(*id_);
    }

    Buffer const blindingFactor =
        arg.blindingFactor ? *arg.blindingFactor : generateBlindingFactor();

    // Generate the encrypted amounts if not provided
    if (arg.senderEncryptedAmt)
        jv[sfSenderEncryptedAmount] = strHex(*arg.senderEncryptedAmt);
    else
        jv[sfSenderEncryptedAmount] =
            strHex(encryptAmount(*arg.account, *arg.amt, blindingFactor));

    if (arg.destEncryptedAmt)
        jv[sfDestinationEncryptedAmount] = strHex(*arg.destEncryptedAmt);
    else
        jv[sfDestinationEncryptedAmount] =
            strHex(encryptAmount(*arg.dest, *arg.amt, blindingFactor));

    if (arg.issuerEncryptedAmt)
        jv[sfIssuerEncryptedAmount] = strHex(*arg.issuerEncryptedAmt);
    else
        jv[sfIssuerEncryptedAmount] =
            strHex(encryptAmount(issuer_, *arg.amt, blindingFactor));

    if (arg.auditorEncryptedAmt)
        jv[sfAuditorEncryptedAmount] = strHex(*arg.auditorEncryptedAmt);
    else if (auditor())
        jv[sfAuditorEncryptedAmount] =
            strHex(encryptAmount(*auditor(), *arg.amt, blindingFactor));

    if (arg.proof)
        jv[sfZKProof] = *arg.proof;

    if (arg.credentials)
    {
        auto& arr(jv[sfCredentialIDs.jsonName] = Json::arrayValue);
        for (auto const& hash : *arg.credentials)
            arr.append(hash);
    }

    auto const senderPubAmt = getBalance(*arg.account);
    auto const destPubAmt = getBalance(*arg.dest);
    auto const prevCOA = getIssuanceConfidentialBalance();
    auto const prevOA = getIssuanceOutstandingBalance();

    // Sender's previous confidential state
    uint64_t prevSenderInbox =
        getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_INBOX);
    uint64_t prevSenderSpending =
        getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);
    uint64_t prevSenderIssuer =
        getDecryptedBalance(*arg.account, ISSUER_ENCRYPTED_BALANCE);
    [[maybe_unused]] uint64_t prevSenderAuditor =
        getDecryptedBalance(*arg.account, AUDITOR_ENCRYPTED_BALANCE);

    // Destination's previous confidential state
    uint64_t prevDestInbox =
        getDecryptedBalance(*arg.dest, HOLDER_ENCRYPTED_INBOX);
    uint64_t prevDestSpending =
        getDecryptedBalance(*arg.dest, HOLDER_ENCRYPTED_SPENDING);
    uint64_t prevDestIssuer =
        getDecryptedBalance(*arg.dest, ISSUER_ENCRYPTED_BALANCE);
    [[maybe_unused]] uint64_t prevDestAuditor =
        getDecryptedBalance(*arg.dest, AUDITOR_ENCRYPTED_BALANCE);

    if (submit(arg, jv) == tesSUCCESS)
    {
        auto const postCOA = getIssuanceConfidentialBalance();
        auto const postOA = getIssuanceOutstandingBalance();

        // Sender's post confidential state
        uint64_t postSenderInbox =
            getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_INBOX);
        uint64_t postSenderSpending =
            getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);
        uint64_t postSenderIssuer =
            getDecryptedBalance(*arg.account, ISSUER_ENCRYPTED_BALANCE);

        // Destination's post confidential state
        uint64_t postDestInbox =
            getDecryptedBalance(*arg.dest, HOLDER_ENCRYPTED_INBOX);
        uint64_t postDestSpending =
            getDecryptedBalance(*arg.dest, HOLDER_ENCRYPTED_SPENDING);
        uint64_t postDestIssuer =
            getDecryptedBalance(*arg.dest, ISSUER_ENCRYPTED_BALANCE);

        // Public balances unchanged
        env_.require(mptbalance(*this, *arg.account, senderPubAmt));
        env_.require(mptbalance(*this, *arg.dest, destPubAmt));

        // OA and COA unchanged
        env_.require(requireAny([&]() -> bool { return prevOA == postOA; }));
        env_.require(requireAny([&]() -> bool { return prevCOA == postCOA; }));

        // Verify sender changes
        env_.require(requireAny([&]() -> bool {
            return prevSenderSpending >= *arg.amt &&
                postSenderSpending == prevSenderSpending - *arg.amt;
        }));
        env_.require(requireAny(
            [&]() -> bool { return postSenderInbox == prevSenderInbox; }));
        env_.require(requireAny([&]() -> bool {
            return prevSenderIssuer >= *arg.amt &&
                postSenderIssuer == prevSenderIssuer - *arg.amt;
        }));

        // Verify destination changes
        env_.require(requireAny([&]() -> bool {
            return postDestInbox == prevDestInbox + *arg.amt;
        }));
        env_.require(requireAny(
            [&]() -> bool { return postDestSpending == prevDestSpending; }));
        env_.require(requireAny([&]() -> bool {
            return postDestIssuer == prevDestIssuer + *arg.amt;
        }));

        // Cross checks
        env_.require(requireAny([&]() -> bool {
            return postSenderInbox + postSenderSpending == postSenderIssuer;
        }));
        env_.require(requireAny([&]() -> bool {
            return postDestInbox + postDestSpending == postDestIssuer;
        }));

        if (arg.auditorEncryptedAmt || auditor_)
        {
            uint64_t postSenderAuditor =
                getDecryptedBalance(*arg.account, AUDITOR_ENCRYPTED_BALANCE);
            uint64_t postDestAuditor =
                getDecryptedBalance(*arg.dest, AUDITOR_ENCRYPTED_BALANCE);

            env_.require(requireAny([&]() -> bool {
                return postSenderAuditor == postSenderIssuer &&
                    postDestAuditor == postDestIssuer;
            }));

            // verify sender
            env_.require(requireAny([&]() -> bool {
                return prevSenderAuditor >= *arg.amt &&
                    postSenderAuditor == prevSenderAuditor - *arg.amt;
            }));

            // verify dest
            env_.require(requireAny([&]() -> bool {
                return postDestAuditor == prevDestAuditor + *arg.amt;
            }));
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

    jv[jss::TransactionType] = jss::ConfidentialClawback;
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
        uint256 const ctxHash = getClawbackContextHash(
            account.id(), seq, *id_, *arg.amt, arg.holder->id());
        Buffer proof = getClawbackProof(
            *arg.holder, *arg.amt, getPrivKey(account), ctxHash);

        jv[sfZKProof] = strHex(proof);
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
        env_.require(requireAny([&]() -> bool {
            return prevCOA >= *arg.amt && postCOA == prevCOA - *arg.amt;
        }));
        env_.require(requireAny([&]() -> bool {
            return prevOA >= *arg.amt && postOA == prevOA - *arg.amt;
        }));

        // Verify holder's confidential balances are zeroed out
        env_.require(requireAny([&]() -> bool {
            return getDecryptedBalance(*arg.holder, HOLDER_ENCRYPTED_INBOX) ==
                0;
        }));
        env_.require(requireAny([&]() -> bool {
            return getDecryptedBalance(
                       *arg.holder, HOLDER_ENCRYPTED_SPENDING) == 0;
        }));
        env_.require(requireAny([&]() -> bool {
            return getDecryptedBalance(*arg.holder, ISSUER_ENCRYPTED_BALANCE) ==
                0;
        }));
        env_.require(requireAny([&]() -> bool {
            return getDecryptedBalance(
                       *arg.holder, AUDITOR_ENCRYPTED_BALANCE) == 0;
        }));
    }
}

void
MPTTester::generateKeyPair(Account const& account)
{
    unsigned char privKey[ecPrivKeyLength];
    secp256k1_pubkey pubKey;
    if (!secp256k1_elgamal_generate_keypair(
            secp256k1Context(), privKey, &pubKey))
        Throw<std::runtime_error>("failed to generate key pair");

    pubKeys.insert({account.id(), Buffer{pubKey.data, ecPubKeyLength}});
    privKeys.insert({account.id(), Buffer{privKey, ecPrivKeyLength}});
}

Buffer
MPTTester::getPubKey(Account const& account) const
{
    auto it = pubKeys.find(account.id());
    if (it != pubKeys.end())
    {
        return it->second;
    }

    Throw<std::runtime_error>("Account does not have public key");
}

Buffer
MPTTester::getPrivKey(Account const& account) const
{
    auto it = privKeys.find(account.id());
    if (it != privKeys.end())
    {
        return it->second;
    }

    Throw<std::runtime_error>("Account does not have private key");
}

Buffer
MPTTester::encryptAmount(
    Account const& account,
    uint64_t const amt,
    Buffer const& blindingFactor) const
{
    auto const result =
        ripple::encryptAmount(amt, getPubKey(account), blindingFactor);

    if (result)
        return *result;

    // Return a dummy buffer on failure to allow testing of
    // failures that occur prior to encryption.
    return Buffer(ecGamalEncryptedTotalLength);
}

uint64_t
MPTTester::decryptAmount(Account const& account, Buffer const& amt) const
{
    secp256k1_pubkey c1;
    secp256k1_pubkey c2;

    uint64_t decryptedAmt;

    if (!makeEcPair(amt, c1, c2))
        Throw<std::runtime_error>(
            "Failed to convert into individual EC components");

    if (!secp256k1_elgamal_decrypt(
            secp256k1Context(),
            &decryptedAmt,
            &c1,
            &c2,
            getPrivKey(account).data()))
        Throw<std::runtime_error>("Failed to decrypt amount");

    return decryptedAmt;
}

uint64_t
MPTTester::getDecryptedBalance(
    Account const& account,
    EncryptedBalanceType balanceType) const

{
    auto maybeEncrypted = getEncryptedBalance(account, balanceType);
    Account accountToDecrypt = account;

    if (balanceType == ISSUER_ENCRYPTED_BALANCE)
        accountToDecrypt = issuer_;
    else if (balanceType == AUDITOR_ENCRYPTED_BALANCE)
    {
        if (!auditor_)
            return 0;
        accountToDecrypt = *auditor_;
    }

    return maybeEncrypted ? decryptAmount(accountToDecrypt, *maybeEncrypted)
                          : 0;
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
    jv[sfTransactionType] = jss::ConfidentialMergeInbox;
    uint64_t prevInboxBalance =
        getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_INBOX);
    uint64_t prevSpendingBalance =
        getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);
    uint64_t prevIssuerBalance =
        getDecryptedBalance(*arg.account, ISSUER_ENCRYPTED_BALANCE);
    if (submit(arg, jv) == tesSUCCESS)
    {
        uint64_t postInboxBalance =
            getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_INBOX);
        uint64_t postSpendingBalance =
            getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);
        uint64_t postIssuerBalance =
            getDecryptedBalance(*arg.account, ISSUER_ENCRYPTED_BALANCE);

        env_.require(requireAny([&]() -> bool {
            return postSpendingBalance ==
                prevInboxBalance + prevSpendingBalance &&
                postInboxBalance == 0;
        }));

        env_.require(requireAny(
            [&]() -> bool { return prevIssuerBalance == postIssuerBalance; }));

        env_.require(requireAny([&]() -> bool {
            return postSpendingBalance + postInboxBalance == postIssuerBalance;
        }));
    }
}

std::int64_t
MPTTester::getIssuanceOutstandingBalance() const
{
    if (!id_)
        Throw<std::runtime_error>("Issuance ID does not exist");

    auto const sle = env_.current()->read(keylet::mptIssuance(*id_));

    if (!sle || !sle->isFieldPresent(sfOutstandingAmount))
        Throw<std::runtime_error>(
            "Issuance object does not contain outstanding amount");

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

    jv[jss::TransactionType] = jss::ConfidentialConvertBack;
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

    fillConversionCiphertexts(
        arg,
        jv,
        holderCiphertext,
        issuerCiphertext,
        auditorCiphertext,
        blindingFactor);

    jv[sfBlindingFactor] = strHex(blindingFactor);

    if (arg.proof)
        jv[sfZKProof.jsonName] = *arg.proof;
    else
    {
        auto const version = getMPTokenVersion(*arg.account);

        // if the caller generated ciphertexts themselves, they should also
        // generate the proof themselves from the blinding factor
        uint256 const ctxHash = getConvertBackContextHash(
            arg.account->id(), env_.seq(*arg.account), *id_, *arg.amt, version);
        Buffer proof = getConvertBackProof(
            *arg.account,
            *arg.amt,
            ctxHash,
            holderCiphertext,
            issuerCiphertext,
            auditorCiphertext,
            blindingFactor);
        jv[sfZKProof] = strHex(proof);
    }

    auto const holderAmt = getBalance(*arg.account);
    auto const prevConfidentialOutstanding = getIssuanceConfidentialBalance();

    uint64_t prevInboxBalance =
        getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_INBOX);
    uint64_t prevSpendingBalance =
        getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);
    uint64_t prevIssuerBalance =
        getDecryptedBalance(*arg.account, ISSUER_ENCRYPTED_BALANCE);
    [[maybe_unused]] uint64_t prevAuditorBalance =
        getDecryptedBalance(*arg.account, AUDITOR_ENCRYPTED_BALANCE);

    if (submit(arg, jv) == tesSUCCESS)
    {
        auto const postConfidentialOutstanding =
            getIssuanceConfidentialBalance();
        env_.require(mptbalance(*this, *arg.account, holderAmt + *arg.amt));
        env_.require(requireAny([&]() -> bool {
            return prevConfidentialOutstanding - *arg.amt ==
                postConfidentialOutstanding;
        }));

        uint64_t postInboxBalance =
            getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_INBOX);
        uint64_t postIssuerBalance =
            getDecryptedBalance(*arg.account, ISSUER_ENCRYPTED_BALANCE);
        uint64_t postSpendingBalance =
            getDecryptedBalance(*arg.account, HOLDER_ENCRYPTED_SPENDING);

        if (arg.auditorEncryptedAmt || auditor_)
        {
            uint64_t postAuditorBalance =
                getDecryptedBalance(*arg.account, AUDITOR_ENCRYPTED_BALANCE);
            // auditor's encrypted balance is updated correctly
            env_.require(requireAny([&]() -> bool {
                return prevAuditorBalance - *arg.amt == postAuditorBalance;
            }));
        }

        // inbox balance should not change
        env_.require(requireAny(
            [&]() -> bool { return postInboxBalance == prevInboxBalance; }));

        // issuer's encrypted balance is updated correctly
        env_.require(requireAny([&]() -> bool {
            return prevIssuerBalance - *arg.amt == postIssuerBalance;
        }));

        // holder's spending balance is updated correctly
        env_.require(requireAny([&]() -> bool {
            return prevSpendingBalance - *arg.amt == postSpendingBalance;
        }));

        // sum of holder's inbox and spending balance should equal to issuer's
        // encrypted balance
        env_.require(requireAny([&]() -> bool {
            return postInboxBalance + postSpendingBalance == postIssuerBalance;
        }));
    }
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
