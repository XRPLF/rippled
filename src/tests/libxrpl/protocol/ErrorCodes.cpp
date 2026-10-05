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
