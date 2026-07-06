
#include <test/jtx/mpt.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/owners.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <utility/mpt_utility.h>

#include <mpt_protocol.h>
#include <secp256k1.h>
#include <secp256k1_mpt.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace xrpl::test::jtx {
namespace {

constexpr std::uint64_t kElGamalDecryptRangeLow = 0;
constexpr std::uint64_t kElGamalDecryptRangeHigh = 3000;

/**
 * @brief Returns a reference to the value held by an optional, throwing if it
 *        is not an optional.
 *
 * @param opt The optional to unwrap.
 * @param what Description used in the thrown exception if opt is empty.
 * @return A const reference to the contained value.
 */
template <class T>
[[nodiscard]] T const&
requireValue(std::optional<T> const& opt, char const* what)
{
    if (!opt)
        Throw<std::logic_error>(what);
    return *opt;
}

/**
 * @brief Helper function to convert a PedersenProofParams into the C library struct.
 *
 * @param params The Pedersen commitment proof parameters.
 * @return The equivalent mpt_pedersen_proof_params for use with the C library.
 */
mpt_pedersen_proof_params
makePedersenParams(PedersenProofParams const& params)
{
    mpt_pedersen_proof_params res{};
    std::memcpy(
        res.pedersen_commitment, params.pedersenCommitment.data(), kMPT_PEDERSEN_COMMIT_SIZE);
    res.amount = params.amt;
    std::memcpy(res.ciphertext, params.encryptedAmt.data(), kMPT_ELGAMAL_TOTAL_SIZE);
    std::memcpy(res.blinding_factor, params.blindingFactor.data(), kMPT_BLINDING_FACTOR_SIZE);
    return res;
}

}  // namespace

struct MPTSetFlagMapping
{
    std::uint32_t setFlag;
    std::uint32_t ledgerFlag;
};

static constexpr std::array<MPTSetFlagMapping, 6> mptSetFlagMappings = {{
    {.setFlag = tmfMPTSetCanLock, .ledgerFlag = lsfMPTCanLock},
    {.setFlag = tmfMPTSetRequireAuth, .ledgerFlag = lsfMPTRequireAuth},
    {.setFlag = tmfMPTSetCanEscrow, .ledgerFlag = lsfMPTCanEscrow},
    {.setFlag = tmfMPTSetCanClawback, .ledgerFlag = lsfMPTCanClawback},
    {.setFlag = tmfMPTSetCanTrade, .ledgerFlag = lsfMPTCanTrade},
    {.setFlag = tmfMPTSetCanTransfer, .ledgerFlag = lsfMPTCanTransfer},
}};

void
MptFlags::operator()(Env& env) const
{
    env.test.expect(tester_.checkFlags(flags_, holder_));
}

void
MptBalance::operator()(Env& env) const
{
    env.test.expect(amount_ == tester_.getBalance(account_));
}

void
RequireAny::operator()(Env& env) const
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

MPTTester::MPTTester(Env& env, Account issuer, MPTInit const& arg)
    : env_(env)
    , issuer_(std::move(issuer))
    , holders_(makeHolders(arg.holders))
    , auditor_(arg.auditor)
    , close_(arg.close)
{
    if (arg.fund)
    {
        env_.fund(arg.xrp, issuer_);
        for (auto const& it : holders_)
            env_.fund(arg.xrpHolders, it.second);

        if (arg.auditor)
            env_.fund(arg.xrp, *arg.auditor);
    }
    if (close_)
        env.close();
    if (arg.fund)
    {
        env_.require(Owners(issuer_, 0));
        for (auto const& it : holders_)
        {
            if (issuer_.id() == it.second.id())
                Throw<std::runtime_error>("Issuer can't be holder");
            env_.require(Owners(it.second, 0));
        }

        if (arg.auditor)
            env_.require(Owners(*arg.auditor, 0));
    }
    if (arg.create)
        create(*arg.create);
}

MPTTester::MPTTester(
    Env& env,
    Account issuer,
    MPTID const& id,
    std::vector<Account> const& holders,
    bool close)
    : env_(env), issuer_(std::move(issuer)), holders_(makeHolders(holders)), id_(id), close_(close)
{
}

static MPTCreate
makeMPTCreate(MPTInitDef const& arg)
{
    if (arg.pay)
    {
        return {
            .maxAmt = arg.maxAmt,
            .transferFee = arg.transferFee,
            .pay = {{arg.holders, *arg.pay}},
            .flags = arg.flags,
            .mutableFlags = arg.mutableFlags,
            .authHolder = arg.authHolder};
    }
    return {
        .maxAmt = arg.maxAmt,
        .transferFee = arg.transferFee,
        .authorize = arg.holders,
        .flags = arg.flags,
        .mutableFlags = arg.mutableFlags,
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

MPTTester::
operator MPT() const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");
    return MPT("", *id_);
}

json::Value
MPTTester::createJV(MPTCreate const& arg)
{
    if (!arg.issuer)
        Throw<std::runtime_error>("MPTTester::createJV: issuer is not set");
    json::Value jv;
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
    json::Value const jv = createJV(
        {.issuer = issuer_,
         .maxAmt = arg.maxAmt,
         .assetScale = arg.assetScale,
         .transferFee = arg.transferFee,
         .metadata = arg.metadata,
         .mutableFlags = arg.mutableFlags,
         .domainID = arg.domainID});
    if (!isTesSuccess(submit(arg, jv)))
    {
        // Verify issuance doesn't exist
        env_.require(RequireAny(
            [&]() -> bool { return env_.le(keylet::mptokenIssuance(*id_)) == nullptr; }));

        id_.reset();
    }
    else
    {
        env_.require(MptFlags(*this, arg.flags.value_or(0)));
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
            {
                authAndPay(holders_, [](auto const& it) { return it.second; });
            }
            else
            {
                authAndPay(*arg.authorize, [](auto const& it) { return it; });
            }
        }
        else if (arg.pay)
        {
            if (arg.pay->first.empty())
            {
                authAndPay(holders_, [](auto const& it) { return it.second; });
            }
            else
            {
                authAndPay(arg.pay->first, [](auto const& it) { return it; });
            }
        }
    }
}

json::Value
MPTTester::destroyJV(MPTDestroy const& arg)
{
    json::Value jv;
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
    json::Value const jv =
        destroyJV({.issuer = arg.issuer ? arg.issuer : issuer_, .id = arg.id ? arg.id : id_});
    submit(arg, jv);
}

Account const&
MPTTester::holder(std::string const& holder) const
{
    auto const& it = holders_.find(holder);
    if (it == holders_.cend())
        Throw<std::runtime_error>("Holder is not found");
    return it->second;
}

json::Value
MPTTester::authorizeJV(MPTAuthorize const& arg)
{
    json::Value jv;
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
    json::Value const jv = authorizeJV({
        .account = arg.account ? arg.account : issuer_,
        .holder = arg.holder,
        .id = arg.id ? arg.id : id_,
    });
    if (auto const result = submit(arg, jv); isTesSuccess(result))
    {
        // Issuer authorizes
        if (!arg.account || *arg.account == issuer_)
        {
            auto const flags = getFlags(arg.holder);
            // issuer un-authorizes the holder
            if (arg.flags.value_or(0) == tfMPTUnauthorize)
            {
                env_.require(MptFlags(*this, flags, arg.holder));
                // issuer authorizes the holder
            }
            else
            {
                env_.require(MptFlags(*this, flags | lsfMPTAuthorized, arg.holder));
            }
        }
        // Holder authorizes
        else if (arg.flags.value_or(0) != tfMPTUnauthorize)
        {
            auto const flags = getFlags(arg.account);
            // holder creates a token
            env_.require(MptFlags(*this, flags, arg.account));
            env_.require(MptBalance(*this, *arg.account, 0));
        }
        else
        {
            // Verify that the MPToken doesn't exist.
            forObject([&](SLEP const& sle) { return env_.test.BEAST_EXPECT(!sle); }, arg.account);
        }
    }
    else if (
        arg.account && *arg.account != issuer_ && arg.flags.value_or(0) != tfMPTUnauthorize && id_)
    {
        if (result == tecDUPLICATE)
        {
            // Verify that MPToken already exists
            env_.require(RequireAny([&]() -> bool {
                return env_.le(keylet::mptoken(*id_, arg.account->id())) != nullptr;
            }));
        }
        else
        {
            // Verify MPToken doesn't exist if holder failed authorizing(unless
            // it already exists)
            env_.require(RequireAny([&]() -> bool {
                return env_.le(keylet::mptoken(*id_, arg.account->id())) == nullptr;
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

json::Value
MPTTester::setJV(MPTSet const& arg)
{
    json::Value jv;
    if (!arg.account || !arg.id)
        Throw<std::runtime_error>("MPTTester::setJV: account and/or id is not set");
    jv[sfAccount] = arg.account->human();
    jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    if (arg.holder)
    {
        std::visit(
            [&jv]<typename T>(T const& holder) {
                if constexpr (std::is_same_v<T, Account>)
                {
                    jv[sfHolder] = holder.human();
                }
                else if constexpr (std::is_same_v<T, AccountID>)
                {
                    jv[sfHolder] = toBase58(holder);
                }
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
        jv[sfIssuerEncryptionKey] = strHex(*arg.issuerPubKey);
    if (arg.auditorPubKey)
        jv[sfAuditorEncryptionKey] = strHex(*arg.auditorPubKey);
    jv[sfTransactionType] = jss::MPTokenIssuanceSet;

    return jv;
}

void
MPTTester::set(MPTSet const& arg)
{
    if (!arg.id && !id_)
        Throw<std::runtime_error>("MPT has not been created");
    json::Value const jv = setJV(
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
    if (submit(arg, jv) == tesSUCCESS && ((arg.flags.value_or(0) != 0u) || arg.mutableFlags))
    {
        if (((arg.flags.value_or(0) != 0u) || arg.mutableFlags))
        {
            auto require = [&](std::optional<Account> const& holder, bool unchanged) {
                auto flags = getFlags(holder);
                if (!unchanged)
                {
                    if (arg.flags)
                    {
                        if (*arg.flags & tfMPTLock)
                        {
                            flags |= lsfMPTLocked;
                        }
                        else if (*arg.flags & tfMPTUnlock)
                        {
                            flags &= ~lsfMPTLocked;
                        }
                    }

                    if (arg.mutableFlags)
                    {
                        for (auto const& [setFlag, ledgerFlag] : mptSetFlagMappings)
                        {
                            if ((*arg.mutableFlags & setFlag) != 0u)
                            {
                                flags |= ledgerFlag;
                            }
                        }

                        if (*arg.mutableFlags & tmfMPTSetCanHoldConfidentialBalance)
                            flags |= tfMPTCanHoldConfidentialBalance;
                    }
                }
                env_.require(MptFlags(*this, flags, holder));
            };
            if (arg.account)
                require(std::nullopt, arg.holder.has_value());
            if (auto const account = (arg.holder ? std::get_if<Account>(&(*arg.holder)) : nullptr))
                require(*account, false);

            if (arg.issuerPubKey)
            {
                env_.require(RequireAny([&]() -> bool {
                    return forObject([&](SLEP const& sle) -> bool {
                        if (sle)
                        {
                            auto const issuerPubKey = getPubKey(issuer_);
                            if (!issuerPubKey)
                            {
                                Throw<std::runtime_error>(
                                    "MPTTester::set: issuer's pubkey is not set");
                            }

                            return strHex((*sle)[sfIssuerEncryptionKey]) == strHex(*issuerPubKey);
                        }
                        return false;
                    });
                }));
            }
            if (arg.auditorPubKey)
            {
                env_.require(RequireAny([&]() -> bool {
                    return forObject([&](SLEP const& sle) -> bool {
                        if (sle)
                        {
                            if (!auditor_.has_value())
                                Throw<std::runtime_error>("MPTTester::set: auditor is not set");

                            auto const auditorPubKey = getPubKey(*auditor_);
                            if (!auditorPubKey)
                            {
                                Throw<std::runtime_error>(
                                    "MPTTester::set: auditor's pubkey is not set");
                            }

                            return strHex((*sle)[sfAuditorEncryptionKey]) == strHex(*auditorPubKey);
                        }
                        return false;
                    });
                }));
            }
        }
    }
}

bool
MPTTester::forObject(
    std::function<bool(SLEP const& sle)> const& cb,
    std::optional<Account> const& holder) const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");
    auto const key = holder ? keylet::mptoken(*id_, holder->id()) : keylet::mptokenIssuance(*id_);
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
MPTTester::checkMPTokenAmount(Account const& holder, std::int64_t expectedAmount) const
{
    return forObject(
        [&](SLEP const& sle) { return expectedAmount == (*sle)[sfMPTAmount]; }, holder);
}

[[nodiscard]] bool
MPTTester::checkMPTokenOutstandingAmount(std::int64_t expectedAmount) const
{
    return forObject(
        [&](SLEP const& sle) { return expectedAmount == (*sle)[sfOutstandingAmount]; });
}

[[nodiscard]] bool
MPTTester::checkIssuanceConfidentialBalance(std::int64_t expectedAmount) const
{
    return forObject([&](SLEP const& sle) {
        return expectedAmount == (*sle)[~sfConfidentialOutstandingAmount].value_or(0);
    });
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
    return forObject(
        [&](SLEP const& sle) -> bool { return sle->isFieldPresent(sfMPTokenMetadata); });
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
    {
        env_(
            jtx::pay(src, dest, mpt(amount)),
            Ter(err.value_or(tesSUCCESS)),
            credentials::Ids(*credentials));
    }
    else
    {
        env_(jtx::pay(src, dest, mpt(amount)), Ter(err.value_or(tesSUCCESS)));
    }

    if (!isTesSuccess(env_.ter()))
        amount = 0;
    if (close_)
        env_.close();
    if (src == issuer_)
    {
        env_.require(MptBalance(*this, src, srcAmt + amount));
        env_.require(MptBalance(*this, dest, destAmt + amount));
    }
    else if (dest == issuer_)
    {
        env_.require(MptBalance(*this, src, srcAmt - amount));
        env_.require(MptBalance(*this, dest, destAmt - amount));
    }
    else
    {
        STAmount const saAmount = {*id_, amount};
        auto const actual = multiply(saAmount, transferRate(*env_.current(), *id_)).mpt().value();
        // Sender pays the transfer fee if any
        env_.require(MptBalance(*this, src, srcAmt - actual));
        env_.require(MptBalance(*this, dest, destAmt + amount));
        // Outstanding amount is reduced by the transfer fee if any
        env_.require(MptBalance(*this, issuer_, outstandingAmt - (actual - amount)));
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
    env_(jtx::claw(issuer, mpt(amount), holder), Ter(err.value_or(tesSUCCESS)));
    if (!isTesSuccess(env_.ter()))
        amount = 0;
    if (close_)
        env_.close();

    env_.require(MptBalance(*this, issuer, issuerAmt - std::min(holderAmt, amount)));
    env_.require(MptBalance(*this, holder, holderAmt - std::min(holderAmt, amount)));
}

PrettyAmount
MPTTester::mpt(std::int64_t amount) const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");
    return xrpl::test::jtx::MPT(issuer_.name(), *id_)(amount);
}

MPTTester::
operator Asset() const
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
        if (auto const sle = env_.le(keylet::mptokenIssuance(*id_)))
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

    if (auto const sle = env_.le(keylet::mptokenIssuance(*id_)))
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
    auto const sleIssuance = env_.le(keylet::mptokenIssuance(*id_));

    if (!sleHolder || !sleIssuance)
        return std::nullopt;

    auto const ciphertextBlob = sleHolder->getFieldVL(sfIssuerEncryptedBalance);
    if (ciphertextBlob.size() != kEcGamalEncryptedTotalLength)
        return std::nullopt;

    auto const pubKeyBlob = sleIssuance->getFieldVL(sfIssuerEncryptionKey);
    if (pubKeyBlob.size() != kEcPubKeyLength)
        return std::nullopt;

    Buffer proof(kEcClawbackProofLength);

    if (mpt_get_clawback_proof(
            privateKey.data(),
            pubKeyBlob.data(),
            contextHash.data(),
            amount,
            ciphertextBlob.data(),
            proof.data()) != 0)
    {
        return std::nullopt;
    }

    return proof;
}

std::optional<Buffer>
MPTTester::getSchnorrProof(Account const& account, uint256 const& ctxHash) const
{
    auto const pubKey = getPubKey(account);
    if (!pubKey || pubKey->size() != kEcPubKeyLength)
        return std::nullopt;

    auto const privKey = getPrivKey(account);
    if (requireValue(privKey, "privKey").size() != kEcPrivKeyLength)
        return std::nullopt;

    Buffer proof(kEcSchnorrProofLength);

    if (mpt_get_convert_proof(
            requireValue(pubKey, "pubKey").data(),
            requireValue(privKey, "privKey").data(),
            ctxHash.data(),
            proof.data()) != 0)
        return std::nullopt;

    return proof;
}

std::optional<Buffer>
MPTTester::getConfidentialSendProof(
    Account const& sender,
    std::uint64_t const amount,
    std::vector<ConfidentialRecipient> const& recipients,
    Slice const& blindingFactor,
    uint256 const& contextHash,
    PedersenProofParams const& amountParams,
    PedersenProofParams const& balanceParams) const
{
    auto const pedersenBalanceParams = makePedersenParams(balanceParams);

    if (blindingFactor.size() != kEcBlindingFactorLength)
        return std::nullopt;

    auto const senderPrivKey = getPrivKey(sender);
    if (!senderPrivKey)
        return std::nullopt;

    auto const senderPubKey = getPubKey(sender);
    if (!senderPubKey || senderPubKey->size() != kEcPubKeyLength)
        return std::nullopt;

    if (amountParams.pedersenCommitment.size() != kEcPedersenCommitmentLength)
        return std::nullopt;

    // Build mpt_confidential_participant array
    std::vector<mpt_confidential_participant> participants(recipients.size());
    for (size_t i = 0; i < recipients.size(); ++i)
    {
        auto const& r = recipients[i];
        if (r.encryptedAmount.size() != kEcGamalEncryptedTotalLength ||
            r.publicKey.size() != kEcPubKeyLength)
        {
            return std::nullopt;
        }
        std::memcpy(participants[i].pubkey, r.publicKey.data(), kEcPubKeyLength);
        std::memcpy(
            participants[i].ciphertext, r.encryptedAmount.data(), kEcGamalEncryptedTotalLength);
    }

    size_t proofLen = kEcSendProofLength;
    Buffer proof(proofLen);

    if (mpt_get_confidential_send_proof(
            senderPrivKey->data(),
            senderPubKey->data(),
            amount,
            participants.data(),
            recipients.size(),
            blindingFactor.data(),
            contextHash.data(),
            amountParams.pedersenCommitment.data(),
            &pedersenBalanceParams,
            proof.data(),
            &proofLen) != 0)
        return std::nullopt;

    return proof;
}

Buffer
MPTTester::getPedersenCommitment(std::uint64_t const amount, Buffer const& pedersenBlindingFactor)
{
    // Blinding factor (rho) must be a 32-byte scalar
    if (pedersenBlindingFactor.size() != kEcBlindingFactorLength)
        Throw<std::runtime_error>("Invalid blinding factor size");

    // secp256k1_mpt_pedersen_commit doesn't handle amount 0, return a trivial
    // valid commitment for test purposes
    if (amount == 0)
    {
        Buffer buf(kEcPedersenCommitmentLength);
        std::memset(buf.data(), 0, kEcPedersenCommitmentLength);
        buf.data()[0] = kEcCompressedPrefixEvenY;
        buf.data()[kEcPedersenCommitmentLength - 1] = 0x01;
        return buf;
    }

    Buffer buf(kEcPedersenCommitmentLength);

    if (mpt_get_pedersen_commitment(amount, pedersenBlindingFactor.data(), buf.data()) != 0)
        Throw<std::runtime_error>("Pedersen commitment generation failed");

    return buf;
}

Buffer
MPTTester::getConvertBackProof(
    Account const& holder,
    std::uint64_t const amount,
    uint256 const& contextHash,
    PedersenProofParams const& pcParams) const
{
    // Expected total proof length: compact sigma proof (128 bytes) + single bulletproof (688 bytes)
    std::size_t constexpr kExpectedProofLength = kEcConvertBackProofLength;

    auto const sleMptoken = env_.le(keylet::mptoken(issuanceID(), holder.id()));
    if (!sleMptoken || !sleMptoken->isFieldPresent(sfConfidentialBalanceSpending))
        return gMakeZeroBuffer(kExpectedProofLength);

    auto const holderPubKey = getPubKey(holder);
    auto const holderPrivKey = getPrivKey(holder);

    if (!holderPubKey || !holderPrivKey)
        return gMakeZeroBuffer(kExpectedProofLength);

    auto const pedersenParams = makePedersenParams(pcParams);
    Buffer proof(kExpectedProofLength);

    if (mpt_get_convert_back_proof(
            holderPrivKey->data(),
            holderPubKey->data(),
            contextHash.data(),
            amount,
            &pedersenParams,
            proof.data()) != 0)
        return gMakeZeroBuffer(kExpectedProofLength);

    return proof;
}

std::optional<Buffer>
MPTTester::getEncryptedBalance(Account const& account, EncryptedBalanceType option) const
{
    if (!id_)
        Throw<std::runtime_error>("MPT has not been created");

    if (auto const sle = env_.le(keylet::mptoken(*id_, account.id())))
    {
        if (option == holderEncryptedInbox && sle->isFieldPresent(sfConfidentialBalanceInbox))
        {
            return Buffer(
                (*sle)[sfConfidentialBalanceInbox].data(),
                (*sle)[sfConfidentialBalanceInbox].size());
        }
        if (option == holderEncryptedSpending && sle->isFieldPresent(sfConfidentialBalanceSpending))
        {
            return Buffer(
                (*sle)[sfConfidentialBalanceSpending].data(),
                (*sle)[sfConfidentialBalanceSpending].size());
        }
        if (option == issuerEncryptedBalance && sle->isFieldPresent(sfIssuerEncryptedBalance))
        {
            return Buffer(
                (*sle)[sfIssuerEncryptedBalance].data(), (*sle)[sfIssuerEncryptedBalance].size());
        }
        if (option == auditorEncryptedBalance && sle->isFieldPresent(sfAuditorEncryptedBalance))
        {
            return Buffer(
                (*sle)[sfAuditorEncryptedBalance].data(), (*sle)[sfAuditorEncryptedBalance].size());
        }
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
    json::Value& jv,
    Buffer& holderCiphertext,
    Buffer& issuerCiphertext,
    std::optional<Buffer>& auditorCiphertext,
    Buffer& blindingFactor) const
{
    blindingFactor = arg.blindingFactor ? *arg.blindingFactor : generateBlindingFactor();

    // Handle Holder
    if (arg.holderEncryptedAmt)
    {
        holderCiphertext = *arg.holderEncryptedAmt;
    }
    else
    {
        holderCiphertext = encryptAmount(
            requireValue(arg.account, "account"), requireValue(arg.amt, "amt"), blindingFactor);
    }

    jv[sfHolderEncryptedAmount.jsonName] = strHex(holderCiphertext);

    // Handle Issuer
    if (arg.issuerEncryptedAmt)
    {
        issuerCiphertext = *arg.issuerEncryptedAmt;
    }
    else
    {
        issuerCiphertext = encryptAmount(issuer_, requireValue(arg.amt, "amt"), blindingFactor);
    }

    jv[sfIssuerEncryptedAmount.jsonName] = strHex(issuerCiphertext);

    // Handle Auditor
    if (arg.auditorEncryptedAmt)
    {
        auditorCiphertext = *arg.auditorEncryptedAmt;
    }
    else if (auditor_.has_value() && arg.fillAuditorEncryptedAmt.value_or(false))
    {
        auditorCiphertext = encryptAmount(
            requireValue(auditor_, "auditor"), requireValue(arg.amt, "amt"), blindingFactor);
    }

    // Update auditor JSON only if ciphertext exists
    if (auditorCiphertext)
        jv[sfAuditorEncryptedAmount.jsonName] = strHex(*auditorCiphertext);
}

void
MPTTester::convert(MPTConvert const& arg)
{
    json::Value jv;
    if (arg.account)
    {
        jv[sfAccount] = arg.account->human();
    }
    else
    {
        Throw<std::runtime_error>("Account not specified");
    }

    jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
    if (arg.id)
    {
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    }
    else
    {
        if (!id_)
            Throw<std::runtime_error>("MPT has not been created");
        jv[sfMPTokenIssuanceID] = to_string(*id_);
    }

    if (arg.amt)
        jv[sfMPTAmount.jsonName] = std::to_string(*arg.amt);
    if (arg.holderPubKey)
        jv[sfHolderEncryptionKey.jsonName] = strHex(*arg.holderPubKey);

    Buffer holderCiphertext;
    Buffer issuerCiphertext;
    std::optional<Buffer> auditorCiphertext;
    Buffer blindingFactor;

    fillConversionCiphertexts(
        arg, jv, holderCiphertext, issuerCiphertext, auditorCiphertext, blindingFactor);

    jv[sfBlindingFactor.jsonName] = strHex(blindingFactor);
    if (arg.proof)
    {
        jv[sfZKProof.jsonName] = *arg.proof;
    }
    else if (arg.fillSchnorrProof.value_or(arg.holderPubKey.has_value()))
    {
        // whether to automatically generate and attach a Schnorr proof:
        // if fillSchnorrProof is explicitly set, follow its value;
        // otherwise, default to generating the proof only if holder pub key is
        // present.
        auto const seq = arg.ticketSeq.value_or(env_.seq(*arg.account));
        auto const contextHash =
            getConvertContextHash(requireValue(arg.account, "account").id(), issuanceID(), seq);

        auto const proof = getSchnorrProof(*arg.account, contextHash);
        if (proof)
        {
            jv[sfZKProof.jsonName] = strHex(*proof);
        }
        else
        {
            jv[sfZKProof.jsonName] = strHex(gMakeZeroBuffer(kEcSchnorrProofLength));
        }
    }

    auto const holderAmt = getBalance(*arg.account);
    auto const prevConfidentialOutstanding = getIssuanceConfidentialBalance();

    auto const prevInboxBalance = getDecryptedBalance(*arg.account, holderEncryptedInbox);
    auto const prevSpendingBalance = getDecryptedBalance(*arg.account, holderEncryptedSpending);
    auto const prevIssuerBalance = getDecryptedBalance(*arg.account, issuerEncryptedBalance);

    if (!prevInboxBalance || !prevSpendingBalance || !prevIssuerBalance)
        Throw<std::runtime_error>("Failed to get Pre-convert balance");

    std::optional<uint64_t> prevAuditorBalance;
    if (arg.auditorEncryptedAmt || auditor_)
    {
        prevAuditorBalance = getDecryptedBalance(*arg.account, auditorEncryptedBalance);
        if (!prevAuditorBalance)
            Throw<std::runtime_error>("Failed to get Pre-convert balance");
    }

    auto const prevOutstanding = getIssuanceOutstandingBalance();

    if (submit(arg, jv) == tesSUCCESS)
    {
        auto const postConfidentialOutstanding = getIssuanceConfidentialBalance();
        auto const postOutstanding = getIssuanceOutstandingBalance();
        env_.require(MptBalance(
            *this, requireValue(arg.account, "account"), holderAmt - requireValue(arg.amt, "amt")));
        env_.require(RequireAny([&]() -> bool {
            return prevOutstanding && postOutstanding && *prevOutstanding == *postOutstanding;
        }));
        env_.require(RequireAny([&]() -> bool {
            return prevConfidentialOutstanding + *arg.amt == postConfidentialOutstanding;
        }));

        env_.require(RequireAny([&]() -> bool {
            return getEncryptedBalance(*arg.account, holderEncryptedInbox).has_value();
        }));
        env_.require(RequireAny([&]() -> bool {
            return getEncryptedBalance(*arg.account, holderEncryptedSpending).has_value();
        }));
        env_.require(RequireAny([&]() -> bool {
            return getEncryptedBalance(*arg.account, issuerEncryptedBalance).has_value();
        }));

        auto const postInboxBalance = getDecryptedBalance(*arg.account, holderEncryptedInbox);
        auto const postIssuerBalance = getDecryptedBalance(*arg.account, issuerEncryptedBalance);
        auto const postSpendingBalance = getDecryptedBalance(*arg.account, holderEncryptedSpending);

        if (!postInboxBalance || !postIssuerBalance || !postSpendingBalance)
            Throw<std::runtime_error>("Failed to get post-convert balance");

        if (arg.auditorEncryptedAmt || auditor_)
        {
            auto const postAuditorBalance =
                getDecryptedBalance(*arg.account, auditorEncryptedBalance);

            if (!postAuditorBalance)
                Throw<std::runtime_error>("Failed to get post-convert auditor balance");

            env_.require(RequireAny([&]() -> bool {
                return getEncryptedBalance(*arg.account, auditorEncryptedBalance).has_value();
            }));

            // auditor's encrypted balance is updated correctly
            env_.require(RequireAny(
                [&]() -> bool { return *prevAuditorBalance + *arg.amt == *postAuditorBalance; }));
        }
        // spending balance should not change
        env_.require(
            RequireAny([&]() -> bool { return *postSpendingBalance == *prevSpendingBalance; }));

        // issuer's encrypted balance is updated correctly
        env_.require(RequireAny(
            [&]() -> bool { return *prevIssuerBalance + *arg.amt == *postIssuerBalance; }));

        // holder's inbox balance is updated correctly
        env_.require(RequireAny(
            [&]() -> bool { return *prevInboxBalance + *arg.amt == *postInboxBalance; }));

        // sum of holder's inbox and spending balance should equal to issuer's
        // encrypted balance
        env_.require(RequireAny([&]() -> bool {
            return *postInboxBalance + *postSpendingBalance == *postIssuerBalance;
        }));

        if (arg.holderPubKey)
        {
            env_.require(RequireAny([&]() -> bool {
                return forObject(
                    [&](SLEP const& sle) -> bool {
                        if (sle)
                        {
                            auto const holderPubKey = getPubKey(*arg.account);
                            if (!holderPubKey)
                            {
                                Throw<std::runtime_error>(
                                    "MPTTester::convert: holder's pubkey is "
                                    "not set");
                            }

                            return strHex((*sle)[sfHolderEncryptionKey]) == strHex(*holderPubKey);
                        }
                        return false;
                    },
                    arg.account);
            }));
        }
    }
}

json::Value
MPTTester::convertJV(MPTConvert const& arg, std::uint32_t seq)
{
    json::Value jv;
    if (arg.account)
    {
        jv[sfAccount] = arg.account->human();
    }
    else
    {
        Throw<std::runtime_error>("Account not specified");
    }

    jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
    if (arg.id)
    {
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    }
    else
    {
        if (!id_)
            Throw<std::runtime_error>("MPT has not been created");
        jv[sfMPTokenIssuanceID] = to_string(*id_);
    }

    if (arg.amt)
        jv[sfMPTAmount.jsonName] = std::to_string(*arg.amt);
    if (arg.holderPubKey)
        jv[sfHolderEncryptionKey.jsonName] = strHex(*arg.holderPubKey);

    Buffer holderCiphertext;
    Buffer issuerCiphertext;
    std::optional<Buffer> auditorCiphertext;
    Buffer blindingFactor;

    fillConversionCiphertexts(
        arg, jv, holderCiphertext, issuerCiphertext, auditorCiphertext, blindingFactor);

    jv[sfBlindingFactor.jsonName] = strHex(blindingFactor);

    if (arg.proof)
    {
        jv[sfZKProof.jsonName] = *arg.proof;
    }
    else if (arg.fillSchnorrProof.value_or(arg.holderPubKey.has_value()))
    {
        auto const contextHash =
            getConvertContextHash(requireValue(arg.account, "account").id(), issuanceID(), seq);
        auto const proof = getSchnorrProof(*arg.account, contextHash);
        if (proof)
        {
            jv[sfZKProof.jsonName] = strHex(*proof);
        }
        else
        {
            jv[sfZKProof.jsonName] = strHex(gMakeZeroBuffer(kEcSchnorrProofLength));
        }
    }

    return jv;
}

void
MPTTester::send(MPTConfidentialSend const& arg)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::ConfidentialMPTSend;

    if (arg.account)
    {
        jv[sfAccount] = arg.account->human();
    }
    else
    {
        Throw<std::runtime_error>("Account not specified");
    }

    if (arg.dest)
    {
        jv[sfDestination] = arg.dest->human();
    }
    else
    {
        Throw<std::runtime_error>("Destination not specified");
    }

    if (!arg.amt)
        Throw<std::runtime_error>("Amount not specified for testing purposes");

    if (arg.id)
    {
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    }
    else
    {
        if (!id_)
            Throw<std::runtime_error>("MPT has not been created");
        jv[sfMPTokenIssuanceID] = to_string(*id_);
    }

    Buffer const blindingFactor =
        arg.blindingFactor ? *arg.blindingFactor : generateBlindingFactor();

    // fill in the encrypted amounts if not provided
    auto const senderAmt = arg.senderEncryptedAmt
        ? *arg.senderEncryptedAmt
        : encryptAmount(*arg.account, *arg.amt, blindingFactor);
    auto const destAmt = arg.destEncryptedAmt ? *arg.destEncryptedAmt
                                              : encryptAmount(*arg.dest, *arg.amt, blindingFactor);
    auto const issuerAmt = arg.issuerEncryptedAmt
        ? *arg.issuerEncryptedAmt
        : encryptAmount(issuer_, *arg.amt, blindingFactor);

    std::optional<Buffer> auditorAmt;
    if (arg.auditorEncryptedAmt)
    {
        auditorAmt = arg.auditorEncryptedAmt;
    }
    else if (auditor_.has_value() && arg.fillAuditorEncryptedAmt.value_or(false))
    {
        auditorAmt = encryptAmount(
            requireValue(auditor_, "auditor"), requireValue(arg.amt, "amt"), blindingFactor);
    }

    jv[sfSenderEncryptedAmount] = strHex(senderAmt);
    jv[sfDestinationEncryptedAmount] = strHex(destAmt);
    jv[sfIssuerEncryptedAmount] = strHex(issuerAmt);
    if (auditorAmt)
        jv[sfAuditorEncryptedAmount] = strHex(*auditorAmt);

    if (arg.credentials)
    {
        auto& arr(jv[sfCredentialIDs.jsonName] = json::ValueType::Array);
        for (auto const& hash : *arg.credentials)
            arr.append(hash);
    }

    // Version counters before send
    auto const prevSenderVersion = getMPTokenVersion(*arg.account);
    auto const prevDestVersion = getMPTokenVersion(*arg.dest);

    // Sender's previous confidential state
    auto const prevSenderInbox = getDecryptedBalance(*arg.account, holderEncryptedInbox);
    auto const prevSenderSpending = getDecryptedBalance(*arg.account, holderEncryptedSpending);
    auto const prevSenderIssuer = getDecryptedBalance(*arg.account, issuerEncryptedBalance);
    auto const prevSenderInboxEncrypted = getEncryptedBalance(*arg.account, holderEncryptedInbox);
    auto const prevSenderSpendingEncrypted =
        getEncryptedBalance(*arg.account, holderEncryptedSpending);
    auto const prevSenderIssuerEncrypted =
        getEncryptedBalance(*arg.account, issuerEncryptedBalance);
    if (!prevSenderInbox || !prevSenderSpending || !prevSenderIssuer)
        Throw<std::runtime_error>("Failed to get Pre-send balance");

    std::optional<uint64_t> prevSenderAuditor;
    auto const prevSenderAuditorEncrypted =
        getEncryptedBalance(*arg.account, auditorEncryptedBalance);
    if (arg.auditorEncryptedAmt || auditor_)
    {
        prevSenderAuditor = getDecryptedBalance(*arg.account, auditorEncryptedBalance);
        if (!prevSenderAuditor)
            Throw<std::runtime_error>("Failed to get Pre-send balance");
    }

    // Destination's previous confidential state
    auto const prevDestInbox = getDecryptedBalance(*arg.dest, holderEncryptedInbox);
    auto const prevDestSpending = getDecryptedBalance(*arg.dest, holderEncryptedSpending);
    auto const prevDestIssuer = getDecryptedBalance(*arg.dest, issuerEncryptedBalance);
    auto const prevDestInboxEncrypted = getEncryptedBalance(*arg.dest, holderEncryptedInbox);
    auto const prevDestSpendingEncrypted = getEncryptedBalance(*arg.dest, holderEncryptedSpending);
    auto const prevDestIssuerEncrypted = getEncryptedBalance(*arg.dest, issuerEncryptedBalance);
    if (!prevDestInbox || !prevDestSpending || !prevDestIssuer)
        Throw<std::runtime_error>("Failed to get Pre-send balance");

    std::optional<uint64_t> prevDestAuditor;
    auto const prevDestAuditorEncrypted = getEncryptedBalance(*arg.dest, auditorEncryptedBalance);
    if (arg.auditorEncryptedAmt || auditor_)
    {
        prevDestAuditor = getDecryptedBalance(*arg.dest, auditorEncryptedBalance);
        if (!prevDestAuditor)
            Throw<std::runtime_error>("Failed to get Pre-send balance");
    }

    // Fill in the commitment if not provided
    // The amount commitment must use the same blinding factor as the ElGamal
    // encryption. The sigma proof links the two, so using different randomness
    // for each would cause proof verification to fail.
    Buffer amountCommitment, balanceCommitment;
    if (arg.amountCommitment)
    {
        amountCommitment = *arg.amountCommitment;
    }
    else
    {
        amountCommitment = getPedersenCommitment(*arg.amt, blindingFactor);
    }

    jv[sfAmountCommitment] = strHex(amountCommitment);

    auto const balanceBlindingFactor = generateBlindingFactor();
    if (arg.balanceCommitment)
    {
        balanceCommitment = *arg.balanceCommitment;
    }
    else
    {
        balanceCommitment = getPedersenCommitment(*prevSenderSpending, balanceBlindingFactor);
    }

    jv[sfBalanceCommitment] = strHex(balanceCommitment);

    // Fill in the proof if not provided
    if (arg.proof)
    {
        jv[sfZKProof] = *arg.proof;
    }
    else
    {
        auto const version = getMPTokenVersion(*arg.account);
        auto const seq = arg.ticketSeq.value_or(env_.seq(*arg.account));
        auto const ctxHash = getSendContextHash(
            requireValue(arg.account, "account").id(),
            issuanceID(),
            seq,
            requireValue(arg.dest, "dest").id(),
            version);

        std::vector<ConfidentialRecipient> recipients;

        auto const senderPubKey = getPubKey(*arg.account);
        auto const destPubKey = getPubKey(*arg.dest);
        auto const issuerPubKey = getPubKey(issuer_);

        // If a key is missing, we skip adding the recipient. This intentionally
        // causes proof generation to fail, triggering the dummy proof fallback.
        if (senderPubKey)
        {
            recipients.push_back({
                .publicKey = Slice(*senderPubKey),
                .encryptedAmount = senderAmt,
            });
        }
        if (destPubKey)
        {
            recipients.push_back({
                .publicKey = Slice(*destPubKey),
                .encryptedAmount = destAmt,
            });
        }
        if (issuerPubKey)
        {
            recipients.push_back({
                .publicKey = Slice(*issuerPubKey),
                .encryptedAmount = issuerAmt,
            });
        }

        std::optional<Buffer> auditorPubKey;
        if (auditorAmt)
        {
            if (!auditor_)
                Throw<std::runtime_error>("Auditor not registered");

            auditorPubKey = getPubKey(*auditor_);
            if (auditorPubKey)
            {
                recipients.push_back({
                    .publicKey = Slice(*auditorPubKey),
                    .encryptedAmount = *auditorAmt,
                });
            }
        }

        std::optional<Buffer> proof;

        // Skip proof generation if encrypted balance is missing (e.g.,
        // feature disabled), when the sender and destination are the same
        // (malformed case causing pcm to be zero), or when spending balance
        // is 0
        if (arg.account != arg.dest && prevSenderSpendingEncrypted && *prevSenderSpending > 0)
        {
            proof = getConfidentialSendProof(
                *arg.account,
                *arg.amt,
                recipients,
                blindingFactor,
                ctxHash,
                {
                    .pedersenCommitment = amountCommitment,
                    .amt = *arg.amt,
                    .encryptedAmt = senderAmt,
                    .blindingFactor = blindingFactor,
                },
                {
                    .pedersenCommitment = balanceCommitment,
                    .amt = *prevSenderSpending,
                    .encryptedAmt = *prevSenderSpendingEncrypted,
                    .blindingFactor = balanceBlindingFactor,
                });
        }

        if (proof)
        {
            jv[sfZKProof.jsonName] = strHex(*proof);
        }
        else
        {
            jv[sfZKProof.jsonName] = strHex(gMakeZeroBuffer(kEcSendProofLength));
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
        auto const postSenderInbox = getDecryptedBalance(*arg.account, holderEncryptedInbox);
        auto const postSenderSpending = getDecryptedBalance(*arg.account, holderEncryptedSpending);
        auto const postSenderIssuer = getDecryptedBalance(*arg.account, issuerEncryptedBalance);

        if (!postSenderInbox || !postSenderSpending || !postSenderIssuer)
            Throw<std::runtime_error>("Failed to get Post-send balance");

        // Destination's post confidential state
        auto const postDestInbox = getDecryptedBalance(*arg.dest, holderEncryptedInbox);
        auto const postDestSpending = getDecryptedBalance(*arg.dest, holderEncryptedSpending);
        auto const postDestIssuer = getDecryptedBalance(*arg.dest, issuerEncryptedBalance);

        if (!postDestInbox || !postDestSpending || !postDestIssuer)
            Throw<std::runtime_error>("Failed to get Post-send balance");

        // Public balances unchanged
        env_.require(MptBalance(*this, *arg.account, senderPubAmt));
        env_.require(MptBalance(*this, *arg.dest, destPubAmt));

        // OA and COA unchanged
        env_.require(RequireAny([&]() -> bool { return prevOA && postOA && *prevOA == *postOA; }));
        env_.require(RequireAny([&]() -> bool { return prevCOA == postCOA; }));

        // Verify sender changes
        env_.require(RequireAny([&]() -> bool {
            return *prevSenderSpending >= *arg.amt &&
                *postSenderSpending == *prevSenderSpending - *arg.amt;
        }));
        env_.require(RequireAny([&]() -> bool { return postSenderInbox == prevSenderInbox; }));
        env_.require(RequireAny([&]() -> bool {
            return *prevSenderIssuer >= *arg.amt &&
                *postSenderIssuer == *prevSenderIssuer - *arg.amt;
        }));

        // Verify destination changes
        env_.require(
            RequireAny([&]() -> bool { return *postDestInbox == *prevDestInbox + *arg.amt; }));
        env_.require(RequireAny([&]() -> bool { return *postDestSpending == *prevDestSpending; }));
        env_.require(
            RequireAny([&]() -> bool { return *postDestIssuer == *prevDestIssuer + *arg.amt; }));

        // Cross checks
        env_.require(RequireAny(
            [&]() -> bool { return *postSenderInbox + *postSenderSpending == *postSenderIssuer; }));
        env_.require(RequireAny(
            [&]() -> bool { return *postDestInbox + *postDestSpending == *postDestIssuer; }));

        // Version: sender increments by 1; receiver version is unchanged by incoming sends
        env_.require(RequireAny(
            [&]() -> bool { return getMPTokenVersion(*arg.account) == prevSenderVersion + 1; }));
        env_.require(
            RequireAny([&]() -> bool { return getMPTokenVersion(*arg.dest) == prevDestVersion; }));

        if (arg.auditorEncryptedAmt || auditor_)
        {
            auto const postSenderAuditor =
                getDecryptedBalance(*arg.account, auditorEncryptedBalance);
            auto const postDestAuditor = getDecryptedBalance(*arg.dest, auditorEncryptedBalance);
            if (!postSenderAuditor || !postDestAuditor)
                Throw<std::runtime_error>("Failed to get Post-send balance");

            env_.require(RequireAny([&]() -> bool {
                return *postSenderAuditor == *postSenderIssuer &&
                    *postDestAuditor == *postDestIssuer;
            }));

            // verify sender
            env_.require(RequireAny([&]() -> bool {
                return prevSenderAuditor >= *arg.amt &&
                    *postSenderAuditor == *prevSenderAuditor - *arg.amt;
            }));

            // verify dest
            env_.require(RequireAny(
                [&]() -> bool { return *postDestAuditor == *prevDestAuditor + *arg.amt; }));
        }
    }
}

json::Value
MPTTester::sendJV(
    MPTConfidentialSend const& arg,
    std::uint32_t seq,
    std::optional<ConfidentialSendChainState> chain)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::ConfidentialMPTSend;

    if (arg.account)
    {
        jv[sfAccount] = arg.account->human();
    }
    else
    {
        Throw<std::runtime_error>("Account not specified");
    }

    if (arg.dest)
    {
        jv[sfDestination] = arg.dest->human();
    }
    else
    {
        Throw<std::runtime_error>("Destination not specified");
    }

    if (!arg.amt)
        Throw<std::runtime_error>("Amount not specified for testing purposes");

    if (arg.id)
    {
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    }
    else
    {
        if (!id_)
            Throw<std::runtime_error>("MPT has not been created");
        jv[sfMPTokenIssuanceID] = to_string(*id_);
    }

    Buffer const blindingFactor =
        arg.blindingFactor ? *arg.blindingFactor : generateBlindingFactor();

    auto const senderAmt = arg.senderEncryptedAmt
        ? *arg.senderEncryptedAmt
        : encryptAmount(*arg.account, *arg.amt, blindingFactor);
    auto const destAmt = arg.destEncryptedAmt ? *arg.destEncryptedAmt
                                              : encryptAmount(*arg.dest, *arg.amt, blindingFactor);
    auto const issuerAmt = arg.issuerEncryptedAmt
        ? *arg.issuerEncryptedAmt
        : encryptAmount(issuer_, *arg.amt, blindingFactor);

    std::optional<Buffer> auditorAmt;
    if (arg.auditorEncryptedAmt)
    {
        auditorAmt = arg.auditorEncryptedAmt;
    }
    else if (auditor_.has_value() && arg.fillAuditorEncryptedAmt.value_or(false))
    {
        auditorAmt = encryptAmount(
            requireValue(auditor_, "auditor"), requireValue(arg.amt, "amt"), blindingFactor);
    }

    jv[sfSenderEncryptedAmount] = strHex(senderAmt);
    jv[sfDestinationEncryptedAmount] = strHex(destAmt);
    jv[sfIssuerEncryptedAmount] = strHex(issuerAmt);
    if (auditorAmt)
        jv[sfAuditorEncryptedAmount] = strHex(*auditorAmt);

    if (arg.credentials)
    {
        auto& arr(jv[sfCredentialIDs.jsonName] = json::ValueType::Array);
        for (auto const& hash : *arg.credentials)
            arr.append(hash);
    }

    std::uint64_t prevSenderSpending = 0;
    std::optional<Buffer> prevEncryptedSenderSpending;
    std::uint32_t version = 0;
    if (chain)
    {
        prevSenderSpending = chain->spending;
        prevEncryptedSenderSpending = chain->encSpending;
        version = chain->version;
    }
    else
    {
        auto const ledgerSpending = getDecryptedBalance(*arg.account, holderEncryptedSpending);
        if (!ledgerSpending)
            Throw<std::runtime_error>("Failed to get sender spending balance");
        prevSenderSpending = *ledgerSpending;
        prevEncryptedSenderSpending = getEncryptedBalance(*arg.account, holderEncryptedSpending);
        version = getMPTokenVersion(*arg.account);
    }

    // The amount commitment must use the same blinding factor as the tx ElGamal
    // encryption blinding factor.
    Buffer amountCommitment, balanceCommitment;
    if (arg.amountCommitment)
    {
        amountCommitment = *arg.amountCommitment;
    }
    else
    {
        amountCommitment = getPedersenCommitment(*arg.amt, blindingFactor);
    }

    jv[sfAmountCommitment] = strHex(amountCommitment);

    auto const balanceBlindingFactor = generateBlindingFactor();
    if (arg.balanceCommitment)
    {
        balanceCommitment = *arg.balanceCommitment;
    }
    else
    {
        balanceCommitment = getPedersenCommitment(prevSenderSpending, balanceBlindingFactor);
    }

    jv[sfBalanceCommitment] = strHex(balanceCommitment);

    if (arg.proof)
    {
        jv[sfZKProof.jsonName] = *arg.proof;
    }
    else
    {
        auto const ctxHash = getSendContextHash(
            requireValue(arg.account, "account").id(),
            issuanceID(),
            seq,
            requireValue(arg.dest, "dest").id(),
            version);

        std::vector<ConfidentialRecipient> recipients;

        auto const senderPubKey = getPubKey(*arg.account);
        auto const destPubKey = getPubKey(*arg.dest);
        auto const issuerPubKey = getPubKey(issuer_);

        if (senderPubKey)
        {
            recipients.push_back({
                .publicKey = Slice(*senderPubKey),
                .encryptedAmount = senderAmt,
            });
        }
        if (destPubKey)
        {
            recipients.push_back({
                .publicKey = Slice(*destPubKey),
                .encryptedAmount = destAmt,
            });
        }
        if (issuerPubKey)
        {
            recipients.push_back({
                .publicKey = Slice(*issuerPubKey),
                .encryptedAmount = issuerAmt,
            });
        }

        std::optional<Buffer> auditorPubKey;
        if (auditorAmt)
        {
            if (!auditor_)
                Throw<std::runtime_error>("Auditor not registered");
            auditorPubKey = getPubKey(*auditor_);
            if (auditorPubKey)
            {
                recipients.push_back({
                    .publicKey = Slice(*auditorPubKey),
                    .encryptedAmount = *auditorAmt,
                });
            }
        }

        std::optional<Buffer> proof;

        // Skip proof generation when spending balance is 0
        if (arg.account != arg.dest && prevEncryptedSenderSpending && prevSenderSpending > 0)
        {
            proof = getConfidentialSendProof(
                *arg.account,
                *arg.amt,
                recipients,
                blindingFactor,
                ctxHash,
                {
                    .pedersenCommitment = amountCommitment,
                    .amt = *arg.amt,
                    .encryptedAmt = senderAmt,
                    .blindingFactor = blindingFactor,
                },
                {
                    .pedersenCommitment = balanceCommitment,
                    .amt = prevSenderSpending,
                    .encryptedAmt = *prevEncryptedSenderSpending,
                    .blindingFactor = balanceBlindingFactor,
                });
        }

        if (proof)
        {
            jv[sfZKProof.jsonName] = strHex(*proof);
        }
        else
        {
            jv[sfZKProof.jsonName] = strHex(gMakeZeroBuffer(kEcSendProofLength));
        }
    }

    return jv;
}

static Buffer
parseSenderEncAmt(json::Value const& jv)
{
    auto const hexStr = jv[sfSenderEncryptedAmount.jsonName].asString();
    auto const bytes = strUnHex(hexStr);
    if (!bytes)
        Throw<std::runtime_error>("chainAfterSend: invalid hex in sfSenderEncryptedAmount");
    return Buffer(bytes->data(), bytes->size());
}

ConfidentialSendChainState
MPTTester::chainAfterSend(Account const& sender, std::uint64_t sendAmt, json::Value const& jv) const
{
    auto const prevSpending = getDecryptedBalance(sender, holderEncryptedSpending);
    auto const prevEncSpending = getEncryptedBalance(sender, holderEncryptedSpending);
    auto const prevVersion = getMPTokenVersion(sender);

    if (!prevSpending || !prevEncSpending)
        Throw<std::runtime_error>("chainAfterSend: failed to read sender state from ledger");

    Buffer const senderEncAmt = parseSenderEncAmt(jv);
    auto chain = computeNextSendChainState(
        *prevSpending, Slice(*prevEncSpending), prevVersion, sendAmt, Slice(senderEncAmt));
    if (!chain)
        Throw<std::runtime_error>("chainAfterSend: computeNextSendChainState failed");
    return std::move(*chain);
}

std::optional<ConfidentialSendChainState>
computeNextSendChainState(
    std::uint64_t currentSpending,
    Slice const& currentEncSpending,
    std::uint32_t currentVersion,
    std::uint64_t sendAmt,
    Slice const& senderEncAmt)
{
    if (sendAmt > currentSpending)
        return std::nullopt;  // LCOV_EXCL_LINE

    auto newEncSpending = homomorphicSubtract(currentEncSpending, senderEncAmt);
    if (!newEncSpending)
        return std::nullopt;  // LCOV_EXCL_LINE

    return ConfidentialSendChainState{
        .spending = currentSpending - sendAmt,
        .encSpending = std::move(*newEncSpending),
        .version = currentVersion + 1,
    };
}

void
MPTTester::confidentialClaw(MPTConfidentialClawback const& arg)
{
    json::Value jv;
    auto const account = arg.account ? *arg.account : issuer_;
    jv[sfAccount] = account.human();

    if (arg.holder)
    {
        jv[sfHolder] = arg.holder->human();
    }
    else
    {
        Throw<std::runtime_error>("Holder not specified");
    }

    jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
    if (arg.id)
    {
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    }
    else if (id_)
    {
        jv[sfMPTokenIssuanceID] = to_string(*id_);
    }
    else
    {
        Throw<std::runtime_error>("MPT has not been created");
    }

    if (arg.amt)
        jv[sfMPTAmount] = std::to_string(*arg.amt);

    if (arg.proof)
    {
        jv[sfZKProof] = *arg.proof;
    }
    else
    {
        auto const seq = arg.ticketSeq ? *arg.ticketSeq : env_.seq(account);
        auto const contextHash = getClawbackContextHash(
            account.id(), issuanceID(), seq, requireValue(arg.holder, "holder").id());

        auto const privKey = getPrivKey(account);
        if (!privKey || privKey->size() != kEcPrivKeyLength)
            Throw<std::runtime_error>("Failed to get clawback private key");

        auto const proof = getClawbackProof(
            requireValue(arg.holder, "holder"),
            requireValue(arg.amt, "amt"),
            requireValue(privKey, "privKey"),
            contextHash);

        if (proof)
        {
            jv[sfZKProof] = strHex(*proof);
        }
        else
        {
            jv[sfZKProof] = strHex(gMakeZeroBuffer(kEcClawbackProofLength));
        }
    }

    auto const holderPubAmt = getBalance(*arg.holder);
    auto const prevCOA = getIssuanceConfidentialBalance();
    auto const prevOA = getIssuanceOutstandingBalance();
    auto const prevVersion = getMPTokenVersion(*arg.holder);

    if (submit(arg, jv) == tesSUCCESS)
    {
        auto const postCOA = getIssuanceConfidentialBalance();
        auto const postOA = getIssuanceOutstandingBalance();
        auto const postVersion = getMPTokenVersion(*arg.holder);

        // Verify holder's public balance is unchanged
        env_.require(MptBalance(*this, *arg.holder, holderPubAmt));

        // Verify COA and OA are reduced correctly
        env_.require(RequireAny(
            [&]() -> bool { return prevCOA >= *arg.amt && postCOA == prevCOA - *arg.amt; }));
        env_.require(RequireAny([&]() -> bool {
            return prevOA && postOA && *prevOA >= *arg.amt && *postOA == *prevOA - *arg.amt;
        }));

        // Verify holder's confidential balances are zeroed out
        env_.require(RequireAny(
            [&]() -> bool { return getDecryptedBalance(*arg.holder, holderEncryptedInbox) == 0; }));
        env_.require(RequireAny([&]() -> bool {
            return getDecryptedBalance(*arg.holder, holderEncryptedSpending) == 0;
        }));
        env_.require(RequireAny([&]() -> bool {
            return getDecryptedBalance(*arg.holder, issuerEncryptedBalance) == 0;
        }));
        env_.require(RequireAny([&]() -> bool {
            return getDecryptedBalance(*arg.holder, auditorEncryptedBalance) == 0;
        }));

        // Verify version is incremented
        env_.require(RequireAny([&]() -> bool { return postVersion == prevVersion + 1; }));
    }
}

void
MPTTester::generateKeyPair(Account const& account)
{
    unsigned char privKey[kEcPrivKeyLength];
    secp256k1_pubkey pubKey;
    if (secp256k1_elgamal_generate_keypair(secp256k1Context(), privKey, &pubKey) == 0)
        Throw<std::runtime_error>("failed to generate key pair");

    // Serialize public key to compressed format (33 bytes)
    unsigned char compressedPubKey[kEcPubKeyLength];
    size_t outLen = kEcPubKeyLength;
    if (secp256k1_ec_pubkey_serialize(
            secp256k1Context(), compressedPubKey, &outLen, &pubKey, SECP256K1_EC_COMPRESSED) != 1 ||
        outLen != kEcPubKeyLength)
    {
        Throw<std::runtime_error>("failed to serialize public key");
    }

    pubKeys_.insert({account.id(), Buffer{compressedPubKey, kEcPubKeyLength}});
    privKeys_.insert({account.id(), Buffer{privKey, kEcPrivKeyLength}});
}

std::optional<Buffer>
MPTTester::getPubKey(Account const& account) const
{
    if (auto const it = pubKeys_.find(account.id()); it != pubKeys_.end())
        return it->second;

    return std::nullopt;
}

std::optional<Buffer>
MPTTester::getPrivKey(Account const& account) const
{
    if (auto const it = privKeys_.find(account.id()); it != privKeys_.end())
        return it->second;

    return std::nullopt;
}

Buffer
MPTTester::encryptAmount(Account const& account, uint64_t const amt, Buffer const& blindingFactor)
    const
{
    if (auto const pubKey = getPubKey(account))
    {
        if (auto const result = xrpl::encryptAmount(amt, *pubKey, blindingFactor))
            return *result;
    }

    // Return a dummy buffer on failure to allow testing of
    // failures that occur prior to encryption.
    return gMakeZeroBuffer(kEcGamalEncryptedTotalLength);
}

std::optional<uint64_t>
MPTTester::decryptAmount(Account const& account, Buffer const& amt) const
{
    if (amt.size() != kEcGamalEncryptedTotalLength)
        return std::nullopt;

    auto const pair = makeEcPair(amt);
    if (!pair)
        return std::nullopt;

    auto const privKey = getPrivKey(account);
    if (!privKey || privKey->size() != kEcPrivKeyLength)
        return std::nullopt;

    uint64_t decryptedAmt = 0;
    if (secp256k1_elgamal_decrypt(
            secp256k1Context(),
            &decryptedAmt,
            &pair->c1,
            &pair->c2,
            privKey->data(),
            kElGamalDecryptRangeLow,
            kElGamalDecryptRangeHigh) == 0)
    {
        return std::nullopt;
    }

    return decryptedAmt;
}

std::optional<uint64_t>
MPTTester::getDecryptedBalance(Account const& account, EncryptedBalanceType balanceType) const
{
    auto const encryptedAmt = getEncryptedBalance(account, balanceType);

    // Return zero to test cases like Feature Disabled, where the ledger object
    // does not exist.
    if (!encryptedAmt)
        return 0;

    Account decryptor = account;

    if (balanceType == issuerEncryptedBalance)
    {
        decryptor = issuer_;
    }
    else if (balanceType == auditorEncryptedBalance)
    {
        if (!auditor_)
            return std::nullopt;
        decryptor = *auditor_;
    }

    return decryptAmount(decryptor, *encryptedAmt);
};

json::Value
MPTTester::mergeInboxJV(MPTMergeInbox const& arg) const
{
    json::Value jv;
    if (arg.account)
    {
        jv[sfAccount] = arg.account->human();
    }
    else
    {
        Throw<std::runtime_error>("Account not specified");
    }
    if (arg.id)
    {
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    }
    else
    {
        if (!id_)
            Throw<std::runtime_error>("MPT has not been created");
        jv[sfMPTokenIssuanceID] = to_string(*id_);
    }
    jv[sfTransactionType] = jss::ConfidentialMPTMergeInbox;
    return jv;
}

void
MPTTester::mergeInbox(MPTMergeInbox const& arg)
{
    json::Value jv;
    if (arg.account)
    {
        jv[sfAccount] = arg.account->human();
    }
    else
    {
        Throw<std::runtime_error>("Account not specified");
    }
    if (arg.id)
    {
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    }
    else
    {
        if (!id_)
            Throw<std::runtime_error>("MPT has not been created");
        jv[sfMPTokenIssuanceID] = to_string(*id_);
    }

    jv[sfTransactionType] = jss::ConfidentialMPTMergeInbox;
    auto const holderPubAmt = getBalance(*arg.account);
    auto const prevCOA = getIssuanceConfidentialBalance();
    auto const prevOA = getIssuanceOutstandingBalance();
    auto const prevInboxBalance = getDecryptedBalance(*arg.account, holderEncryptedInbox);
    auto const prevSpendingBalance = getDecryptedBalance(*arg.account, holderEncryptedSpending);
    auto const prevIssuerBalance = getDecryptedBalance(*arg.account, issuerEncryptedBalance);
    auto const prevIssuerEncrypted = getEncryptedBalance(*arg.account, issuerEncryptedBalance);
    auto const prevAuditorEncrypted = getEncryptedBalance(*arg.account, auditorEncryptedBalance);
    auto const prevVersion = getMPTokenVersion(*arg.account);

    if (!prevInboxBalance || !prevSpendingBalance || !prevIssuerBalance)
        Throw<std::runtime_error>("Failed to get pre-mergeInbox balances");

    if (submit(arg, jv) == tesSUCCESS)
    {
        auto const postCOA = getIssuanceConfidentialBalance();
        auto const postOA = getIssuanceOutstandingBalance();
        auto const postInboxBalance = getDecryptedBalance(*arg.account, holderEncryptedInbox);
        auto const postSpendingBalance = getDecryptedBalance(*arg.account, holderEncryptedSpending);
        auto const postIssuerBalance = getDecryptedBalance(*arg.account, issuerEncryptedBalance);
        auto const postInboxEncrypted = getEncryptedBalance(*arg.account, holderEncryptedInbox);
        auto const postIssuerEncrypted = getEncryptedBalance(*arg.account, issuerEncryptedBalance);
        auto const postAuditorEncrypted =
            getEncryptedBalance(*arg.account, auditorEncryptedBalance);
        auto const postVersion = getMPTokenVersion(*arg.account);

        if (!postInboxBalance || !postSpendingBalance || !postIssuerBalance ||
            !prevIssuerEncrypted || !postInboxEncrypted || !postIssuerEncrypted)
            Throw<std::runtime_error>("Failed to get post-mergeInbox balances");

        env_.require(MptBalance(*this, *arg.account, holderPubAmt));
        env_.require(RequireAny([&]() -> bool { return prevOA && postOA && *prevOA == *postOA; }));
        env_.require(RequireAny([&]() -> bool { return prevCOA == postCOA; }));

        env_.require(RequireAny([&]() -> bool {
            return *postSpendingBalance == *prevInboxBalance + *prevSpendingBalance &&
                *postInboxBalance == 0;
        }));

        env_.require(
            RequireAny([&]() -> bool { return *prevIssuerBalance == *postIssuerBalance; }));

        auto const holderPubKey = getPubKey(*arg.account);
        if (!holderPubKey)
            Throw<std::runtime_error>("Failed to get holder public key");

        auto const expectedInbox = encryptCanonicalZeroAmount(
            requireValue(holderPubKey, "holderPubKey"),
            requireValue(arg.account, "account").id(),
            issuanceID());
        if (!expectedInbox)
            Throw<std::runtime_error>("Failed to get canonical zero encryption");

        env_.require(RequireAny([&]() -> bool { return *postInboxEncrypted == *expectedInbox; }));
        env_.require(
            RequireAny([&]() -> bool { return *postIssuerEncrypted == *prevIssuerEncrypted; }));
        env_.require(RequireAny([&]() -> bool {
            return postAuditorEncrypted.has_value() == prevAuditorEncrypted.has_value() &&
                (!postAuditorEncrypted || *postAuditorEncrypted == *prevAuditorEncrypted);
        }));
        env_.require(RequireAny([&]() -> bool { return postVersion == prevVersion + 1; }));

        env_.require(RequireAny([&]() -> bool {
            return *postSpendingBalance + *postInboxBalance == *postIssuerBalance;
        }));
    }
}

std::optional<std::int64_t>
MPTTester::getIssuanceOutstandingBalance() const
{
    if (!id_)
        return std::nullopt;

    auto const sle = env_.current()->read(keylet::mptokenIssuance(*id_));

    if (!sle)
        return std::nullopt;

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
    json::Value jv;
    if (arg.account)
    {
        jv[sfAccount] = arg.account->human();
    }
    else
    {
        Throw<std::runtime_error>("Account not specified");
    }

    jv[jss::TransactionType] = jss::ConfidentialMPTConvertBack;
    if (arg.id)
    {
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    }
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
        arg, jv, holderCiphertext, issuerCiphertext, auditorCiphertext, blindingFactor);

    jv[sfBlindingFactor] = strHex(blindingFactor);

    auto const prevInboxBalance = getDecryptedBalance(*arg.account, holderEncryptedInbox);
    auto const prevSpendingBalance = getDecryptedBalance(*arg.account, holderEncryptedSpending);
    auto const prevIssuerBalance = getDecryptedBalance(*arg.account, issuerEncryptedBalance);

    if (!prevInboxBalance || !prevSpendingBalance || !prevIssuerBalance)
        Throw<std::runtime_error>("Failed to get Pre-convertBack balance");

    Buffer pedersenCommitment;
    Buffer const pcBlindingFactor = generateBlindingFactor();
    if (arg.pedersenCommitment)
    {
        pedersenCommitment = *arg.pedersenCommitment;
    }
    else
    {
        pedersenCommitment = getPedersenCommitment(*prevSpendingBalance, pcBlindingFactor);
    }

    jv[sfBalanceCommitment] = strHex(pedersenCommitment);

    if (arg.proof)
    {
        jv[sfZKProof.jsonName] = strHex(*arg.proof);
    }
    else
    {
        auto const version = getMPTokenVersion(*arg.account);

        // if the caller generated ciphertexts themselves, they should also
        // generate the proof themselves from the blinding factor
        auto const seq = arg.ticketSeq.value_or(env_.seq(*arg.account));
        auto const contextHash = getConvertBackContextHash(
            requireValue(arg.account, "account").id(), issuanceID(), seq, version);
        auto const prevEncryptedSpendingBalance =
            getEncryptedBalance(*arg.account, holderEncryptedSpending);

        Buffer proof;
        // generate a dummy proof if no encrypted amount field, so that other
        // preflight/preclaim are checked
        if (!prevEncryptedSpendingBalance)
        {
            proof = gMakeZeroBuffer(kEcConvertBackProofLength);
        }
        else
        {
            proof = getConvertBackProof(
                *arg.account,
                requireValue(arg.amt, "amt"),
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
        prevAuditorBalance = getDecryptedBalance(*arg.account, auditorEncryptedBalance);
        if (!prevAuditorBalance)
            Throw<std::runtime_error>("Failed to get Pre-convertBack balance");
    }

    auto const prevOutstanding = getIssuanceOutstandingBalance();
    auto const prevVersion = getMPTokenVersion(*arg.account);

    if (submit(arg, jv) == tesSUCCESS)
    {
        auto const postConfidentialOutstanding = getIssuanceConfidentialBalance();
        auto const postOutstanding = getIssuanceOutstandingBalance();
        auto const postVersion = getMPTokenVersion(*arg.account);
        env_.require(MptBalance(
            *this, requireValue(arg.account, "account"), holderAmt + requireValue(arg.amt, "amt")));
        env_.require(RequireAny([&]() -> bool {
            return prevOutstanding && postOutstanding && *prevOutstanding == *postOutstanding;
        }));
        env_.require(RequireAny([&]() -> bool {
            return prevConfidentialOutstanding - *arg.amt == postConfidentialOutstanding;
        }));

        auto const postInboxBalance = getDecryptedBalance(*arg.account, holderEncryptedInbox);
        auto const postIssuerBalance = getDecryptedBalance(*arg.account, issuerEncryptedBalance);
        auto const postSpendingBalance = getDecryptedBalance(*arg.account, holderEncryptedSpending);

        if (!postInboxBalance || !postIssuerBalance || !postSpendingBalance)
            Throw<std::runtime_error>("Failed to get post-convertBack balance");

        if (arg.auditorEncryptedAmt || auditor_)
        {
            auto const postAuditorBalance =
                getDecryptedBalance(*arg.account, auditorEncryptedBalance);

            if (!postAuditorBalance)
                Throw<std::runtime_error>("Failed to get post-convertBack balance");

            // auditor's encrypted balance is updated correctly
            env_.require(RequireAny(
                [&]() -> bool { return *prevAuditorBalance - *arg.amt == *postAuditorBalance; }));
        }

        // inbox balance should not change
        env_.require(RequireAny([&]() -> bool { return *postInboxBalance == *prevInboxBalance; }));

        // issuer's encrypted balance is updated correctly
        env_.require(RequireAny(
            [&]() -> bool { return *prevIssuerBalance - *arg.amt == *postIssuerBalance; }));

        // holder's spending balance is updated correctly
        env_.require(RequireAny(
            [&]() -> bool { return *prevSpendingBalance - *arg.amt == *postSpendingBalance; }));

        // holder's confidential balance version is updated correctly
        env_.require(RequireAny([&]() -> bool { return postVersion == prevVersion + 1; }));

        // sum of holder's inbox and spending balance should equal to issuer's
        // encrypted balance
        env_.require(RequireAny([&]() -> bool {
            return *postInboxBalance + *postSpendingBalance == *postIssuerBalance;
        }));
    }
}

json::Value
MPTTester::convertBackJV(MPTConvertBack const& arg, std::uint32_t seq)
{
    json::Value jv;
    if (arg.account)
    {
        jv[sfAccount] = arg.account->human();
    }
    else
    {
        Throw<std::runtime_error>("Account not specified");
    }

    jv[jss::TransactionType] = jss::ConfidentialMPTConvertBack;
    if (arg.id)
    {
        jv[sfMPTokenIssuanceID] = to_string(*arg.id);
    }
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
        arg, jv, holderCiphertext, issuerCiphertext, auditorCiphertext, blindingFactor);

    jv[sfBlindingFactor] = strHex(blindingFactor);

    auto const prevSpendingBalance = getDecryptedBalance(*arg.account, holderEncryptedSpending);
    if (!prevSpendingBalance)
        Throw<std::runtime_error>("convertBackJV: failed to read spending balance from ledger");

    Buffer pedersenCommitment;
    Buffer const pcBlindingFactor = generateBlindingFactor();
    if (arg.pedersenCommitment)
    {
        pedersenCommitment = *arg.pedersenCommitment;
    }
    else
    {
        pedersenCommitment = getPedersenCommitment(*prevSpendingBalance, pcBlindingFactor);
    }

    jv[sfBalanceCommitment] = strHex(pedersenCommitment);

    if (arg.proof)
    {
        jv[sfZKProof.jsonName] = strHex(*arg.proof);
    }
    else
    {
        auto const version = getMPTokenVersion(*arg.account);
        auto const prevEncSpending = getEncryptedBalance(*arg.account, holderEncryptedSpending);
        auto const contextHash = getConvertBackContextHash(
            requireValue(arg.account, "account").id(), issuanceID(), seq, version);

        Buffer proof;
        if (!prevEncSpending)
        {
            proof = gMakeZeroBuffer(kEcConvertBackProofLength);
        }
        else
        {
            proof = getConvertBackProof(
                *arg.account,
                requireValue(arg.amt, "amt"),
                contextHash,
                {
                    .pedersenCommitment = pedersenCommitment,
                    .amt = *prevSpendingBalance,
                    .encryptedAmt = *prevEncSpending,
                    .blindingFactor = pcBlindingFactor,
                });
        }

        jv[sfZKProof] = strHex(proof);
    }

    return jv;
}

}  // namespace xrpl::test::jtx
