//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2020 Ripple Labs Inc.

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

#include <ripple/app/misc/detail/WorkSSL.h>
#include <ripple/basics/StringUtilities.h>
#include <test/jtx.h>

#include <condition_variable>
#include <memory>

namespace ripple {
namespace test {

class DNS_test : public beast::unit_test::suite
{
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using error_code = boost::system::error_code;
    std::weak_ptr<ripple::detail::Work> work_;
    endpoint_type lastEndpoint_{};
    parsedURL pUrl_;
    std::string port_;
    jtx::Env env_;
    std::map<std::string, int> resolved_;
    std::mutex mutex_;
    std::condition_variable cv_;

public:
    DNS_test() : env_(*this)
    {
    }

    void
    makeRequest(endpoint_type const& lastEndpoint, bool lastStatus)
    {
        auto onFetch = [&](error_code const& errorCode,
                           endpoint_type const& endpoint,
                           ripple::detail::response_type&& resp) {
            BEAST_EXPECT(!errorCode);
            lastEndpoint_ = endpoint;
            resolved_[endpoint.address().to_string()]++;
            cv_.notify_all();
        };

        auto sp = std::make_shared<ripple::detail::WorkSSL>(
            pUrl_.domain,
            pUrl_.path,
            port_,
            env_.app().getIOService(),
            env_.journal,
            env_.app().config(),
            lastEndpoint,
            lastStatus,
            onFetch);
        work_ = sp;
        sp->run();

        std::unique_lock l(mutex_);
        cv_.wait(l);
    }

    bool
    isMultipleEndpoints()
    {
        using boost::asio::ip::tcp;
        tcp::resolver resolver(env_.app().getIOService());
        tcp::resolver::query query(pUrl_.domain, port_);
        tcp::resolver::iterator it = resolver.resolve(query);
        tcp::resolver::iterator end;
        int n = 0;
        for (; it != end; ++it)
            ++n;
        return n > 1;
    }

    void
    parse()
    {
        std::string url = arg();
        if (url == "")
            url = "https://vl.ripple.com";
        BEAST_EXPECT(parseUrl(pUrl_, url));
        port_ = pUrl_.port ? std::to_string(*pUrl_.port) : "443";
    }

    void
    run() override
    {
        parse();
        // First endpoint is random. Next three
        // hould resolve to the same endpoint. Run a few times
        // to verify we are not selecting by chance the same endpoint.
        for (int i = 1; i <= 4; ++i)
        {
            makeRequest(lastEndpoint_, true);
            BEAST_EXPECT(
                resolved_.size() == 1 && resolved_.begin()->second == i);
        }
        if (!isMultipleEndpoints())
            return;
        // Run with the "failed" status. In this case endpoints are selected at
        // random.
        for (int i = 0; i < 4; ++i)
            makeRequest(lastEndpoint_, false);
        // Should have more than one but some endpoints can repeat since
        // selected at random.
        BEAST_EXPECT(resolved_.size() > 1);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(DNS, ripple_data, ripple, 20);

}  // namespace test
}  // namespace ripple
