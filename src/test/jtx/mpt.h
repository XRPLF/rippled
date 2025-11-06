#ifndef XRPL_TEST_JTX_MPT_H_INCLUDED
#define XRPL_TEST_JTX_MPT_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>

#include <xrpl/protocol/UintTypes.h>

namespace ripple {
namespace test {
namespace jtx {

class MPTTester;

auto const MPTDEXFlags = tfMPTCanTrade | tfMPTCanTransfer;

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

using Holders = std::vector<Account>;

struct MPTCreate
{
    static inline std::vector<Account> AllHolders = {};
    std::optional<Account> issuer = std::nullopt;
    std::optional<std::uint64_t> maxAmt = std::nullopt;
    std::optional<std::uint8_t> assetScale = std::nullopt;
    std::optional<std::uint16_t> transferFee = std::nullopt;
    std::optional<std::string> metadata = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    // authorize if seated.
    // if empty vector then authorize all holders
    std::optional<std::vector<Account>> authorize = std::nullopt;
    // pay if seated. if authorize is not seated then authorize.
    // if empty vector then pay to either authorize or all holders.
    std::optional<std::pair<std::vector<Account>, std::uint64_t>> pay =
        std::nullopt;
    std::optional<std::uint32_t> flags = {0};
    std::optional<std::uint32_t> mutableFlags = std::nullopt;
    bool authHolder = false;
    std::optional<uint256> domainID = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

struct MPTInit
{
    Holders holders = {};
    PrettyAmount const xrp = XRP(10'000);
    PrettyAmount const xrpHolders = XRP(10'000);
    bool fund = true;
    bool close = true;
    // create MPTIssuanceID if seated and follow rules for MPTCreate args
    std::optional<MPTCreate> create = std::nullopt;
};
static MPTInit const mptInitNoFund{.fund = false};

struct MPTInitDef
{
    Env& env;
    Account issuer;
    Holders holders = {};
    std::uint16_t transferFee = 0;
    std::optional<std::uint64_t> pay = std::nullopt;
    std::uint32_t flags = MPTDEXFlags;
    bool authHolder = false;
    bool fund = false;
    bool close = true;
    std::optional<std::uint64_t> maxAmt = std::nullopt;
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
    std::optional<std::variant<Account, AccountID>> holder = std::nullopt;
    std::optional<MPTID> id = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<std::uint32_t> mutableFlags = std::nullopt;
    std::optional<std::uint16_t> transferFee = std::nullopt;
    std::optional<std::string> metadata = std::nullopt;
    std::optional<Account> delegate = std::nullopt;
    std::optional<uint256> domainID = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

class MPTTester
{
    Env& env_;
    Account const issuer_;
    std::unordered_map<std::string, Account> const holders_;
    std::optional<MPTID> id_;
    bool close_;

public:
    MPTTester(Env& env, Account const& issuer, MPTInit const& constr = {});
    MPTTester(MPTInitDef const& constr);
    MPTTester(
        Env& env,
        Account const& issuer,
        MPTID const& id,
        std::vector<Account> const& holders = {},
        bool close = true);
    operator MPT() const;

    void
    create(MPTCreate const& arg = MPTCreate{});

    static Json::Value
    createjv(MPTCreate const& arg = MPTCreate{});

    void
    destroy(MPTDestroy const& arg = MPTDestroy{});

    static Json::Value
    destroyjv(MPTDestroy const& arg = MPTDestroy{});

    void
    authorize(MPTAuthorize const& arg = MPTAuthorize{});

    static Json::Value
    authorizejv(MPTAuthorize const& arg = MPTAuthorize{});

    void
    authorizeHolders(Holders const& holders);

    void
    set(MPTSet const& set = {});

    static Json::Value
    setjv(MPTSet const& set = {});

    [[nodiscard]] bool
    checkDomainID(std::optional<uint256> expected) const;

    [[nodiscard]] bool
    checkMPTokenAmount(Account const& holder, std::int64_t expectedAmount)
        const;

    [[nodiscard]] bool
    checkMPTokenOutstandingAmount(std::int64_t expectedAmount) const;

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

    MPT
    operator[](std::string const& name) const;

    PrettyAmount
    operator()(std::uint64_t amount) const;

    operator Asset() const;

private:
    using SLEP = SLE::const_pointer;
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
