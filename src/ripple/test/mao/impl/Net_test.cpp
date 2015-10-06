//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/basics/Log.h>
#include <ripple/test/mao/Net.h>
#include <ripple/net/HTTPClient.h>
#include <ripple/net/RPCCall.h>
#include <beast/unit_test/suite.h>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace ripple {
namespace test {
namespace mao {

struct TestApp
{
    TestApp()
    {
        auto config = std::make_unique<Config>();
        setupConfigForUnitTests(*config);
        config->RUN_STANDALONE = true;
        (*config)["server"].append("port_peer");
        (*config)["port_peer"].set("ip", "127.0.0.1");
        (*config)["port_peer"].set("port", "8080");
        (*config)["port_peer"].set("protocol", "peer");
        (*config)["server"].append("port_admin");
        (*config)["port_admin"].set("ip", "127.0.0.1");
        (*config)["port_admin"].set("port", "8081");
        (*config)["port_admin"].set("protocol", "http");
        (*config)["port_admin"].set("admin", "127.0.0.1");
        // Hack so we dont have to call Config::setup
        HTTPClient::initializeSSLContext(*config);
        auto logs = std::make_unique<Logs>();
        instance = make_Application(
            std::move(config), std::move(logs));
        instance->setup();
        thread_ = std::thread(
            [&]() { instance->run(); });
    }

    ~TestApp()
    {
        if (thread_.joinable())
        {
            instance->signalStop();
            thread_.join();
        }
    }

    void
    join()
    {
        thread_.join();
    }

    Application*
    operator->()
    {
        return instance.get();
    }

    template <class T, class... Args>
    void
    rpc (T const& t, Args const&... args)
    {
        std::vector<std::string> v;
        collect(v, t, args...);
        RPCCall::fromCommandLine(
            instance->config(), v,
                instance->logs());
    }

private:
    inline
    void
    collect (std::vector<std::string>& v)
    {
    }

    template <class T, class... Args>
    void
    collect (std::vector<std::string>& v,
        T const& t, Args const&... args)
    {
        v.emplace_back(t);
        collect(v, args...);
    }

    std::unique_ptr<Application> instance;
    std::thread thread_;
    std::mutex mutex_;
};

class Net_test : public beast::unit_test::suite
{
public:
    void
    testStartStop()
    {
        TestApp app;
        pass();
    }

    void
    testRPC()
    {
        TestApp app;
        app.rpc("stop");
        app.join();
        pass();
    }

    void
    run() override
    {
        testStartStop();
        testRPC();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(Net,mao,ripple)

} // mao
} // test
} // ripple
