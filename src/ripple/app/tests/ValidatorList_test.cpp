//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2015 Ripple Labs Inc.

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
#include <ripple/basics/Slice.h>
#include <ripple/basics/strHex.h>
#include <ripple/basics/TestSuite.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Sign.h>
#include <boost/utility/in_place_factory.hpp>
#include <boost/asio.hpp>

namespace ripple {
namespace tests {

struct Validator
{
    PublicKey masterPublicKey;
    PublicKey signingPublicKey;
    std::string manifest;
};

class http_sync_server
{
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

    using req_type = beast::http::request_v1<beast::http::string_body>;
    using resp_type = beast::http::response_v1<beast::http::string_body>;
    using error_code = boost::system::error_code;

    socket_type sock_;
    boost::asio::ip::tcp::acceptor acceptor_;

    std::string list_;

public:
    http_sync_server(endpoint_type const& ep,
            boost::asio::io_service& ios,
            std::pair<PublicKey, SecretKey> keys,
            std::string manifest,
            int sequence,
            int version,
            std::vector <Validator> validators)
        : sock_(ios)
        , acceptor_(ios)
    {
        std::string data =
            "{\"sequence\":" + std::to_string(sequence) + ",\"validators\":[";

        for (auto const& val : validators)
        {
            data += "{\"validation_public_key\":\"" +
                toBase58(TokenType::TOKEN_NODE_PUBLIC, val.masterPublicKey) +
                "\",\"validation_manifest\":\"" + val.manifest + "\"},";
        }
        data.pop_back();
        data += "]}";
        std::string blob = beast::detail::base64_encode(data);

        list_ = "{\"blob\":\"" + blob + "\"";

        auto const sig = signDigest(
            keys.first, keys.second, sha512Half(makeSlice(data)));

        list_ += ",\"signature\":\"" + strHex(sig) + "\"";
        list_ += ",\"manifest\":\"" + manifest + "\"";
        list_ += ",\"version\":" + std::to_string(version) + '}';

        acceptor_.open(ep.protocol());
        acceptor_.bind(ep);
        acceptor_.listen(
            boost::asio::socket_base::max_connections);
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
                res.headers.insert("Server", "http_sync_server");
                res.headers.insert("Content-Type", "text/html");
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
            res.headers.insert("Server", "http_sync_server");
            res.headers.insert("Content-Type", "application/json");

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
                res.headers.insert("Server", "http_sync_server");
                res.headers.insert("Content-Type", "text/html");
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

class ValidatorList_test : public ripple::TestSuite
{
private:
    static
    PublicKey
    randomNode ()
    {
        return derivePublicKey (
            KeyType::secp256k1,
            randomSecretKey());
    }

    static
    PublicKey
    randomMasterKey ()
    {
        return derivePublicKey (
            KeyType::ed25519,
            randomSecretKey());
    }

    Validator
    randomValidator ()
    {
        auto const masterSecret = randomSecretKey();
        auto const masterPublic = derivePublicKey(
            KeyType::ed25519, masterSecret);
        auto const signingPublic = randomKeyPair(KeyType::secp256k1).first;
        return Validator({
            masterPublic, signingPublic,
            make_Manifest (1, signingPublic, masterSecret, masterPublic)});
    }

    class TestThread
    {
    private:
        boost::asio::io_service io_service_;
        boost::optional<boost::asio::io_service::work> work_;
        std::thread thread_;

    public:
        TestThread()
            : work_(boost::in_place(std::ref(io_service_)))
            , thread_([&]() { this->io_service_.run(); })
        {
        }

        ~TestThread()
        {
            work_ = boost::none;
            thread_.join();
        }

        boost::asio::io_service&
        get_io_service()
        {
            return io_service_;
        }
    };

    Manifest
    make_Manifest (
        PublicKey const& pk,
        SecretKey const& sk,
        PublicKey const& spk,
        int seq)
    {
        STObject st(sfGeneric);
        st[sfSequence] = seq;
        st[sfPublicKey] = pk;
        st[sfSigningPubKey] = spk;

        sign(st, HashPrefix::manifest, KeyType::ed25519, sk);
        BEAST_EXPECT(verify(st, HashPrefix::manifest, pk, true));

        Serializer s;
        st.add(s);

        std::string const m (static_cast<char const*> (s.data()), s.size());
        if (auto r = ripple::make_Manifest (std::move (m)))
            return std::move (*r);
        Throw<std::runtime_error> ("Could not create a manifest");
        return *ripple::make_Manifest(std::move(m)); // Silence compiler warning.
    }

    std::string
    make_Manifest (
        int seq,
        PublicKey const& spk,
        SecretKey const& sk,
        PublicKey const& pk)
    {
        STObject st(sfGeneric);
        st[sfSequence] = seq;
        st[sfPublicKey] = pk;
        st[sfSigningPubKey] = spk;

        sign(st, HashPrefix::manifest, KeyType::ed25519, sk);
        BEAST_EXPECT(verify(st, HashPrefix::manifest, pk, true));

        Serializer s;
        st.add(s);

        return beast::detail::base64_encode(
            std::string(static_cast<char const*> (s.data()), s.size()));
    }

    void
    testConfigLoad ()
    {
        testcase ("Config Load");

        ManifestCache manifests;
        TestThread thread;

        auto validators = std::make_unique <ValidatorList> (
            manifests, thread.get_io_service (), beast::Journal ());

        auto format = [](
            PublicKey const &publicKey,
            char const* comment = nullptr)
        {
            auto ret = toBase58(
                TokenType::TOKEN_NODE_PUBLIC,
                publicKey);

            if (comment)
                ret += comment;

            return ret;
        };

        PublicKey emptyLocalKey;
        std::vector<std::string> emptyCfgValidators;
        std::vector<std::string> emptyCfgListSites;
        std::vector<std::string> emptyCfgListKeys;
        std::vector<std::string> emptyCfgManifest;
        {
            // Correct (empty) configuration
            BEAST_EXPECT(validators->load (
                emptyLocalKey, emptyCfgValidators, emptyCfgListSites,
                emptyCfgListKeys, emptyCfgManifest));
        }
        {
            // load local validator key with or without manifest
            auto const localSigningKey = randomNode();
            auto const localMasterSecret = randomSecretKey();
            auto const localMasterPublic = derivePublicKey(
                KeyType::ed25519, localMasterSecret);

            std::vector<std::string> const cfgManifest ({
                make_Manifest (
                    1, localSigningKey, localMasterSecret, localMasterPublic)});

            BEAST_EXPECT(validators->load (
                localSigningKey, emptyCfgValidators, emptyCfgListSites,
                emptyCfgListKeys, emptyCfgManifest));
            BEAST_EXPECT(validators->listed (localSigningKey));

            BEAST_EXPECT(validators->load (
                localSigningKey, emptyCfgValidators, emptyCfgListSites,
                emptyCfgListKeys, cfgManifest));
            BEAST_EXPECT(validators->listed (localMasterPublic));
            BEAST_EXPECT(validators->listed (localSigningKey));
        }
        {
            // load should reject invalid config manifest
            auto const localSigningKey = randomNode();
            auto const localMasterSecret = randomSecretKey();
            auto const localMasterPublic = derivePublicKey(
                KeyType::ed25519, localMasterSecret);

            std::vector<std::string> const badManifest ({
                make_Manifest (
                    1, randomNode(), localMasterSecret, localMasterPublic)});

            BEAST_EXPECT(! validators->load (
                localSigningKey, emptyCfgValidators, emptyCfgListSites,
                emptyCfgListKeys, badManifest));

            std::vector<std::string> const revokedManifest ({
                make_Manifest (
                    std::numeric_limits<std::uint32_t>::max (),
                    localSigningKey, localMasterSecret, localMasterPublic)});

            BEAST_EXPECT(! validators->load (
                localSigningKey, emptyCfgValidators, emptyCfgListSites,
                emptyCfgListKeys, revokedManifest));
        }
        {
            std::vector<PublicKey> configList;
            configList.reserve(8);

            while (configList.size () != 8)
                configList.push_back (randomNode());

            // Correct configuration
            std::vector<std::string> cfgValidators ({
                format (configList[0]),
                format (configList[1], " Comment"),
                format (configList[2], " Multi Word Comment"),
                format (configList[3], "    Leading Whitespace"),
                format (configList[4], " Trailing Whitespace    "),
                format (configList[5], "    Leading & Trailing Whitespace    "),
                format (configList[6], "    Leading, Trailing & Internal    Whitespace    "),
                format (configList[7], "    ")
            });

            // Validators loaded from config are added to the list
            {
                auto valList = std::make_unique <ValidatorList> (
                    manifests, thread.get_io_service (), beast::Journal ());

                BEAST_EXPECT(valList->load (
                    emptyLocalKey, cfgValidators, emptyCfgListSites,
                    emptyCfgListKeys, emptyCfgManifest));
                BEAST_EXPECT(valList->quorum () == 0);

                for (auto const& n : configList)
                    BEAST_EXPECT(valList->listed (n));
            }
            {
                // local validator key on config list
                auto valList = std::make_unique <ValidatorList> (
                    manifests, thread.get_io_service (), beast::Journal ());

                auto const localSigningKey = parseBase58<PublicKey> (
                    TokenType::TOKEN_NODE_PUBLIC, cfgValidators.front());

                BEAST_EXPECT(valList->load (
                    *localSigningKey, cfgValidators, emptyCfgListSites,
                    emptyCfgListKeys, emptyCfgManifest));
                BEAST_EXPECT(valList->quorum () == 0);

                BEAST_EXPECT(valList->listed (*localSigningKey));
                for (auto const& n : configList)
                    BEAST_EXPECT(valList->listed (n));
            }
            {
                // local validator key not on config list
                auto valList = std::make_unique <ValidatorList> (
                    manifests, thread.get_io_service (), beast::Journal ());

                auto const localSigningKey = randomNode();
                BEAST_EXPECT(valList->load (
                    localSigningKey, cfgValidators, emptyCfgListSites,
                    emptyCfgListKeys, emptyCfgManifest));
                BEAST_EXPECT(valList->quorum () == 0);

                BEAST_EXPECT(valList->listed (localSigningKey));
                for (auto const& n : configList)
                    BEAST_EXPECT(valList->listed (n));
            }
            {
                // local validator key (with manifest) not on config list
                auto valList = std::make_unique <ValidatorList> (
                    manifests, thread.get_io_service (), beast::Journal ());

                auto const localSigningKey = randomNode();
                auto const localMasterSecret = randomSecretKey();
                auto const localMasterPublic = derivePublicKey(
                    KeyType::ed25519, localMasterSecret);

                std::vector<std::string> const cfgManifest ({
                    make_Manifest (
                        1, localSigningKey, localMasterSecret, localMasterPublic)});

                BEAST_EXPECT(valList->load (
                    localSigningKey, cfgValidators, emptyCfgListSites,
                    emptyCfgListKeys, cfgManifest));
                BEAST_EXPECT(valList->quorum () == 0);

                BEAST_EXPECT(valList->listed (localSigningKey));
                BEAST_EXPECT(valList->listed (localMasterPublic));
                for (auto const& n : configList)
                    BEAST_EXPECT(valList->listed (n));
            }
        }
        {
            // load should reject invalid configurations
            std::vector<std::string> cfgValidators(
                {"NotAPublicKey"});
            BEAST_EXPECT(!validators->load (
                emptyLocalKey, cfgValidators, emptyCfgListSites,
                emptyCfgListKeys, emptyCfgManifest));

            cfgValidators[0] = format (randomNode(), "!");
            BEAST_EXPECT(!validators->load (
                emptyLocalKey, cfgValidators, emptyCfgListSites,
                emptyCfgListKeys, emptyCfgManifest));

            cfgValidators[0] = format (randomNode(), "!  Comment");
            BEAST_EXPECT(!validators->load (
                emptyLocalKey, cfgValidators, emptyCfgListSites,
                emptyCfgListKeys, emptyCfgManifest));
        }
        {
            // Check if we properly terminate when we encounter
            // a malformed or unparseable entry:
            auto const node1 = randomNode();
            auto const node2 = randomNode ();

            std::vector<std::string> cfgValidators({
                format (node1, "XXX"),
                format (node2)
            });
            BEAST_EXPECT(!validators->load (
                emptyLocalKey, cfgValidators, emptyCfgListSites,
                emptyCfgListKeys, emptyCfgManifest));
            BEAST_EXPECT(!validators->trusted (node1));
            BEAST_EXPECT(!validators->listed (node1));
            BEAST_EXPECT(!validators->trusted (node2));
            BEAST_EXPECT(!validators->listed (node1));
        }
        {
            // load should accept Ed25519 master public keys
            auto const masterNode1 = randomMasterKey ();
            auto const masterNode2 = randomMasterKey ();

            std::vector<std::string> cfgValidators({
                format (masterNode1),
                format (masterNode2, " Comment")
            });
            BEAST_EXPECT(validators->load (
                emptyLocalKey, cfgValidators, emptyCfgListSites,
                emptyCfgListKeys, emptyCfgManifest));
            BEAST_EXPECT(validators->quorum () == 0);
            BEAST_EXPECT(!validators->trusted (masterNode1));
            BEAST_EXPECT(validators->listed (masterNode1));
            BEAST_EXPECT(!validators->trusted (masterNode2));
            BEAST_EXPECT(validators->listed (masterNode2));
        }
        {
            // load should accept valid validator site uris
            std::vector<std::string> cfgSites({
                "http://ripple.com/",
                "http://ripple.com/validators",
                "http://ripple.com:8080/validators",
                "http://207.261.33.37/validators",
                "http://207.261.33.37:8080/validators",
                "https://ripple.com/validators",
                "https://ripple.com:443/validators"});
            BEAST_EXPECT(validators->load (
                emptyLocalKey, emptyCfgValidators, cfgSites,
                emptyCfgListKeys, emptyCfgManifest));
        }
        {
            // load should reject validator site uris with invalid schemes
            std::vector<std::string> cfgSites(
                {"ftp://ripple.com/validators"});
            BEAST_EXPECT(!validators->load (
                emptyLocalKey, emptyCfgValidators, cfgSites,
                emptyCfgListKeys, emptyCfgManifest));

            cfgSites[0] = "wss://ripple.com/validators";
            BEAST_EXPECT(!validators->load (
                emptyLocalKey, emptyCfgValidators, cfgSites,
                emptyCfgListKeys, emptyCfgManifest));
        }
        {
            // load should accept valid validator list signing keys
            auto const nKeys = 3;
            std::vector<PublicKey> keys;
            while (keys.size () < nKeys)
                keys.push_back (randomMasterKey ());

            std::vector<std::string> cfgListKeys;
            for (auto const& key : keys)
                cfgListKeys.push_back (
                    toBase58 (
                        TokenType::TOKEN_ACCOUNT_PUBLIC,
                        key));

            BEAST_EXPECT(validators->load (
                emptyLocalKey, emptyCfgValidators, emptyCfgListSites,
                cfgListKeys, emptyCfgManifest));
            for (auto const& key : keys)
                BEAST_EXPECT(validators->trustedPublisher (key));
        }
        {
            // load should reject invalid validator list signing keys
            std::vector<std::string> cfgListKeys(
                {"NotASigningKey"});
            BEAST_EXPECT(!validators->load (
                emptyLocalKey, emptyCfgValidators, emptyCfgListSites,
                cfgListKeys, emptyCfgManifest));
        }
        {
            // load should reject secp256k1 validator list signing keys
            auto const nKeys = 3;
            std::vector<PublicKey> keys;
            while (keys.size () < nKeys)
                keys.push_back (randomNode ());

            std::vector<std::string> cfgListKeys;
            for (auto const& key : keys)
                cfgListKeys.push_back (
                    toBase58 (
                        TokenType::TOKEN_ACCOUNT_PUBLIC,
                        key));

            BEAST_EXPECT(! validators->load (
                emptyLocalKey, emptyCfgValidators, emptyCfgListSites,
                cfgListKeys, emptyCfgManifest));
            for (auto const& key : keys)
                BEAST_EXPECT(!validators->trustedPublisher (key));
        }
        {
            // load should reject validator list signing keys with invalid encoding
            auto const nKeys = 3;
            std::vector<PublicKey> keys;
            while (keys.size () < nKeys)
                keys.push_back (randomMasterKey ());

            std::vector<std::string> cfgListKeys;
            for (auto const& key : keys)
                cfgListKeys.push_back (
                    toBase58 (
                        TokenType::TOKEN_NODE_PUBLIC,
                        key));

            BEAST_EXPECT(! validators->load (
                emptyLocalKey, emptyCfgValidators, emptyCfgListSites,
                cfgListKeys, emptyCfgManifest));
            for (auto const& key : keys)
                BEAST_EXPECT(!validators->trustedPublisher (key));
        }
    }

    void
    testFetchList ()
    {
        testcase ("Fetch list");

        auto constexpr listSize = 20;
        auto constexpr activePerList = 15;

        ValidationSet activeValidators;
        activeValidators.reserve (activePerList * 2);

        std::vector<Validator> list1;
        list1.reserve (listSize);
        while (list1.size () < listSize)
        {
            list1.push_back (randomValidator());
            if (list1.size () <= activePerList)
                activeValidators[calcNodeID (list1.back().signingPublicKey)];
        }

        std::vector<Validator> list2;
        list2.reserve (listSize);
        while (list2.size () < listSize)
        {
            list2.push_back (randomValidator());
            if (list2.size () <= activePerList)
                activeValidators[calcNodeID (list2.back().signingPublicKey)];
        }

        BEAST_EXPECT(activeValidators.size () == activePerList * 2);

        auto const sequence = 1;
        auto const version = 1;

        std::uint16_t constexpr port1 = 7475;
        std::uint16_t constexpr port2 = 7476;

        using endpoint_type = boost::asio::ip::tcp::endpoint;
        using address_type = boost::asio::ip::address;

        endpoint_type ep1{address_type::from_string("0.0.0.0"), port1};
        endpoint_type ep2{address_type::from_string("0.0.0.0"), port2};

        auto const masterSecret1 = randomSecretKey();
        auto const masterPublic1 = derivePublicKey(KeyType::ed25519, masterSecret1);
        auto const signingKeys1 = randomKeyPair(KeyType::secp256k1);

        auto const manifest1 = make_Manifest (
            1, signingKeys1.first, masterSecret1, masterPublic1);

        auto const masterSecret2 = randomSecretKey();
        auto const masterPublic2 = derivePublicKey(KeyType::ed25519, masterSecret2);
        auto const signingKeys2 = randomKeyPair(KeyType::secp256k1);

        auto const manifest2 = make_Manifest (
            1, signingKeys2.first, masterSecret2, masterPublic2);

        TestThread serverThread;
        http_sync_server server1(
            ep1, serverThread.get_io_service(),
            signingKeys1, manifest1, sequence, version, list1);

        TestThread thread;
        PublicKey emptyLocalKey;
        std::vector<std::string> emptyCfgValidators;
        std::vector<std::string> emptyCfgManifest;

        std::vector<std::string> cfgSites1(
            {"http://localhost:" + std::to_string(port1) + "/validators"});

        std::vector<std::string> cfgSites2({
            "http://localhost:" + std::to_string(port1) + "/validators",
            "http://localhost:" + std::to_string(port2) + "/validators"});

        std::vector<std::string> cfgKeys1({
            toBase58(
                TokenType::TOKEN_ACCOUNT_PUBLIC,
                masterPublic1)});

        std::vector<std::string> cfgKeys2({
            toBase58(
                TokenType::TOKEN_ACCOUNT_PUBLIC,
                masterPublic1),
            toBase58(
                TokenType::TOKEN_ACCOUNT_PUBLIC,
                masterPublic2)});
        {
            // fetch single list
            ManifestCache manifests;
            auto validators = std::make_unique <ValidatorList> (
                manifests, thread.get_io_service (), beast::Journal ());

            BEAST_EXPECT(validators->load (
                emptyLocalKey, emptyCfgValidators, cfgSites1,
                cfgKeys1, emptyCfgManifest));
            BEAST_EXPECT(validators->trustedPublisher (masterPublic1));

            while (validators->getFetchedSitesCount () == 0)
                std::this_thread::sleep_for (std::chrono::milliseconds (10));

            validators->update (activeValidators);
            BEAST_EXPECT(validators->quorum () == 12);

            auto i = 0;
            for (auto const& val : list1)
            {
                BEAST_EXPECT(validators->listed (val.masterPublicKey));
                BEAST_EXPECT(validators->listed (val.signingPublicKey));
                BEAST_EXPECT(
                    validators->trusted (val.signingPublicKey) ==
                    (i++ < activePerList));
            }
        }
        {
            // do not use list from untrusted publisher
            auto const untrustedMasterSecret = randomSecretKey();
            auto const untrustedMasterPublic =
                derivePublicKey(KeyType::ed25519, untrustedMasterSecret);
            auto const untrustedKeys = randomKeyPair(KeyType::secp256k1);

            auto const untrustedManifest = make_Manifest (
                1, untrustedKeys.first,
                untrustedMasterSecret, untrustedMasterPublic);

            http_sync_server server2(
                ep2, serverThread.get_io_service(),
                untrustedKeys, untrustedManifest, sequence, version, list1);

            ManifestCache manifests;
            auto validators = std::make_unique <ValidatorList> (
                manifests, thread.get_io_service (), beast::Journal ());

            std::vector<std::string> cfgSites({
                "http://localhost:" + std::to_string(port2) + "/validators"});

            BEAST_EXPECT(validators->load (
                emptyLocalKey, emptyCfgValidators, cfgSites,
                cfgKeys2, emptyCfgManifest));
            BEAST_EXPECT(validators->trustedPublisher (masterPublic1));
            BEAST_EXPECT(! validators->trustedPublisher (
                untrustedMasterPublic));

            auto tries = 0;
            while (validators->getFetchedSitesCount () == 0 && tries++ < 10)
                std::this_thread::sleep_for (std::chrono::milliseconds (10));

            validators->update (activeValidators);
            BEAST_EXPECT(validators->quorum () == 0);

            for (auto const& val : list1)
            {
                BEAST_EXPECT(! validators->listed (val.masterPublicKey));
                BEAST_EXPECT(! validators->listed (val.signingPublicKey));
                BEAST_EXPECT(! validators->trusted (val.signingPublicKey));
            }
        }
        {
            // do not use list with unhandled version
            auto const badVersion = 666;

            http_sync_server server2(
                ep2, serverThread.get_io_service(),
                signingKeys1, manifest1, sequence, badVersion, list1);

            ManifestCache manifests;
            auto validators = std::make_unique <ValidatorList> (
                manifests, thread.get_io_service (), beast::Journal ());

            std::vector<std::string> cfgSites({
                "http://localhost:" + std::to_string(port2) + "/validators"});

            BEAST_EXPECT(validators->load (
                emptyLocalKey, emptyCfgValidators, cfgSites,
                cfgKeys2, emptyCfgManifest));
            BEAST_EXPECT(validators->trustedPublisher (masterPublic1));

            auto tries = 0;
            while (validators->getFetchedSitesCount () == 0 && tries++ < 10)
                std::this_thread::sleep_for (std::chrono::milliseconds (10));

            validators->update (activeValidators);
            BEAST_EXPECT(validators->quorum () == 0);

            for (auto const& val : list1)
            {
                BEAST_EXPECT(! validators->listed (val.masterPublicKey));
                BEAST_EXPECT(! validators->listed (val.signingPublicKey));
                BEAST_EXPECT(! validators->trusted (val.signingPublicKey));
            }
        }
        {
            // fetch list with highest sequence number
            auto const highSequence = 1000;

            http_sync_server server2(
                ep2, serverThread.get_io_service(),
                signingKeys1, manifest1, highSequence, version, list2);

            ManifestCache manifests;
            auto validators = std::make_unique <ValidatorList> (
                manifests, thread.get_io_service (), beast::Journal ());

            BEAST_EXPECT(validators->load (
                emptyLocalKey, emptyCfgValidators, cfgSites2,
                cfgKeys2, emptyCfgManifest));
            BEAST_EXPECT(validators->trustedPublisher (masterPublic1));

            auto tries = 0;
            while (validators->getFetchedSitesCount () < 2 && tries++ < 10)
                std::this_thread::sleep_for (std::chrono::milliseconds (10));

            validators->update (activeValidators);
            BEAST_EXPECT(validators->quorum () == 12);

            for (auto const& val : list1)
            {
                BEAST_EXPECT(! validators->listed (val.masterPublicKey));
                BEAST_EXPECT(! validators->listed (val.signingPublicKey));
                BEAST_EXPECT(! validators->trusted (val.signingPublicKey));
            }

            auto i = 0;
            for (auto const& val : list2)
            {
                BEAST_EXPECT(validators->listed (val.masterPublicKey));
                BEAST_EXPECT(validators->listed (val.signingPublicKey));
                BEAST_EXPECT(
                    validators->trusted (val.signingPublicKey) ==
                    (i++ < activePerList));
            }
        }
        {
            // fetch multiple lists
            http_sync_server server2(
                ep2, serverThread.get_io_service(),
                signingKeys2, manifest2, sequence, version, list2);

            ManifestCache manifests;
            auto validators = std::make_unique <ValidatorList> (
                manifests, thread.get_io_service (), beast::Journal ());

            BEAST_EXPECT(validators->load (
                emptyLocalKey, emptyCfgValidators, cfgSites2,
                cfgKeys2, emptyCfgManifest));
            BEAST_EXPECT(validators->trustedPublisher (masterPublic1));
            BEAST_EXPECT(validators->trustedPublisher (masterPublic2));

            auto tries = 0;
            while (validators->getFetchedSitesCount () < 2 && tries++ < 10)
                std::this_thread::sleep_for (std::chrono::milliseconds (10));

            validators->update (activeValidators);
            BEAST_EXPECT(validators->quorum () == 13);

            auto i = 0;
            auto nTrusted = 0;
            for (auto const& val : list1)
            {
                BEAST_EXPECT(validators->listed (val.masterPublicKey));
                BEAST_EXPECT(validators->listed (val.signingPublicKey));
                if (validators->trusted (val.signingPublicKey))
                {
                    if (i++ < activePerList)
                        ++nTrusted;
                    else
                        fail ();
                }
            }

            i = 0;
            for (auto const& val : list2)
            {
                BEAST_EXPECT(validators->listed (val.masterPublicKey));
                BEAST_EXPECT(validators->listed (val.signingPublicKey));
                if (validators->trusted (val.signingPublicKey))
                {
                    if (i++ < activePerList)
                        ++nTrusted;
                    else
                        fail ();
                }
            }

            BEAST_EXPECT(nTrusted == 16);
        }
        {
            // fetch list with new publisher key updated by manifest
            auto const newSigningKeys = randomKeyPair(KeyType::secp256k1);

            auto const newManifest = make_Manifest (
                2, newSigningKeys.first, masterSecret1, masterPublic1);

            http_sync_server server2(
                ep2, serverThread.get_io_service(),
                newSigningKeys, newManifest, sequence, version, list2);

            ManifestCache manifests;
            auto validators = std::make_unique <ValidatorList> (
                manifests, thread.get_io_service (), beast::Journal ());

            BEAST_EXPECT(validators->load (
                emptyLocalKey, emptyCfgValidators, cfgSites2,
                cfgKeys2, emptyCfgManifest));
            BEAST_EXPECT(validators->trustedPublisher (masterPublic1));

            auto tries = 0;
            while (validators->getFetchedSitesCount () < 2 && tries++ < 10)
                std::this_thread::sleep_for (std::chrono::milliseconds (10));

            validators->update (activeValidators);
            BEAST_EXPECT(validators->quorum () == 12);

            for (auto const& val : list1)
            {
                BEAST_EXPECT(! validators->listed (val.masterPublicKey));
                BEAST_EXPECT(! validators->listed (val.signingPublicKey));
                BEAST_EXPECT(! validators->trusted (val.signingPublicKey));
            }

            auto i = 0;
            for (auto const& val : list2)
            {
                BEAST_EXPECT(validators->listed (val.masterPublicKey));
                BEAST_EXPECT(validators->listed (val.signingPublicKey));
                BEAST_EXPECT(
                    validators->trusted (val.signingPublicKey) ==
                    (i++ < activePerList));
            }
        }
        {
            // do not fetch list with revoked publisher key
            auto const irrelevantKeys = randomKeyPair(KeyType::secp256k1);
            auto maxManifest = make_Manifest (
                std::numeric_limits<std::uint32_t>::max (),
                irrelevantKeys.first, masterSecret2, masterPublic2);

            http_sync_server server2(
                ep2, serverThread.get_io_service(),
                irrelevantKeys, maxManifest, sequence, version, list2);

            ManifestCache manifests;
            auto validators = std::make_unique <ValidatorList> (
                manifests, thread.get_io_service (), beast::Journal ());

            BEAST_EXPECT(validators->load (
                emptyLocalKey, emptyCfgValidators, cfgSites2,
                cfgKeys2, emptyCfgManifest));
            BEAST_EXPECT(validators->trustedPublisher (masterPublic1));
            BEAST_EXPECT(validators->trustedPublisher (masterPublic2));

            auto tries = 0;
            while (validators->getFetchedSitesCount () < 2 && tries++ < 10)
                std::this_thread::sleep_for (std::chrono::milliseconds (10));

            validators->update (activeValidators);
            BEAST_EXPECT(! validators->trustedPublisher (masterPublic2));
            BEAST_EXPECT(validators->quorum () == 12);

            auto i = 0;
            for (auto const& val : list1)
            {
                BEAST_EXPECT(validators->listed (val.masterPublicKey));
                BEAST_EXPECT(validators->listed (val.signingPublicKey));
                BEAST_EXPECT(
                    validators->trusted (val.signingPublicKey) ==
                    (i++ < activePerList));
            }

            for (auto const& val : list2)
            {
                BEAST_EXPECT(! validators->listed (val.masterPublicKey));
                BEAST_EXPECT(! validators->listed (val.signingPublicKey));
                BEAST_EXPECT(! validators->trusted (val.signingPublicKey));
            }
        }
        {
            // fetched list is removed due to revoked publisher key
            http_sync_server server2(
                ep2, serverThread.get_io_service(),
                signingKeys2, manifest2, sequence, version, list2);

            ManifestCache manifests;
            auto validators = std::make_unique <ValidatorList> (
                manifests, thread.get_io_service (), beast::Journal ());

            BEAST_EXPECT(validators->load (
                emptyLocalKey, emptyCfgValidators, cfgSites2,
                cfgKeys2, emptyCfgManifest));
            BEAST_EXPECT(validators->trustedPublisher (masterPublic1));
            BEAST_EXPECT(validators->trustedPublisher (masterPublic2));

            auto tries = 0;
            while (validators->getFetchedSitesCount () < 2 && tries++ < 10)
                std::this_thread::sleep_for (std::chrono::milliseconds (10));

            auto maxManifest = make_Manifest (
                masterPublic2, masterSecret2, randomNode(),
                std::numeric_limits<std::uint32_t>::max ());

            BEAST_EXPECT(ManifestDisposition::accepted ==
                manifests.applyManifest (std::move (maxManifest), *validators));
            BEAST_EXPECT(! validators->trustedPublisher (masterPublic2));

            validators->update (activeValidators);
            BEAST_EXPECT(validators->quorum () == 12);

            auto i = 0;
            for (auto const& val : list1)
            {
                BEAST_EXPECT(validators->listed (val.masterPublicKey));
                BEAST_EXPECT(validators->listed (val.signingPublicKey));
                BEAST_EXPECT(
                    validators->trusted (val.signingPublicKey) ==
                    (i++ < activePerList));
            }

            for (auto const& val : list2)
            {
                BEAST_EXPECT(! validators->listed (val.masterPublicKey));
                BEAST_EXPECT(! validators->listed (val.signingPublicKey));
                BEAST_EXPECT(! validators->trusted (val.signingPublicKey));
            }
        }
        {
            // local validator key is always trusted
            ManifestCache manifests;
            auto validators = std::make_unique <ValidatorList> (
                manifests, thread.get_io_service (), beast::Journal ());

            auto const localSigningKey = randomNode();
            activeValidators[calcNodeID (localSigningKey)];

            BEAST_EXPECT(validators->load (
                localSigningKey, emptyCfgValidators, cfgSites1,
                cfgKeys1, emptyCfgManifest));
            BEAST_EXPECT(validators->trustedPublisher (masterPublic1));

            auto tries = 0;
            while (validators->getFetchedSitesCount () == 0 && tries++ < 10)
                std::this_thread::sleep_for (std::chrono::milliseconds (10));

            validators->update (activeValidators);
            BEAST_EXPECT(validators->quorum () == 13);

            BEAST_EXPECT(validators->listed (localSigningKey));
            BEAST_EXPECT(validators->trusted (localSigningKey));

            auto i = 0;
            for (auto const& val : list1)
            {
                BEAST_EXPECT(validators->listed (val.masterPublicKey));
                BEAST_EXPECT(validators->listed (val.signingPublicKey));
                BEAST_EXPECT(
                    validators->trusted (val.signingPublicKey) ==
                    (i++ < activePerList));
            }
        }
        {
            // local validator key with manifest is always trusted
            http_sync_server server2(
                ep2, serverThread.get_io_service(),
                signingKeys2, manifest2, sequence, version, list2);

            ManifestCache manifests;
            auto validators = std::make_unique <ValidatorList> (
                manifests, thread.get_io_service (), beast::Journal ());

            auto const localSigningKey = randomNode();
            auto const localMasterSecret = randomSecretKey();
            auto const localMasterPublic = derivePublicKey(
                KeyType::ed25519, localMasterSecret);

            std::vector<std::string> const cfgManifest ({
                make_Manifest (
                    1, localSigningKey, localMasterSecret, localMasterPublic)});

            activeValidators[calcNodeID (localSigningKey)];

            BEAST_EXPECT(validators->load (
                localSigningKey, emptyCfgValidators, cfgSites2,
                cfgKeys2, cfgManifest));
            BEAST_EXPECT(validators->trustedPublisher (masterPublic1));
            BEAST_EXPECT(validators->trustedPublisher (masterPublic2));

            auto tries = 0;
            while (validators->getFetchedSitesCount () < 2 && tries++ < 10)
                std::this_thread::sleep_for (std::chrono::milliseconds (10));

            validators->update (activeValidators);
            BEAST_EXPECT(validators->quorum () == 14);

            BEAST_EXPECT(validators->listed (localSigningKey));
            BEAST_EXPECT(validators->listed (localMasterPublic));
            BEAST_EXPECT(validators->trusted (localSigningKey));
            BEAST_EXPECT(validators->trusted (localMasterPublic));

            auto i = 0;
            auto nTrusted = 0;
            for (auto const& val : list1)
            {
                BEAST_EXPECT(validators->listed (val.masterPublicKey));
                BEAST_EXPECT(validators->listed (val.signingPublicKey));
                if (validators->trusted (val.signingPublicKey))
                {
                    if (i++ < activePerList)
                        ++nTrusted;
                    else
                        fail ();
                }
            }

            i = 0;
            for (auto const& val : list2)
            {
                BEAST_EXPECT(validators->listed (val.masterPublicKey));
                BEAST_EXPECT(validators->listed (val.signingPublicKey));
                if (validators->trusted (val.signingPublicKey))
                {
                    if (i++ < activePerList)
                        ++nTrusted;
                    else
                        fail ();
                }
            }
            BEAST_EXPECT(nTrusted == 15);
        }
        {
            // fetch list in addition to list from config
            ManifestCache manifests;
            auto validators = std::make_unique <ValidatorList> (
                manifests, thread.get_io_service (), beast::Journal ());

            std::vector<std::string> cfgValidators;
            for (auto const& val : list2)
            {
                cfgValidators.push_back (toBase58 (
                    TokenType::TOKEN_NODE_PUBLIC, val.signingPublicKey));
            }

            BEAST_EXPECT(validators->load (
                emptyLocalKey, cfgValidators, cfgSites1,
                cfgKeys1, emptyCfgValidators));
            BEAST_EXPECT(validators->trustedPublisher (masterPublic1));

            auto tries = 0;
            while (validators->getFetchedSitesCount () == 0 && tries++ < 10)
                std::this_thread::sleep_for (std::chrono::milliseconds (10));

            validators->update (activeValidators);

            BEAST_EXPECT(validators->quorum () == 13);

            auto i = 0;
            auto nTrusted = 0;
            for (auto const& val : list1)
            {
                BEAST_EXPECT(validators->listed (val.masterPublicKey));
                BEAST_EXPECT(validators->listed (val.signingPublicKey));
                if (validators->trusted (val.signingPublicKey))
                {
                    if (i++ < activePerList)
                        ++nTrusted;
                    else
                        fail ();
                }
            }

            i = 0;
            for (auto const& val : list2)
            {
                BEAST_EXPECT(validators->listed (val.signingPublicKey));
                if (validators->trusted (val.signingPublicKey))
                {
                    if (i++ < activePerList)
                        ++nTrusted;
                    else
                        fail ();
                }
            }
            BEAST_EXPECT(nTrusted == 16);
        }
    }

    void
    testUpdate ()
    {
        testcase ("Update");

        PublicKey emptyLocalKey;
        ManifestCache manifests;
        TestThread thread;
        auto validators = std::make_unique <ValidatorList> (
            manifests, thread.get_io_service (), beast::Journal ());

        std::vector<std::string> cfgSites;
        std::vector<std::string> cfgKeys;
        std::vector<std::string> emptyCfgManifest;
        ValidationSet activeValidators;

        {
            std::vector<std::string> cfgValidators;
            cfgValidators.reserve(20);

            while (cfgValidators.size () != 20)
            {
                auto const valKey = randomNode();
                cfgValidators.push_back (toBase58(
                    TokenType::TOKEN_NODE_PUBLIC,
                    valKey));
                if (cfgValidators.size () <= 10)
                    activeValidators[calcNodeID(valKey)];
            }

            BEAST_EXPECT(validators->load (
                emptyLocalKey, cfgValidators, cfgSites,
                cfgKeys, emptyCfgManifest));

            // update should make all available configured validators trusted
            validators->update (activeValidators);
            BEAST_EXPECT(validators->quorum () == 7);
            auto i = 0;
            for (auto const& val : cfgValidators)
            {
                if (auto const valKey = parseBase58<PublicKey>(
                    TokenType::TOKEN_NODE_PUBLIC, val))
                {
                    BEAST_EXPECT(validators->listed (*valKey));
                    if (i++ < 10)
                        BEAST_EXPECT(validators->trusted (*valKey));
                    else
                        BEAST_EXPECT(!validators->trusted (*valKey));
                }
                else
                    fail ();
            }
        }
        {
            // update with manifests
            auto const seed  = randomSecretKey();
            auto const masterKey = derivePublicKey(KeyType::ed25519, seed);

            std::vector<std::string> cfgValidators ({
                toBase58(
                    TokenType::TOKEN_NODE_PUBLIC,
                    masterKey)});

            BEAST_EXPECT(validators->load (
                emptyLocalKey, cfgValidators, cfgSites,
                cfgKeys, emptyCfgManifest));

            auto const signingKey = randomKeyPair(KeyType::secp256k1).first;
            activeValidators[calcNodeID(signingKey)];

            // update should not trust ephemeral signing key if there
            // is no manifest
            // update does not trust master public key because it is
            // not included in list of active validator signing keys
            validators->update (activeValidators);
            BEAST_EXPECT(validators->listed (masterKey));
            BEAST_EXPECT(!validators->trusted (masterKey));
            BEAST_EXPECT(!validators->listed (signingKey));
            BEAST_EXPECT(!validators->trusted (signingKey));

            // update should trust the ephemeral signing key
            // from the applied manifest
            auto m = make_Manifest (
                masterKey, seed, signingKey, 0);

            BEAST_EXPECT(manifests.applyManifest(std::move (m), *validators) ==
                    ManifestDisposition::accepted);
            validators->update (activeValidators);
            BEAST_EXPECT(validators->quorum () == 9);
            BEAST_EXPECT(validators->listed (masterKey));
            BEAST_EXPECT(validators->trusted (masterKey));
            BEAST_EXPECT(validators->listed (signingKey));
            BEAST_EXPECT(validators->trusted (signingKey));

            // update should only trust the ephemeral signing key
            // from the newest applied manifest
            auto const signingKey1 = randomKeyPair(KeyType::secp256k1).first;
            activeValidators[calcNodeID(signingKey1)];
            auto m1 = make_Manifest (
                masterKey, seed, signingKey1, 1);

            BEAST_EXPECT(manifests.applyManifest(std::move (m1), *validators) ==
                    ManifestDisposition::accepted);
            validators->update (activeValidators);
            BEAST_EXPECT(validators->quorum () == 9);
            BEAST_EXPECT(validators->listed (masterKey));
            BEAST_EXPECT(validators->trusted (masterKey));
            BEAST_EXPECT(validators->listed (signingKey1));
            BEAST_EXPECT(validators->trusted (signingKey1));
            BEAST_EXPECT(!validators->listed (signingKey));
            BEAST_EXPECT(!validators->trusted (signingKey));

            // update should not trust keys from revoked master public key
            auto const signingKeyMax = randomKeyPair(KeyType::secp256k1).first;
            activeValidators[calcNodeID(signingKeyMax)];
            auto mMax = make_Manifest (
                masterKey, seed, signingKeyMax,
                std::numeric_limits<std::uint32_t>::max ());

            BEAST_EXPECT(mMax.revoked ());
            BEAST_EXPECT(manifests.applyManifest(std::move (mMax), *validators) ==
                    ManifestDisposition::accepted);
            BEAST_EXPECT(!manifests.getSigningKey (masterKey));
            BEAST_EXPECT(manifests.revoked (masterKey));
            validators->update (activeValidators);
            BEAST_EXPECT(validators->quorum () == 8);
            BEAST_EXPECT(validators->listed (masterKey));
            BEAST_EXPECT(!validators->trusted (masterKey));
            BEAST_EXPECT(!validators->listed (signingKeyMax));
            BEAST_EXPECT(!validators->trusted (signingKeyMax));
            BEAST_EXPECT(!validators->listed (signingKey1));
            BEAST_EXPECT(!validators->trusted (signingKey1));
            BEAST_EXPECT(!validators->listed (signingKey));
            BEAST_EXPECT(!validators->trusted (signingKey));
        }
    }

    void
    testCalcQuorum ()
    {
        testcase ("Calculate Quorum");

        // Quorum cannot be lower than 32% (80% * 40%) of the number of
        // listed validators
        BEAST_EXPECT(2  == ValidatorList::calcQuorum (0,   5));
        BEAST_EXPECT(2  == ValidatorList::calcQuorum (1,   5));
        BEAST_EXPECT(2  == ValidatorList::calcQuorum (2,   5));
        BEAST_EXPECT(4  == ValidatorList::calcQuorum (0,  10));
        BEAST_EXPECT(4  == ValidatorList::calcQuorum (1,  10));
        BEAST_EXPECT(4  == ValidatorList::calcQuorum (2,  10));
        BEAST_EXPECT(4  == ValidatorList::calcQuorum (3,  10));
        BEAST_EXPECT(4  == ValidatorList::calcQuorum (4,  10));
        BEAST_EXPECT(7  == ValidatorList::calcQuorum (5,  20));
        BEAST_EXPECT(7  == ValidatorList::calcQuorum (6,  20));
        BEAST_EXPECT(7  == ValidatorList::calcQuorum (7,  20));
        BEAST_EXPECT(7  == ValidatorList::calcQuorum (8,  20));

        // Quorum is computed using Byzantine fault tolerance (3f+1)
        // with 10 or fewer trusted validators
        BEAST_EXPECT(3  == ValidatorList::calcQuorum (3,   5));
        BEAST_EXPECT(3  == ValidatorList::calcQuorum (4,   5));
        BEAST_EXPECT(4  == ValidatorList::calcQuorum (5,   5));

        BEAST_EXPECT(4  == ValidatorList::calcQuorum (5,  10));
        BEAST_EXPECT(5  == ValidatorList::calcQuorum (6,  10));
        BEAST_EXPECT(5  == ValidatorList::calcQuorum (7,  10));
        BEAST_EXPECT(6  == ValidatorList::calcQuorum (8,  10));
        BEAST_EXPECT(7  == ValidatorList::calcQuorum (9,  10));
        BEAST_EXPECT(7  == ValidatorList::calcQuorum (10, 10));

        // Quorum is 80% of the number of trusted validators
        // for more than 10 trusted validators
        BEAST_EXPECT(9  == ValidatorList::calcQuorum (11, 20));
        BEAST_EXPECT(10 == ValidatorList::calcQuorum (12, 20));
        BEAST_EXPECT(11 == ValidatorList::calcQuorum (13, 20));
        BEAST_EXPECT(12 == ValidatorList::calcQuorum (14, 20));
        BEAST_EXPECT(12 == ValidatorList::calcQuorum (15, 20));
        BEAST_EXPECT(13 == ValidatorList::calcQuorum (16, 20));
        BEAST_EXPECT(14 == ValidatorList::calcQuorum (17, 20));
        BEAST_EXPECT(15 == ValidatorList::calcQuorum (18, 20));
        BEAST_EXPECT(16 == ValidatorList::calcQuorum (19, 20));
        BEAST_EXPECT(16 == ValidatorList::calcQuorum (20, 20));
        BEAST_EXPECT(80 == ValidatorList::calcQuorum (100, 100));
        BEAST_EXPECT(800 == ValidatorList::calcQuorum (1000, 1000));

        // Allow the number of trusted validators to exceed
        // the number of listed validators
        BEAST_EXPECT(7 == ValidatorList::calcQuorum (10,  5));
    }

public:
    void
    run() override
    {
log << "test flush\n" << std::flush;
        testConfigLoad ();
        testFetchList ();
        testUpdate ();
        testCalcQuorum ();
    }
};

BEAST_DEFINE_TESTSUITE(ValidatorList, app, ripple);

} // tests
} // ripple
