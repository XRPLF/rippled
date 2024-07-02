//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/net/DatabaseDownloader.h>

namespace ripple {

std::shared_ptr<DatabaseDownloader>
make_DatabaseDownloader(
    boost::asio::io_service& io_service,
    Config const& config,
    beast::Journal j)
{
    return std::shared_ptr<DatabaseDownloader>(
        new DatabaseDownloader(io_service, config, j));
}

DatabaseDownloader::DatabaseDownloader(
    boost::asio::io_service& io_service,
    Config const& config,
    beast::Journal j)
    : HTTPDownloader(io_service, config, j)
    , config_(config)
    , io_service_(io_service)
{
}

auto
DatabaseDownloader::getParser(
    boost::filesystem::path dstPath,
    std::function<void(boost::filesystem::path)> complete,
    boost::system::error_code& ec) -> std::shared_ptr<parser>
{
    using namespace boost::beast;

    auto p = std::make_shared<http::response_parser<DatabaseBody>>();
    p->body_limit(std::numeric_limits<std::uint64_t>::max());
    p->get().body().open(dstPath, config_, io_service_, ec);

    if (ec)
        p->get().body().close();

    return p;
}

bool
DatabaseDownloader::checkPath(boost::filesystem::path const& dstPath)
{
    return dstPath.string().size() <= MAX_PATH_LEN;
}

void
DatabaseDownloader::closeBody(std::shared_ptr<parser> p)
{
    using namespace boost::beast;

    auto databaseBodyParser =
        std::dynamic_pointer_cast<http::response_parser<DatabaseBody>>(p);
    assert(databaseBodyParser);

    databaseBodyParser->get().body().close();
}

std::uint64_t
DatabaseDownloader::size(std::shared_ptr<parser> p)
{
    using namespace boost::beast;

    auto databaseBodyParser =
        std::dynamic_pointer_cast<http::response_parser<DatabaseBody>>(p);
    assert(databaseBodyParser);

    return databaseBodyParser->get().body().size();
}

}  // namespace ripple
