#include <test/jtx.h>

#include <xrpld/app/misc/detail/WorkSSL.h>

#include <xrpl/basics/StringUtilities.h>

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
            env_.app().getIOContext(),
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
        tcp::resolver resolver(env_.app().getIOContext());
        std::string port = pUrl_.port ? std::to_string(*pUrl_.port) : "443";
        auto results = resolver.resolve(pUrl_.domain, port);
        auto it = results.begin();
        auto end = results.end();
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
        // should resolve to the same endpoint. Run a few times
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
        // random but last endpoint is not selected.
        resolved_.clear();
        for (int i = 0; i < 4; ++i)
            makeRequest(lastEndpoint_, false);
        // Should have more than one but some endpoints can repeat since
        // selected at random. We'll never have four identical endpoints
        // here because on failure we randomly select an endpoint different
        // from the last endpoint.
        BEAST_EXPECT(resolved_.size() > 1);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(DNS, app, ripple, 20);

}  // namespace test
}  // namespace ripple
