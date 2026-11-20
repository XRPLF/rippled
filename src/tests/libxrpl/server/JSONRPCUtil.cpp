#include <xrpl/server/detail/JSONRPCUtil.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/ErrorCodes.h>

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <string_view>

using namespace xrpl;

namespace {

/**
 * The reply httpReply writes, collected as one string.
 *
 * @param status The HTTP status to write a reply for.
 * @return The whole reply, headers and body.
 */
std::string
reply(int status)
{
    std::string out;
    beast::Journal const journal{beast::Journal::getNullSink()};
    httpReply(status, "{}", [&out](std::string_view s) { out += s; }, journal);
    return out;
}

/**
 * The reply's status line, without the trailing CRLF.
 *
 * @param reply A whole HTTP reply.
 * @return The first line, or the whole reply when it holds no CRLF.
 */
std::string_view
statusLine(std::string const& reply)
{
    auto const end = reply.find("\r\n");
    return std::string_view{reply}.substr(0, end == std::string::npos ? reply.size() : end);
}

}  // namespace

TEST(JSONRPCUtil, status_line_names_the_status)
{
    // Every status this server sends but the two the last test names. The phrase comes from the
    // registry but for the two below.
    EXPECT_EQ(statusLine(reply(200)), "HTTP/1.1 200 OK");
    EXPECT_EQ(statusLine(reply(202)), "HTTP/1.1 202 Accepted");
    EXPECT_EQ(statusLine(reply(400)), "HTTP/1.1 400 Bad Request");
    EXPECT_EQ(statusLine(reply(403)), "HTTP/1.1 403 Forbidden");
    EXPECT_EQ(statusLine(reply(404)), "HTTP/1.1 404 Not Found");
    EXPECT_EQ(statusLine(reply(405)), "HTTP/1.1 405 Method Not Allowed");
    EXPECT_EQ(statusLine(reply(429)), "HTTP/1.1 429 Too Many Requests");
    EXPECT_EQ(statusLine(reply(500)), "HTTP/1.1 500 Internal Server Error");
    EXPECT_EQ(statusLine(reply(501)), "HTTP/1.1 501 Not Implemented");

    // The two whose phrase is this server's own, not the registry's.
    EXPECT_EQ(statusLine(reply(401)), "HTTP/1.1 401 Authorization Required");
    EXPECT_EQ(statusLine(reply(503)), "HTTP/1.1 503 Server is overloaded");
}

TEST(JSONRPCUtil, every_status_the_error_table_names_gets_a_status_line)
{
    // httpReply sees a status, not the table, so only this can check every status the table names.
    std::set<int> statuses;
    for (int i = RpcBadSyntax; i <= RpcLast; ++i)
    {
        auto const& info = rpc::getErrorInfo(static_cast<ErrorCodeI>(i));

        // Gaps in the table name no error, so they report no status of their own.
        if (info.code == RpcUnknown)
            continue;

        statuses.insert(info.httpStatus);
    }
    EXPECT_FALSE(statuses.empty());

    for (int const status : statuses)
    {
        auto const line = std::string{statusLine(reply(status))};
        auto const expectedPrefix = "HTTP/1.1 " + std::to_string(status) + " ";
        EXPECT_TRUE(line.starts_with(expectedPrefix)) << "status: " << status << ", line: " << line;

        // A placeholder phrase means the table names something that is not a status.
        EXPECT_GT(line.size(), expectedPrefix.size()) << "status: " << status;
        EXPECT_EQ(line.find("unknown-status"), std::string::npos) << "status: " << status;
    }
}

TEST(JSONRPCUtil, statuses_without_a_case_take_the_registry_phrase)
{
    // The two statuses the error table names and the switch spells no case for.
    EXPECT_EQ(rpc::errorCodeHttpStatus(RpcHighFee), 402);
    EXPECT_EQ(statusLine(reply(402)), "HTTP/1.1 402 Payment Required");

    EXPECT_EQ(rpc::errorCodeHttpStatus(RpcDbDeserialization), 502);
    EXPECT_EQ(statusLine(reply(502)), "HTTP/1.1 502 Bad Gateway");

    // A number the registry does not know still gets a line, with the placeholder phrase the table
    // test above refuses.
    EXPECT_EQ(statusLine(reply(999)), "HTTP/1.1 999 <unknown-status>");
}
