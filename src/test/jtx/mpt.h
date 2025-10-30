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

#ifndef RIPPLE_TEST_JTX_MPT_H_INCLUDED
#define RIPPLE_TEST_JTX_MPT_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>

#include <xrpl/protocol/UintTypes.h>

#include <cstdint>

namespace ripple {
namespace test {
namespace jtx {

class MPTTester;

// Generates a syntactically valid placeholder ciphertext
ripple::Buffer
generatePlaceholderCiphertext();

// Check flags settings on MPT create
class mptflags
{
private:
    MPTTester& tester_;
    std::uint32_t flags_;
    std::optional<Account> holder_;

public:
    mptflags(
        MPTTester& tester,
        std::uint32_t flags,
        std::optional<Account> const& holder = std::nullopt)
        : tester_(tester), flags_(flags), holder_(holder)
    {
    }

    void
    operator()(Env& env) const;
};

// Check mptissuance or mptoken amount balances on payment
class mptbalance
{
private:
    MPTTester const& tester_;
    Account const& account_;
    std::int64_t const amount_;

public:
    mptbalance(MPTTester& tester, Account const& account, std::int64_t amount)
        : tester_(tester), account_(account), amount_(amount)
    {
    }

    void
    operator()(Env& env) const;
};

class requireAny
{
private:
    std::function<bool()> cb_;

public:
    requireAny(std::function<bool()> const& cb) : cb_(cb)
    {
    }

    void
    operator()(Env& env) const;
};

struct MPTInit
{
    std::vector<Account> holders = {};
    PrettyAmount const xrp = XRP(10'000);
    PrettyAmount const xrpHolders = XRP(10'000);
    bool fund = true;
    bool close = true;
};
static MPTInit const mptInitNoFund{.fund = false};

struct MPTCreate
{
    std::optional<std::uint64_t> maxAmt = std::nullopt;
    std::optional<std::uint8_t> assetScale = std::nullopt;
    std::optional<std::uint16_t> transferFee = std::nullopt;
    std::optional<std::string> metadata = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    bool fund = true;
    std::optional<std::uint32_t> flags = {0};
    std::optional<std::uint32_t> mutableFlags = std::nullopt;
    std::optional<uint256> domainID = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

struct MPTDestroy
{
    std::optional<Account> issuer = std::nullopt;
    std::optional<MPTID> id = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

struct MPTAuthorize
{
    std::optional<Account> account = std::nullopt;
    std::optional<Account> holder = std::nullopt;
    std::optional<MPTID> id = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

struct MPTSet
{
    std::optional<Account> account = std::nullopt;
    std::optional<Account> holder = std::nullopt;
    std::optional<MPTID> id = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<std::uint32_t> mutableFlags = std::nullopt;
    std::optional<std::uint16_t> transferFee = std::nullopt;
    std::optional<std::string> metadata = std::nullopt;
    std::optional<Account> delegate = std::nullopt;
    std::optional<uint256> domainID = std::nullopt;
    std::optional<Buffer> pubKey = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

struct MPTConvert
{
    std::optional<Account> account = std::nullopt;
    std::optional<MPTID> id = std::nullopt;
    std::optional<std::uint64_t> amt = std::nullopt;
    std::optional<std::string> proof = std::nullopt;
    std::optional<Buffer> holderPubKey = std::nullopt;
    std::optional<Buffer> holderEncryptedAmt = std::nullopt;
    std::optional<Buffer> issuerEncryptedAmt = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

struct MPTMergeInbox
{
    std::optional<Account> account = std::nullopt;
    std::optional<MPTID> id = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

struct MPTConfidentialSend
{
    std::optional<Account> account = std::nullopt;
    std::optional<Account> dest = std::nullopt;
    std::optional<MPTID> id = std::nullopt;
    // amt is to generate encrypted amounts for testing purposes
    std::optional<std::uint64_t> amt = std::nullopt;
    std::optional<std::string> proof = std::nullopt;
    std::optional<Buffer> senderEncryptedAmt = std::nullopt;
    std::optional<Buffer> destEncryptedAmt = std::nullopt;
    std::optional<Buffer> issuerEncryptedAmt = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<TER> err = std::nullopt;
};
struct MPTConvertBack
{
    std::optional<Account> account = std::nullopt;
    std::optional<MPTID> id = std::nullopt;
    std::optional<std::uint64_t> amt = std::nullopt;
    std::optional<std::string> proof = std::nullopt;
    std::optional<Buffer> holderEncryptedAmt = std::nullopt;
    std::optional<Buffer> issuerEncryptedAmt = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

class MPTTester
{
    Env& env_;
    Account const& issuer_;
    std::unordered_map<std::string, Account> const holders_;
    std::optional<MPTID> id_;
    bool close_;
    std::unordered_map<AccountID, Buffer> pubKeys;
    std::unordered_map<AccountID, Buffer> privKeys;

public:
    enum EncryptedBalanceType {
        ISSUER_ENCRYPTED_BALANCE,
        HOLDER_ENCRYPTED_INBOX,
        HOLDER_ENCRYPTED_SPENDING,
    };

    MPTTester(Env& env, Account const& issuer, MPTInit const& constr = {});

    void
    create(MPTCreate const& arg = MPTCreate{});

    void
    destroy(MPTDestroy const& arg = MPTDestroy{});

    void
    authorize(MPTAuthorize const& arg = MPTAuthorize{});

    void
    set(MPTSet const& set = {});

    void
    convert(MPTConvert const& arg = MPTConvert{});

    void
    mergeInbox(MPTMergeInbox const& arg = MPTMergeInbox{});

    void
    send(MPTConfidentialSend const& arg = MPTConfidentialSend{});

    void
    convertBack(MPTConvertBack const& arg = MPTConvertBack{});

    [[nodiscard]] bool
    checkDomainID(std::optional<uint256> expected) const;

    [[nodiscard]] bool
    checkMPTokenAmount(Account const& holder, std::int64_t expectedAmount)
        const;

    [[nodiscard]] bool
    checkMPTokenOutstandingAmount(std::int64_t expectedAmount) const;

    [[nodiscard]] bool
    checkIssuanceConfidentialBalance(std::int64_t expectedAmount) const;

    [[nodiscard]] bool
    checkFlags(
        uint32_t const expectedFlags,
        std::optional<Account> const& holder = std::nullopt) const;

    [[nodiscard]] bool
    checkMetadata(std::string const& metadata) const;

    [[nodiscard]] bool
    isMetadataPresent() const;

    [[nodiscard]] bool
    checkTransferFee(std::uint16_t transferFee) const;

    [[nodiscard]] bool
    isTransferFeePresent() const;

    Account const&
    issuer() const
    {
        return issuer_;
    }
    Account const&
    holder(std::string const& h) const;

    void
    pay(Account const& src,
        Account const& dest,
        std::int64_t amount,
        std::optional<TER> err = std::nullopt,
        std::optional<std::vector<std::string>> credentials = std::nullopt);

    void
    claw(
        Account const& issuer,
        Account const& holder,
        std::int64_t amount,
        std::optional<TER> err = std::nullopt);

    PrettyAmount
    mpt(std::int64_t amount) const;

    MPTID const&
    issuanceID() const
    {
        if (!env_.test.BEAST_EXPECT(id_))
            Throw<std::logic_error>("Uninitialized issuanceID");
        return *id_;
    }

    std::int64_t
    getBalance(Account const& account) const;

    std::int64_t
    getIssuanceConfidentialBalance() const;

    std::optional<Buffer>
    getEncryptedBalance(
        Account const& account,
        EncryptedBalanceType option = HOLDER_ENCRYPTED_INBOX) const;

    MPT
    operator[](std::string const& name);

    bool
    printMPT(Account const& holder_) const;

    void
    generateKeyPair(Account const& account);

    Buffer
    getPubKey(Account const& account) const;

    Buffer
    getPrivKey(Account const& account) const;

    Buffer
    encryptAmount(Account const& account, uint64_t amt) const;

    uint64_t
    decryptAmount(Account const& account, Buffer const& amt) const;

    uint64_t
    getDecryptedBalance(
        Account const& account,
        EncryptedBalanceType balanceType) const;

    std::int64_t
    getIssuanceOutstandingBalance() const;

private:
    using SLEP = std::shared_ptr<SLE const>;
    bool
    forObject(
        std::function<bool(SLEP const& sle)> const& cb,
        std::optional<Account> const& holder = std::nullopt) const;

    template <typename A>
    TER
    submit(A const& arg, Json::Value const& jv)
    {
        env_(
            jv,
            txflags(arg.flags.value_or(0)),
            ter(arg.err.value_or(tesSUCCESS)));
        auto const err = env_.ter();
        if (close_)
            env_.close();
        if (arg.ownerCount)
            env_.require(owners(issuer_, *arg.ownerCount));
        if (arg.holderCount)
        {
            for (auto it : holders_)
                env_.require(owners(it.second, *arg.holderCount));
        }
        return err;
    }

    static std::unordered_map<std::string, Account>
    makeHolders(std::vector<Account> const& holders);

    std::uint32_t
    getFlags(std::optional<Account> const& holder) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
