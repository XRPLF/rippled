//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2016 Ripple Labs Inc.

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

#include <beast/core/placeholders.hpp>
#include <beast/core/detail/base64.hpp>
#include <beast/http.hpp>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/app/misc/ValidatorSite.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/strHex.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Sign.h>
#include <test/jtx.h>
#include <test/jtx/TestSuite.h>
#include <boost/utility/in_place_factory.hpp>
#include <boost/asio.hpp>

namespace ripple {
namespace test {

class http_sync_server
{
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

    using req_type = beast::http::request<beast::http::string_body>;
    using resp_type = beast::http::response<beast::http::string_body>;
    using error_code = boost::system::error_code;

    socket_type sock_;
    boost::asio::ip::tcp::acceptor acceptor_;

    std::string list_;

public:
    http_sync_server(endpoint_type const& ep,
            boost::asio::io_service& ios,
            std::pair<PublicKey, SecretKey> keys,
            std::string const& manifest,
            int sequence,
            std::size_t expiration,
            int version,
            std::vector <PublicKey> const& validators)
        : sock_(ios)
        , acceptor_(ios)
    {
        std::string data =
            "{\"sequence\":" + std::to_string(sequence) +
            ",\"expiration\":" + std::to_string(expiration) +
            ",\"validators\":[";

        for (auto const& val : validators)
        {
            data += "{\"validation_public_key\":\"" + strHex (val) + "\"},";
        }
        data.pop_back();
        data += "]}";
        std::string blob = beast::detail::base64_encode(data);

        list_ = "{\"blob\":\"" + blob + "\"";

        auto const sig = sign(keys.first, keys.second, makeSlice(data));

        list_ += ",\"signature\":\"" + strHex(sig) + "\"";
        list_ += ",\"manifest\":\"" + manifest + "\"";
        list_ += ",\"version\":" + std::to_string(version) + '}';

        acceptor_.open(ep.protocol());
        error_code ec;
        acceptor_.set_option(
            boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
        acceptor_.bind(ep);
        acceptor_.listen(boost::asio::socket_base::max_connections);
        acceptor_.async_accept(sock_,
            std::bind(&http_sync_server::on_accept, this,
                beast::asio::placeholders::error));
    }

    ~http_sync_server()
    {
        error_code ec;
        acceptor_.close(ec);
    }

private:
    struct lambda
    {
        int id;
        http_sync_server& self;
        socket_type sock;
        boost::asio::io_service::work work;

        lambda(int id_, http_sync_server& self_,
                socket_type&& sock_)
            : id(id_)
            , self(self_)
            , sock(std::move(sock_))
            , work(sock.get_io_service())
        {
        }

        void operator()()
        {
            self.do_peer(id, std::move(sock));
        }
    };

    void
    on_accept(error_code ec)
    {
        if(! acceptor_.is_open())
            return;
        if(ec)
            return;
        static int id_ = 0;
        std::thread{lambda{++id_, *this, std::move(sock_)}}.detach();
        acceptor_.async_accept(sock_,
            std::bind(&http_sync_server::on_accept, this,
                beast::asio::placeholders::error));
    }

    void
    do_peer(int id, socket_type&& sock0)
    {
        socket_type sock(std::move(sock0));
        beast::streambuf sb;
        error_code ec;
        for(;;)
        {
            req_type req;
            beast::http::read(sock, sb, req, ec);
            if(ec)
                break;
            auto path = req.url;
            if(path != "/validators")
            {
                resp_type res;
                res.status = 404;
                res.reason = "Not Found";
                res.version = req.version;
                res.fields.insert("Server", "http_sync_server");
                res.fields.insert("Content-Type", "text/html");
                res.body = "The file '" + path + "' was not found";
                prepare(res);
                write(sock, res, ec);
                if(ec)
                    break;
            }
            resp_type res;
            res.status = 200;
            res.reason = "OK";
            res.version = req.version;
            res.fields.insert("Server", "http_sync_server");
            res.fields.insert("Content-Type", "application/json");

            res.body = list_;
            try
            {
                prepare(res);
            }
            catch(std::exception const& e)
            {
                res = {};
                res.status = 500;
                res.reason = "Internal Error";
                res.version = req.version;
                res.fields.insert("Server", "http_sync_server");
                res.fields.insert("Content-Type", "text/html");
                res.body =
                    std::string{"An internal error occurred"} + e.what();
                prepare(res);
            }
            write(sock, res, ec);
            if(ec)
                break;
        }
    }
};

class ValidatorSite_test : public beast::unit_test::suite
{
private:
    static
    PublicKey
    randomNode ()
    {
        return derivePublicKey (KeyType::secp256k1, randomSecretKey());
    }

    std::string
    makeManifestString (
        PublicKey const& pk,
        SecretKey const& sk,
        PublicKey const& spk,
        SecretKey const& ssk,
        int seq)
    {
        STObject st(sfGeneric);
        st[sfSequence] = seq;
        st[sfPublicKey] = pk;
        st[sfSigningPubKey] = spk;

        sign(st, HashPrefix::manifest, *publicKeyType(spk), ssk);
        sign(st, HashPrefix::manifest, *publicKeyType(pk), sk,
            sfMasterSignature);

        Serializer s;
        st.add(s);

        return beast::detail::base64_encode (std::string(
            static_cast<char const*> (s.data()), s.size()));
    }

    void
    testConfigLoad ()
    {
        testcase ("Config Load");

        using namespace jtx;

        Env env (*this);
        auto trustedSites = std::make_unique<ValidatorSite> (
            env.app(), beast::Journal());

        // load should accept empty sites list
        std::vector<std::string> emptyCfgSites;
        BEAST_EXPECT(trustedSites->load (emptyCfgSites));

        // load should accept valid validator site uris
        std::vector<std::string> cfgSites({
            "http://ripple.com/",
            "http://ripple.com/validators",
            "http://ripple.com:8080/validators",
            "http://207.261.33.37/validators",
            "http://207.261.33.37:8080/validators",
            "https://ripple.com/validators",
            "https://ripple.com:443/validators"});
        BEAST_EXPECT(trustedSites->load (cfgSites));

        // load should reject validator site uris with invalid schemes
        std::vector<std::string> badSites(
            {"ftp://ripple.com/validators"});
        BEAST_EXPECT(!trustedSites->load (badSites));

        badSites[0] = "wss://ripple.com/validators";
        BEAST_EXPECT(!trustedSites->load (badSites));

        badSites[0] = "ripple.com/validators";
        BEAST_EXPECT(!trustedSites->load (badSites));
    }

    void
    testFetchList ()
    {
        testcase ("Fetch list");

        using namespace jtx;

        Env env (*this);
        auto& ioService = env.app ().getIOService ();
        auto& trustedKeys = env.app ().validators ();

        beast::Journal journal;

        PublicKey emptyLocalKey;
        std::vector<std::string> emptyCfgKeys;

        auto const publisherSecret1 = randomSecretKey();
        auto const publisherPublic1 =
            derivePublicKey(KeyType::ed25519, publisherSecret1);
        auto const pubSigningKeys1 = randomKeyPair(KeyType::secp256k1);

        auto const manifest1 = makeManifestString (
            publisherPublic1, publisherSecret1,
            pubSigningKeys1.first, pubSigningKeys1.second, 1);

        auto const publisherSecret2 = randomSecretKey();
        auto const publisherPublic2 =
            derivePublicKey(KeyType::ed25519, publisherSecret2);
        auto const pubSigningKeys2 = randomKeyPair(KeyType::secp256k1);

        auto const manifest2 = makeManifestString (
            publisherPublic2, publisherSecret2,
            pubSigningKeys2.first, pubSigningKeys2.second, 1);

        std::vector<std::string> cfgPublishers({
            strHex(publisherPublic1),
            strHex(publisherPublic2)});

        BEAST_EXPECT(trustedKeys.load (
            emptyLocalKey, emptyCfgKeys, cfgPublishers));

        auto constexpr listSize = 20;
        std::vector<PublicKey> list1;
        list1.reserve (listSize);
        while (list1.size () < listSize)
            list1.push_back (randomNode());

        std::vector<PublicKey> list2;
        list2.reserve (listSize);
        while (list2.size () < listSize)
            list2.push_back (randomNode());

        std::uint16_t constexpr port1 = 7475;
        std::uint16_t constexpr port2 = 7476;

        using endpoint_type = boost::asio::ip::tcp::endpoint;
        using address_type = boost::asio::ip::address;

        endpoint_type ep1{address_type::from_string("127.0.0.1"), port1};
        endpoint_type ep2{address_type::from_string("127.0.0.1"), port2};

        auto const sequence = 1;
        auto const version = 1;
        NetClock::time_point const expiration =
            env.timeKeeper().now() + 3600s;

        http_sync_server server1(
            ep1, ioService, pubSigningKeys1, manifest1, sequence,
            expiration.time_since_epoch().count(), version, list1);

        http_sync_server server2(
            ep2, ioService, pubSigningKeys2, manifest2, sequence,
            expiration.time_since_epoch().count(), version, list2);

        {
            // fetch single site
            std::vector<std::string> cfgSites(
            {"http://127.0.0.1:" + std::to_string(port1) + "/validators"});

            auto sites = std::make_unique<ValidatorSite> (
                env.app (), journal);

            sites->load (cfgSites);
            sites->start();
            sites->join();

            for (auto const& val : list1)
                BEAST_EXPECT(trustedKeys.listed (val));
        }
        {
            // fetch multiple sites
            std::vector<std::string> cfgSites({
            "http://127.0.0.1:" + std::to_string(port1) + "/validators",
            "http://127.0.0.1:" + std::to_string(port2) + "/validators"});

            auto sites = std::make_unique<ValidatorSite> (
                env.app (), journal);

            sites->load (cfgSites);
            sites->start();
            sites->join();

            for (auto const& val : list1)
                BEAST_EXPECT(trustedKeys.listed (val));

            for (auto const& val : list2)
                BEAST_EXPECT(trustedKeys.listed (val));
        }
    }

public:
    void
    run() override
    {
        testConfigLoad ();
        testFetchList ();
    }
};

BEAST_DEFINE_TESTSUITE(ValidatorSite, app, ripple);

} // test
} // ripple
