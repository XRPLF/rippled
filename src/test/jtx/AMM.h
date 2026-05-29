#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/multisign.h>
#include <test/jtx/seq.h>
#include <test/jtx/ter.h>

#include <xrpld/rpc/GRPCHandlers.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>

#include <nudb/detail/stream.hpp>

namespace xrpl::test::jtx {

class LPToken
{
    Number const tokens_;
    Asset asset_;

public:
    LPToken(std::uint64_t tokens) : tokens_(tokens), asset_(xrpIssue())
    {
    }
    LPToken(IOUAmount tokens) : tokens_(tokens), asset_(xrpIssue())
    {
    }
    LPToken(STAmount tokens) : tokens_(tokens), asset_(tokens.asset())
    {
    }
    [[nodiscard]] STAmount
    tokens() const
    {
        return STAmount{asset_, tokens_};
    }
    [[nodiscard]] STAmount
    tokens(Issue const& ammIssue) const
    {
        return STAmount{ammIssue, tokens_};
    }
};

struct CreateArg
{
    bool log = false;
    std::uint16_t tfee = 0;
    std::uint32_t fee = 0;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<jtx::Seq> seq = std::nullopt;
    std::optional<jtx::Msig> ms = std::nullopt;
    std::optional<Ter> err = std::nullopt;
    bool close = true;
};

struct DepositArg
{
    std::optional<Account> account = std::nullopt;
    std::optional<LPToken> tokens = std::nullopt;
    std::optional<STAmount> asset1In = std::nullopt;
    std::optional<STAmount> asset2In = std::nullopt;
    std::optional<STAmount> maxEP = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<std::pair<Asset, Asset>> assets = std::nullopt;
    std::optional<jtx::Seq> seq = std::nullopt;
    std::optional<std::uint16_t> tfee = std::nullopt;
    std::optional<Ter> err = std::nullopt;
};

struct WithdrawArg
{
    std::optional<Account> account = std::nullopt;
    std::optional<LPToken> tokens = std::nullopt;
    std::optional<STAmount> asset1Out = std::nullopt;
    std::optional<STAmount> asset2Out = std::nullopt;
    std::optional<LPToken> maxEP = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<std::pair<Asset, Asset>> assets = std::nullopt;
    std::optional<jtx::Seq> seq = std::nullopt;
    std::optional<Ter> err = std::nullopt;
};

struct VoteArg
{
    std::optional<Account> account = std::nullopt;
    std::uint32_t tfee = 0;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<jtx::Seq> seq = std::nullopt;
    std::optional<std::pair<Asset, Asset>> assets = std::nullopt;
    std::optional<Ter> err = std::nullopt;
};

struct BidArg
{
    std::optional<Account> account = std::nullopt;
    std::optional<std::variant<int, IOUAmount, STAmount>> bidMin = std::nullopt;
    std::optional<std::variant<int, IOUAmount, STAmount>> bidMax = std::nullopt;
    std::vector<Account> authAccounts = {};  // NOLINT(readability-redundant-member-init)
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<std::pair<Asset, Asset>> assets = std::nullopt;
};

struct ClawbackArg
{
    Account issuer;
    Account holder;
    std::optional<std::pair<Asset, Asset>> assets = std::nullopt;
    std::optional<STAmount> amount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<Ter> err = std::nullopt;
};

/** Convenience class to test AMM functionality.
 */
class AMM
{
    Env& env_;
    Account const creatorAccount_;
    STAmount const asset1_;
    STAmount const asset2_;
    uint256 const ammID_;
    bool log_;
    bool doClose_;
    // Predict next purchase price
    IOUAmount lastPurchasePrice_;
    std::optional<IOUAmount> bidMin_;
    std::optional<IOUAmount> bidMax_;
    // Multi-signature
    std::optional<Msig> const msig_;
    // Transaction fee
    std::uint32_t const fee_;
    AccountID const ammAccount_;
    Issue const lptIssue_;
    IOUAmount const initialLPTokens_;

public:
    AMM(Env& env,
        Account account,
        STAmount asset1,
        STAmount asset2,
        bool log = false,
        std::uint16_t tfee = 0,
        std::uint32_t fee = 0,
        std::optional<std::uint32_t> flags = std::nullopt,
        std::optional<jtx::Seq> seq = std::nullopt,
        std::optional<jtx::Msig> ms = std::nullopt,
        std::optional<Ter> const& ter = std::nullopt,
        bool close = true);
    AMM(Env& env,
        Account const& account,
        STAmount const& asset1,
        STAmount const& asset2,
        Ter const& ter,
        bool log = false,
        bool close = true);
    AMM(Env& env,
        Account const& account,
        STAmount const& asset1,
        STAmount const& asset2,
        CreateArg const& arg);

    static json::Value
    createJv(
        AccountID const& account,
        STAmount const& asset1,
        STAmount const& asset2,
        std::uint16_t const& tfee);

    /** Send amm_info RPC command
     */
    [[nodiscard]] json::Value
    ammRpcInfo(
        std::optional<AccountID> const& account = std::nullopt,
        std::optional<std::string> const& ledgerIndex = std::nullopt,
        std::optional<Asset> asset1 = std::nullopt,
        std::optional<Asset> asset2 = std::nullopt,
        std::optional<AccountID> const& ammAccount = std::nullopt,
        bool ignoreParams = false,
        unsigned apiVersion = RPC::kApiInvalidVersion) const;

    /** Verify the AMM balances.
     */
    [[nodiscard]] bool
    expectBalances(
        STAmount const& asset1,
        STAmount const& asset2,
        IOUAmount const& lpt,
        std::optional<AccountID> const& account = std::nullopt) const;

    /** Get AMM balances for the token pair.
     */
    [[nodiscard]] std::tuple<STAmount, STAmount, STAmount>
    balances(
        Asset const& asset1,
        Asset const& asset2,
        std::optional<AccountID> const& account = std::nullopt) const;

    [[nodiscard]] std::tuple<STAmount, STAmount, STAmount>
    balances(std::optional<AccountID> const& account = std::nullopt) const
    {
        return balances(asset1_.asset(), asset2_.asset(), account);
    }

    [[nodiscard]] bool
    expectLPTokens(AccountID const& account, IOUAmount const& tokens) const;

    /**
     * @param fee expected discounted fee
     * @param timeSlot expected time slot
     * @param expectedPrice expected slot price
     */
    [[nodiscard]] bool
    expectAuctionSlot(
        std::uint32_t fee,
        std::optional<std::uint8_t> timeSlot,
        IOUAmount expectedPrice) const;

    [[nodiscard]] bool
    expectAuctionSlot(std::vector<AccountID> const& authAccount) const;

    [[nodiscard]] bool
    expectTradingFee(std::uint16_t fee) const;

    [[nodiscard]] bool
    expectAmmRpcInfo(
        STAmount const& asset1,
        STAmount const& asset2,
        IOUAmount const& balance,
        std::optional<AccountID> const& account = std::nullopt,
        std::optional<std::string> const& ledgerIndex = std::nullopt,
        std::optional<AccountID> const& ammAccount = std::nullopt) const;

    [[nodiscard]] bool
    ammExists() const;

    static json::Value
    depositJv(DepositArg const& arg);

    IOUAmount
    deposit(
        std::optional<Account> const& account,
        LPToken tokens,
        std::optional<STAmount> const& asset1InDetails = std::nullopt,
        std::optional<std::uint32_t> const& flags = std::nullopt,
        std::optional<Ter> const& ter = std::nullopt);

    IOUAmount
    deposit(
        std::optional<Account> const& account,
        STAmount const& asset1InDetails,
        std::optional<STAmount> const& asset2InAmount = std::nullopt,
        std::optional<STAmount> const& maxEP = std::nullopt,
        std::optional<std::uint32_t> const& flags = std::nullopt,
        std::optional<Ter> const& ter = std::nullopt);

    IOUAmount
    deposit(
        std::optional<Account> const& account,
        std::optional<LPToken> tokens,
        std::optional<STAmount> const& asset1In,
        std::optional<STAmount> const& asset2In,
        std::optional<STAmount> const& maxEP,
        std::optional<std::uint32_t> const& flags,
        std::optional<std::pair<Asset, Asset>> const& assets,
        std::optional<jtx::Seq> const& seq,
        std::optional<std::uint16_t> const& tfee = std::nullopt,
        std::optional<Ter> const& ter = std::nullopt);

    IOUAmount
    deposit(DepositArg const& arg);

    static json::Value
    withdrawJv(WithdrawArg const& arg);

    IOUAmount
    withdraw(
        std::optional<Account> const& account,
        std::optional<LPToken> const& tokens,
        std::optional<STAmount> const& asset1OutDetails = std::nullopt,
        std::optional<std::uint32_t> const& flags = std::nullopt,
        std::optional<Ter> const& ter = std::nullopt);

    IOUAmount
    withdrawAll(
        std::optional<Account> const& account,
        std::optional<STAmount> const& asset1OutDetails = std::nullopt,
        std::optional<Ter> const& ter = std::nullopt)
    {
        return withdraw(
            account,
            std::nullopt,
            asset1OutDetails,
            asset1OutDetails ? tfOneAssetWithdrawAll : tfWithdrawAll,
            ter);
    }

    IOUAmount
    withdraw(
        std::optional<Account> const& account,
        STAmount const& asset1Out,
        std::optional<STAmount> const& asset2Out = std::nullopt,
        std::optional<LPToken> const& maxEP = std::nullopt,
        std::optional<Ter> const& ter = std::nullopt);

    IOUAmount
    withdraw(
        std::optional<Account> const& account,
        std::optional<LPToken> const& tokens,
        std::optional<STAmount> const& asset1Out,
        std::optional<STAmount> const& asset2Out,
        std::optional<LPToken> const& maxEP,
        std::optional<std::uint32_t> const& flags,
        std::optional<std::pair<Asset, Asset>> const& assets,
        std::optional<jtx::Seq> const& seq,
        std::optional<Ter> const& ter = std::nullopt);

    IOUAmount
    withdraw(WithdrawArg const& arg);

    static json::Value
    voteJv(VoteArg const& arg);

    void
    vote(
        std::optional<Account> const& account,
        std::uint32_t feeVal,
        std::optional<std::uint32_t> const& flags = std::nullopt,
        std::optional<jtx::Seq> const& seq = std::nullopt,
        std::optional<std::pair<Asset, Asset>> const& assets = std::nullopt,
        std::optional<Ter> const& ter = std::nullopt);

    void
    vote(VoteArg const& arg);

    json::Value
    bid(BidArg const& arg);

    void
    clawback(ClawbackArg const& arg);

    [[nodiscard]] AccountID const&
    ammAccount() const
    {
        return ammAccount_;
    }

    [[nodiscard]] Issue
    lptIssue() const
    {
        return lptIssue_;
    }

    [[nodiscard]] IOUAmount
    tokens() const
    {
        return initialLPTokens_;
    }

    [[nodiscard]] IOUAmount
    getLPTokensBalance(std::optional<AccountID> const& account = std::nullopt) const;

    friend std::ostream&
    operator<<(std::ostream& s, AMM const& amm)
    {
        if (auto const res = amm.ammRpcInfo())
            s << res.toStyledString();
        return s;
    }

    std::string
    operator[](AccountID const& lp) const
    {
        return ammRpcInfo(lp).toStyledString();
    }

    json::Value
    operator()(AccountID const& lp) const
    {
        return ammRpcInfo(lp);
    }

    static json::Value
    deleteJv(AccountID const& account, Asset const& asset1, Asset const& assets);

    void
    ammDelete(AccountID const& account, std::optional<Ter> const& ter = std::nullopt);

    void
    setClose(bool close)
    {
        doClose_ = close;
    }

    [[nodiscard]] uint256
    ammID() const
    {
        return ammID_;
    }

    void
    setTokens(json::Value& jv, std::optional<std::pair<Asset, Asset>> const& assets = std::nullopt);

    Asset const&
    operator[](std::uint8_t i)
    {
        if (i > 1)
            Throw<std::runtime_error>("AMM: operator[], invalid index");
        return i == 0 ? asset1_.asset() : asset2_.asset();
    }

    struct Pool
    {
        AMM const& amm;
        std::vector<json::StaticString> names;
        Pool(AMM const& a, std::vector<json::StaticString> const& n = {}) : amm(a), names(n)
        {
        }
        friend std::ostream&
        operator<<(std::ostream& s, Pool const& p)
        {
            auto const& jr = p.amm.ammRpcInfo();
            auto out = [&](json::Value const& jv) {
                if (jv.isMember(jss::value))
                {
                    std::cout << jv[jss::value].asString();
                }
                else
                {
                    std::cout << jv.asString();
                }
                std::cout << " ";
            };
            if (p.names.empty())
            {
                out(jr[jss::amm][jss::amount]);
                out(jr[jss::amm][jss::amount2]);
                out(jr[jss::amm][jss::lp_token]);
            }
            else
            {
                for (auto const& n : p.names)
                    out(jr[jss::amm][n]);
            }
            std::cout << std::endl;
            return s;
        }
    };
    struct Offers
    {
        json::Value const& jv;
        Offers(json::Value const& j) : jv(j)
        {
        }
        friend std::ostream&
        operator<<(std::ostream& s, Offers const& offers)
        {
            auto out = [&](json::Value const& jv) {
                if (jv.isMember(jss::value))
                {
                    s << jv[jss::value].asString();
                }
                else
                {
                    s << jv;
                }
            };
            for (auto const& o : offers.jv[jss::offers])
            {
                s << "taker_pays: ";
                out(o[jss::taker_pays]);
                s << " taker_gets: ";
                out(o[jss::taker_gets]);
                s << std::endl;
            }
            return s;
        }
    };

private:
    AccountID
    create(
        std::uint32_t tfee = 0,
        std::optional<std::uint32_t> const& flags = std::nullopt,
        std::optional<jtx::Seq> const& seq = std::nullopt,
        std::optional<Ter> const& ter = std::nullopt);

    void
    log(bool log)
    {
        log_ = log;
    }

    [[nodiscard]] bool
    expectAmmInfo(
        STAmount const& asset1,
        STAmount const& asset2,
        IOUAmount const& balance,
        json::Value const& jv) const;

    void
    submit(
        json::Value const& jv,
        std::optional<jtx::Seq> const& seq,
        std::optional<Ter> const& ter);

    [[nodiscard]] bool
    expectAuctionSlot(auto&& cb) const;

    IOUAmount
    initialTokens();
};

namespace amm {

json::Value
ammClawback(
    Account const& issuer,
    Account const& holder,
    Asset const& asset,
    Asset const& asset2,
    std::optional<STAmount> const& amount);
}  // namespace amm

}  // namespace xrpl::test::jtx
