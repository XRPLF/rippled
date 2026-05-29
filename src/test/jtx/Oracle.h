#pragma once

#include <test/jtx.h>

#include <date/date.h>

namespace xrpl::test::jtx::oracle {

using AnyValue = std::variant<std::string, double, json::Int, json::UInt>;
using OraclesData = std::vector<std::pair<std::optional<Account>, std::optional<AnyValue>>>;

// Special string value, which is converted to unquoted string in the string
// passed to rpc.
constexpr char const* kNoneTag = "%None%";
constexpr char const* kUnquotedNone = "None";
constexpr char const* kNonePattern = "\"%None%\"";

std::uint32_t
asUInt(AnyValue const& v);

bool
validDocumentID(AnyValue const& v);

void
toJson(json::Value& jv, AnyValue const& v);

void
toJsonHex(json::Value& jv, AnyValue const& v);

// base asset, quote asset, price, scale
using DataSeries = std::vector<
    std::
        tuple<std::string, std::string, std::optional<std::uint32_t>, std::optional<std::uint8_t>>>;

// Typical defaults for Create
struct CreateArg
{
    std::optional<AccountID> owner = std::nullopt;
    std::optional<AnyValue> documentID = 1;
    DataSeries series = {{"XRP", "USD", 740, 1}};
    std::optional<AnyValue> assetClass = "currency";
    std::optional<AnyValue> provider = "provider";
    std::optional<AnyValue> uri = "URI";
    std::optional<AnyValue> lastUpdateTime = std::nullopt;
    std::uint32_t flags = 0;
    std::optional<jtx::Msig> msig = std::nullopt;
    std::optional<jtx::Seq> seq = std::nullopt;
    int fee = 10;
    std::optional<Ter> err = std::nullopt;
    bool close = false;
};

// Typical defaults for Update
struct UpdateArg
{
    std::optional<AccountID> owner = std::nullopt;
    std::optional<AnyValue> documentID = std::nullopt;
    DataSeries series = {};  // NOLINT(readability-redundant-member-init)
    std::optional<AnyValue> assetClass = std::nullopt;
    std::optional<AnyValue> provider = std::nullopt;
    std::optional<AnyValue> uri = "URI";
    std::optional<AnyValue> lastUpdateTime = std::nullopt;
    std::uint32_t flags = 0;
    std::optional<jtx::Msig> msig = std::nullopt;
    std::optional<jtx::Seq> seq = std::nullopt;
    int fee = 10;
    std::optional<Ter> err = std::nullopt;
};

struct RemoveArg
{
    std::optional<AccountID> const& owner = std::nullopt;
    std::optional<AnyValue> const& documentID = std::nullopt;
    std::uint32_t flags = 0;
    std::optional<jtx::Msig> const& msig = std::nullopt;
    std::optional<jtx::Seq> seq = std::nullopt;
    int fee = 10;
    std::optional<Ter> const& err = std::nullopt;
};

// Simulate testStartTime as 10'000s from XRPL epoch time to make
// LastUpdateTime validation to work and to make unit-test consistent.
// The value doesn't matter much, it has to be greater
// than maxLastUpdateTimeDelta in order to pass LastUpdateTime
// validation {close-maxLastUpdateTimeDelta,close+maxLastUpdateTimeDelta}.
static constexpr std::chrono::seconds kTestStartTime = kEpochOffset + std::chrono::seconds(10'000);

/** Oracle class facilitates unit-testing of the Price Oracle feature.
 * It defines functions to create, update, and delete the Oracle object,
 * to query for various states, and to call APIs.
 */
class Oracle
{
private:
    // Global fee if not 0
    static inline std::uint32_t fee = 0;
    Env& env_;
    AccountID owner_;
    std::uint32_t documentID_{};

private:
    void
    submit(
        json::Value const& jv,
        std::optional<jtx::Msig> const& msig,
        std::optional<jtx::Seq> const& seq,
        std::optional<Ter> const& err);

public:
    Oracle(Env& env, CreateArg const& arg, bool submit = true);

    void
    remove(RemoveArg const& arg);

    void
    set(CreateArg const& arg);
    void
    set(UpdateArg const& arg);

    static json::Value
    aggregatePrice(
        Env& env,
        std::optional<AnyValue> const& baseAsset,
        std::optional<AnyValue> const& quoteAsset,
        std::optional<OraclesData> const& oracles = std::nullopt,
        std::optional<AnyValue> const& trim = std::nullopt,
        std::optional<AnyValue> const& timeThreshold = std::nullopt);

    [[nodiscard]] std::uint32_t
    documentID() const
    {
        return documentID_;
    }

    [[nodiscard]] bool
    exists() const
    {
        return exists(env_, owner_, documentID_);
    }

    [[nodiscard]] static bool
    exists(Env& env, AccountID const& account, std::uint32_t documentID);

    [[nodiscard]] bool
    expectPrice(DataSeries const& prices) const;

    [[nodiscard]] bool
    expectLastUpdateTime(std::uint32_t lastUpdateTime) const;

    static json::Value
    ledgerEntry(
        Env& env,
        std::optional<std::variant<AccountID, std::string>> const& account,
        std::optional<AnyValue> const& documentID,
        std::optional<std::string> const& index = std::nullopt);

    [[nodiscard]] json::Value
    ledgerEntry(std::optional<std::string> const& index = std::nullopt) const
    {
        return Oracle::ledgerEntry(env_, owner_, documentID_, index);
    }

    static void
    setFee(std::uint32_t f)
    {
        fee = f;
    }

    friend std::ostream&
    operator<<(std::ostream& strm, Oracle const& oracle)
    {
        strm << oracle.ledgerEntry().toStyledString();
        return strm;
    }
};

}  // namespace xrpl::test::jtx::oracle
