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

#ifndef RIPPLE_NET_DATABASEDOWNLOADER_H
#define RIPPLE_NET_DATABASEDOWNLOADER_H

#include <ripple/net/DatabaseBody.h>
#include <ripple/net/SSLHTTPDownloader.h>

namespace ripple {

class DatabaseDownloader : public SSLHTTPDownloader
{
public:
    DatabaseDownloader(
        boost::asio::io_service& io_service,
        beast::Journal j,
        Config const& config);

private:
    static const std::uint8_t MAX_PATH_LEN =
        std::numeric_limits<std::uint8_t>::max();

    std::shared_ptr<parser>
    getParser(
        boost::filesystem::path dstPath,
        std::function<void(boost::filesystem::path)> complete,
        boost::system::error_code& ec) override;

    bool
    checkPath(boost::filesystem::path const& dstPath) override;

    void
    closeBody(std::shared_ptr<parser> p) override;

    std::uint64_t
    size(std::shared_ptr<parser> p) override;

    Config const& config_;
    boost::asio::io_service& io_service_;
};

}  // namespace ripple

#endif  // RIPPLE_NET_DATABASEDOWNLOADER_H
