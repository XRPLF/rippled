#include <xrpl/protocol/ErrorCodes.h>

#include <gtest/gtest.h>

#include <set>
#include <type_traits>

using namespace xrpl;

// A row cannot be written without a status, so no row can default to 200 again.
static_assert(!std::is_constructible_v<rpc::ErrorInfo, ErrorCodeI, char const*, char const*>);

namespace {

/**
 * The numbers between `RpcBadSyntax` and `RpcLast` that have no row in the
 * error table: those no enumerator uses, and `RpcReportingUnsupported` (91),
 * the one enumerator the table does not list.
 */
std::set<int> const kGaps{5,  8,  20, 24, 25, 26, 27, 28, 34, 38, 39, 54, 55, 56,
                          59, 60, 61, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91};

}  // namespace

TEST(ErrorCodes, table_gaps_report_the_unknown_placeholder)
{
    // Codes absent from the table report the placeholder, so a scan by code must skip them.
    auto const& gap = rpc::getErrorInfo(static_cast<ErrorCodeI>(5));
    EXPECT_EQ(gap.code, RpcUnknown);
    EXPECT_STREQ(gap.token.cStr(), "unknown");

    auto const& real = rpc::getErrorInfo(RpcInvalidParams);
    EXPECT_EQ(real.code, RpcInvalidParams);
    EXPECT_STREQ(real.token.cStr(), "invalidParams");
}

TEST(ErrorCodes, exactly_the_listed_codes_have_no_row)
{
    // The gap set is written out by hand, so an enumerator added without a row fails here, where a
    // test asking the table which codes it knows would take the missing row for a gap.
    for (int code = RpcBadSyntax; code <= RpcLast; ++code)
    {
        bool const isGap = rpc::getErrorInfo(static_cast<ErrorCodeI>(code)).code == RpcUnknown;
        EXPECT_EQ(isGap, kGaps.contains(code)) << "code " << code;
    }
}

TEST(ErrorCodes, every_code_names_an_http_status)
{
    // Written out here rather than read from the row: a test reading the row production reads
    // cannot tell whether the row is right.
    EXPECT_EQ(rpc::errorCodeHttpStatus(RpcActMalformed), 400);
    EXPECT_EQ(rpc::errorCodeHttpStatus(RpcActNotFound), 404);
    EXPECT_EQ(rpc::errorCodeHttpStatus(RpcAlreadyMultisig), 400);
    EXPECT_EQ(rpc::errorCodeHttpStatus(RpcAlreadySingleSig), 400);

    // Every row names a status of its own. A code with no row reports the placeholder's, which the
    // default constructor fixes at 200.
    for (int code = RpcBadSyntax; code <= RpcLast; ++code)
    {
        auto const status = rpc::errorCodeHttpStatus(static_cast<ErrorCodeI>(code));
        if (kGaps.contains(code))
        {
            EXPECT_EQ(status, 200) << "code " << code;
        }
        else
        {
            EXPECT_NE(status, 200) << "code " << code;
        }
    }
}

TEST(ErrorCodes, code_for_token_resolves_known_tokens)
{
    EXPECT_EQ(rpc::codeForToken("invalidParams"), RpcInvalidParams);
    EXPECT_EQ(rpc::codeForToken("malformedSeq"), RpcMalformedSeq);

    // The first and last entries, so a probe that wraps or stops short is caught. The static
    // assert is what keeps the second one the last entry as codes are appended.
    EXPECT_EQ(rpc::codeForToken("badSyntax"), RpcBadSyntax);
    EXPECT_EQ(rpc::codeForToken("apiVersionConflict"), RpcApiVersionConflict);
    static_assert(RpcApiVersionConflict == RpcLast);
}

TEST(ErrorCodes, code_for_token_rejects_unnamed_tokens)
{
    EXPECT_EQ(rpc::codeForToken("notATokenAnyEntryNames"), RpcUnknown);
    EXPECT_EQ(rpc::codeForToken(""), RpcUnknown);

    // Tokens are matched whole, not by prefix, and the comparison is case-sensitive.
    EXPECT_EQ(rpc::codeForToken("invalidParam"), RpcUnknown);
    EXPECT_EQ(rpc::codeForToken("invalidParamsX"), RpcUnknown);
    EXPECT_EQ(rpc::codeForToken("invalidparams"), RpcUnknown);

    // The placeholder that gaps in the table report names no error, so it resolves to nothing.
    EXPECT_EQ(rpc::codeForToken("unknown"), RpcUnknown);
}

TEST(ErrorCodes, every_token_round_trips_to_its_own_code)
{
    // The views the lookup scans and the rows it answers from are two arrays built from one table.
    // Every row's token resolving to that row's code is what proves the two are index-aligned,
    // which no static assertion checks.
    for (int i = RpcBadSyntax; i <= RpcLast; ++i)
    {
        auto const code = static_cast<ErrorCodeI>(i);
        auto const& info = rpc::getErrorInfo(code);

        // Gaps report the placeholder, which is covered above.
        if (info.code == RpcUnknown)
            continue;

        EXPECT_EQ(rpc::codeForToken(info.token.cStr()), code) << "token: " << info.token.cStr();
    }
}
