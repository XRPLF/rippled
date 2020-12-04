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

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/misc/Manifest.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/core/TimeKeeper.h>
#include <ripple/overlay/Compression.h>
#include <ripple/overlay/Message.h>
#include <ripple/overlay/impl/Handshake.h>
#include <ripple/overlay/impl/ProtocolMessage.h>
#include <ripple/overlay/impl/ZeroCopyStream.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/jss.h>
#include <ripple/shamap/SHAMapNodeID.h>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/endian/conversion.hpp>
#include <algorithm>
#include <ripple.pb.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>

namespace ripple {

namespace test {

using namespace ripple::test;
using namespace ripple::test::jtx;

static uint256
ledgerHash(LedgerInfo const& info)
{
    return ripple::sha512Half(
        HashPrefix::ledgerMaster,
        std::uint32_t(info.seq),
        std::uint64_t(info.drops.drops()),
        info.parentHash,
        info.txHash,
        info.accountHash,
        std::uint32_t(info.parentCloseTime.time_since_epoch().count()),
        std::uint32_t(info.closeTime.time_since_epoch().count()),
        std::uint8_t(info.closeTimeResolution.count()),
        std::uint8_t(info.closeFlags));
}

class compression_test : public beast::unit_test::suite
{
    using Compressed = compression::Compressed;
    using Algorithm = compression::Algorithm;

public:
    compression_test()
    {
    }

    template <typename T>
    void
    doTest(
        std::shared_ptr<T> proto,
        protocol::MessageType mt,
        uint16_t nbuffers,
        std::string msg)
    {
        testcase("Compress/Decompress: " + msg);

        Message m(*proto, mt);

        auto& buffer = m.getBuffer(Compressed::On);

        boost::beast::multi_buffer buffers;

        // simulate multi-buffer
        auto sz = buffer.size() / nbuffers;
        for (int i = 0; i < nbuffers; i++)
        {
            auto start = buffer.begin() + sz * i;
            auto end = i < nbuffers - 1 ? (buffer.begin() + sz * (i + 1))
                                        : buffer.end();
            std::vector<std::uint8_t> slice(start, end);
            buffers.commit(boost::asio::buffer_copy(
                buffers.prepare(slice.size()), boost::asio::buffer(slice)));
        }

        boost::system::error_code ec;
        auto header = ripple::detail::parseMessageHeader(
            ec, buffers.data(), buffer.size());

        BEAST_EXPECT(header);

        if (!header || header->algorithm == Algorithm::None)
            return;

        std::vector<std::uint8_t> decompressed;
        decompressed.resize(header->uncompressed_size);

        BEAST_EXPECT(
            header->payload_wire_size == buffer.size() - header->header_size);

        ZeroCopyInputStream stream(buffers.data());
        stream.Skip(header->header_size);

        auto decompressedSize = ripple::compression::decompress(
            stream,
            header->payload_wire_size,
            decompressed.data(),
            header->uncompressed_size);
        BEAST_EXPECT(decompressedSize == header->uncompressed_size);
        auto const proto1 = std::make_shared<T>();

        BEAST_EXPECT(
            proto1->ParseFromArray(decompressed.data(), decompressedSize));
        auto uncompressed = m.getBuffer(Compressed::Off);
        BEAST_EXPECT(std::equal(
            uncompressed.begin() + ripple::compression::headerBytes,
            uncompressed.end(),
            decompressed.begin()));
    }

    std::shared_ptr<protocol::TMManifests>
    buildManifests(int n)
    {
        auto manifests = std::make_shared<protocol::TMManifests>();
        manifests->mutable_list()->Reserve(n);
        for (int i = 0; i < n; i++)
        {
            auto master = randomKeyPair(KeyType::ed25519);
            auto signing = randomKeyPair(KeyType::ed25519);
            STObject st(sfGeneric);
            st[sfSequence] = i;
            st[sfPublicKey] = std::get<0>(master);
            st[sfSigningPubKey] = std::get<0>(signing);
            st[sfDomain] = makeSlice(
                std::string("example") + std::to_string(i) +
                std::string(".com"));
            sign(
                st,
                HashPrefix::manifest,
                KeyType::ed25519,
                std::get<1>(master),
                sfMasterSignature);
            sign(
                st,
                HashPrefix::manifest,
                KeyType::ed25519,
                std::get<1>(signing));
            Serializer s;
            st.add(s);
            auto* manifest = manifests->add_list();
            manifest->set_stobject(s.data(), s.size());
        }
        return manifests;
    }

    std::shared_ptr<protocol::TMEndpoints>
    buildEndpoints(int n)
    {
        auto endpoints = std::make_shared<protocol::TMEndpoints>();
        endpoints->mutable_endpoints_v2()->Reserve(n);
        for (int i = 0; i < n; i++)
        {
            auto ep = endpoints->add_endpoints_v2();
            ep->set_endpoint(std::string("10.0.1.") + std::to_string(i));
            ep->set_hops(i);
        }
        endpoints->set_version(2);

        return endpoints;
    }

    std::shared_ptr<protocol::TMTransaction>
    buildTransaction(Logs& logs)
    {
        Env env(*this, envconfig());
        int fund = 10000;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(fund), "alice", "bob");
        env.trust(bob["USD"](fund), alice);
        env.close();

        auto toBinary = [this](std::string const& text) {
            auto blob = strUnHex(text);
            BEAST_EXPECT(blob);
            return std::string{
                reinterpret_cast<char const*>(blob->data()), blob->size()};
        };

        std::string usdTxBlob = "";
        auto wsc = makeWSClient(env.app().config());
        {
            Json::Value jrequestUsd;
            jrequestUsd[jss::secret] = toBase58(generateSeed("bob"));
            jrequestUsd[jss::tx_json] =
                pay("bob", "alice", bob["USD"](fund / 2));
            Json::Value jreply_usd = wsc->invoke("sign", jrequestUsd);

            usdTxBlob =
                toBinary(jreply_usd[jss::result][jss::tx_blob].asString());
        }

        auto transaction = std::make_shared<protocol::TMTransaction>();
        transaction->set_rawtransaction(usdTxBlob);
        transaction->set_status(protocol::tsNEW);
        auto tk = make_TimeKeeper(logs.journal("TimeKeeper"));
        transaction->set_receivetimestamp(tk->now().time_since_epoch().count());
        transaction->set_deferred(true);

        return transaction;
    }

    std::shared_ptr<protocol::TMGetLedger>
    buildGetLedger()
    {
        auto getLedger = std::make_shared<protocol::TMGetLedger>();
        getLedger->set_itype(protocol::liTS_CANDIDATE);
        getLedger->set_ltype(protocol::TMLedgerType::ltACCEPTED);
        uint256 const hash(ripple::sha512Half(123456789));
        getLedger->set_ledgerhash(hash.begin(), hash.size());
        getLedger->set_ledgerseq(123456789);
        ripple::SHAMapNodeID sha(17, hash);
        getLedger->add_nodeids(sha.getRawString());
        getLedger->set_requestcookie(123456789);
        getLedger->set_querytype(protocol::qtINDIRECT);
        getLedger->set_querydepth(3);
        return getLedger;
    }

    std::shared_ptr<protocol::TMLedgerData>
    buildLedgerData(uint32_t n, Logs& logs)
    {
        auto ledgerData = std::make_shared<protocol::TMLedgerData>();
        uint256 const hash(ripple::sha512Half(12356789));
        ledgerData->set_ledgerhash(hash.data(), hash.size());
        ledgerData->set_ledgerseq(123456789);
        ledgerData->set_type(protocol::TMLedgerInfoType::liAS_NODE);
        ledgerData->set_requestcookie(123456789);
        ledgerData->set_error(protocol::TMReplyError::reNO_LEDGER);
        ledgerData->mutable_nodes()->Reserve(n);
        uint256 parentHash(0);
        for (int i = 0; i < n; i++)
        {
            LedgerInfo info;
            auto tk = make_TimeKeeper(logs.journal("TimeKeeper"));
            info.seq = i;
            info.parentCloseTime = tk->now();
            info.hash = ripple::sha512Half(i);
            info.txHash = ripple::sha512Half(i + 1);
            info.accountHash = ripple::sha512Half(i + 2);
            info.parentHash = parentHash;
            info.drops = XRPAmount(10);
            info.closeTimeResolution = tk->now().time_since_epoch();
            info.closeTime = tk->now();
            parentHash = ledgerHash(info);
            Serializer nData;
            ripple::addRaw(info, nData);
            ledgerData->add_nodes()->set_nodedata(
                nData.getDataPtr(), nData.getLength());
        }

        return ledgerData;
    }

    std::shared_ptr<protocol::TMGetObjectByHash>
    buildGetObjectByHash()
    {
        auto getObject = std::make_shared<protocol::TMGetObjectByHash>();

        getObject->set_type(protocol::TMGetObjectByHash_ObjectType::
                                TMGetObjectByHash_ObjectType_otTRANSACTION);
        getObject->set_query(true);
        getObject->set_seq(123456789);
        uint256 hash(ripple::sha512Half(123456789));
        getObject->set_ledgerhash(hash.data(), hash.size());
        getObject->set_fat(true);
        for (int i = 0; i < 100; i++)
        {
            uint256 hash(ripple::sha512Half(i));
            auto object = getObject->add_objects();
            object->set_hash(hash.data(), hash.size());
            ripple::SHAMapNodeID sha(i % 55, hash);
            object->set_nodeid(sha.getRawString());
            object->set_index("");
            object->set_data("");
            object->set_ledgerseq(i);
        }
        return getObject;
    }

    std::shared_ptr<protocol::TMValidatorList>
    buildValidatorList()
    {
        auto list = std::make_shared<protocol::TMValidatorList>();

        auto master = randomKeyPair(KeyType::ed25519);
        auto signing = randomKeyPair(KeyType::ed25519);
        STObject st(sfGeneric);
        st[sfSequence] = 0;
        st[sfPublicKey] = std::get<0>(master);
        st[sfSigningPubKey] = std::get<0>(signing);
        st[sfDomain] = makeSlice(std::string("example.com"));
        sign(
            st,
            HashPrefix::manifest,
            KeyType::ed25519,
            std::get<1>(master),
            sfMasterSignature);
        sign(st, HashPrefix::manifest, KeyType::ed25519, std::get<1>(signing));
        Serializer s;
        st.add(s);
        list->set_manifest(s.data(), s.size());
        list->set_version(3);
        STObject signature(sfSignature);
        ripple::sign(
            st, HashPrefix::manifest, KeyType::ed25519, std::get<1>(signing));
        Serializer s1;
        st.add(s1);
        list->set_signature(s1.data(), s1.size());
        list->set_blob(strHex(s.getString()));
        return list;
    }

    void
    testProtocol()
    {
        auto thresh = beast::severities::Severity::kInfo;
        auto logs = std::make_unique<Logs>(thresh);

        protocol::TMManifests manifests;
        protocol::TMEndpoints endpoints;
        protocol::TMTransaction transaction;
        protocol::TMGetLedger get_ledger;
        protocol::TMLedgerData ledger_data;
        protocol::TMGetObjectByHash get_object;
        protocol::TMValidatorList validator_list;

        // 4.5KB
        doTest(buildManifests(20), protocol::mtMANIFESTS, 4, "TMManifests20");
        // 22KB
        doTest(buildManifests(100), protocol::mtMANIFESTS, 4, "TMManifests100");
        // 131B
        doTest(buildEndpoints(10), protocol::mtENDPOINTS, 4, "TMEndpoints10");
        // 1.3KB
        doTest(buildEndpoints(100), protocol::mtENDPOINTS, 4, "TMEndpoints100");
        // 242B
        doTest(
            buildTransaction(*logs),
            protocol::mtTRANSACTION,
            1,
            "TMTransaction");
        // 87B
        doTest(buildGetLedger(), protocol::mtGET_LEDGER, 1, "TMGetLedger");
        // 61KB
        doTest(
            buildLedgerData(500, *logs),
            protocol::mtLEDGER_DATA,
            10,
            "TMLedgerData500");
        // 122 KB
        doTest(
            buildLedgerData(1000, *logs),
            protocol::mtLEDGER_DATA,
            20,
            "TMLedgerData1000");
        // 1.2MB
        doTest(
            buildLedgerData(10000, *logs),
            protocol::mtLEDGER_DATA,
            50,
            "TMLedgerData10000");
        // 12MB
        doTest(
            buildLedgerData(100000, *logs),
            protocol::mtLEDGER_DATA,
            100,
            "TMLedgerData100000");
        // 61MB
        doTest(
            buildLedgerData(500000, *logs),
            protocol::mtLEDGER_DATA,
            100,
            "TMLedgerData500000");
        // 7.7KB
        doTest(
            buildGetObjectByHash(),
            protocol::mtGET_OBJECTS,
            4,
            "TMGetObjectByHash");
        // 895B
        doTest(
            buildValidatorList(),
            protocol::mtVALIDATORLIST,
            4,
            "TMValidatorList");
    }

    void
    testHandshake()
    {
        testcase("Handshake");
        auto getEnv = [&](bool enable) {
            Config c;
            std::stringstream str;
            str << "[reduce_relay]\n"
                << "vp_enable=1\n"
                << "vp_squelch=1\n"
                << "[compression]\n"
                << enable << "\n";
            c.loadFromString(str.str());
            auto env = std::make_shared<jtx::Env>(*this);
            env->app().config().COMPRESSION = c.COMPRESSION;
            env->app().config().VP_REDUCE_RELAY_ENABLE =
                c.VP_REDUCE_RELAY_ENABLE;
            env->app().config().VP_REDUCE_RELAY_SQUELCH =
                c.VP_REDUCE_RELAY_SQUELCH;
            return env;
        };
        auto handshake = [&](int outboundEnable, int inboundEnable) {
            beast::IP::Address addr =
                boost::asio::ip::address::from_string("172.1.1.100");

            auto env = getEnv(outboundEnable);
            auto request = ripple::makeRequest(
                true,
                env->app().config().COMPRESSION,
                env->app().config().VP_REDUCE_RELAY_ENABLE);
            http_request_type http_request;
            http_request.version(request.version());
            http_request.base() = request.base();
            // feature enabled on the peer's connection only if both sides are
            // enabled
            auto const peerEnabled = inboundEnable && outboundEnable;
            // inbound is enabled if the request's header has the feature
            // enabled and the peer's configuration is enabled
            auto const inboundEnabled = peerFeatureEnabled(
                http_request, FEATURE_COMPR, "lz4", inboundEnable);
            BEAST_EXPECT(!(peerEnabled ^ inboundEnabled));

            env.reset();
            env = getEnv(inboundEnable);
            auto http_resp = ripple::makeResponse(
                true,
                http_request,
                addr,
                addr,
                uint256{1},
                1,
                {1, 0},
                env->app());
            // outbound is enabled if the response's header has the feature
            // enabled and the peer's configuration is enabled
            auto const outboundEnabled = peerFeatureEnabled(
                http_resp, FEATURE_COMPR, "lz4", outboundEnable);
            BEAST_EXPECT(!(peerEnabled ^ outboundEnabled));
        };
        handshake(1, 1);
        handshake(1, 0);
        handshake(0, 1);
        handshake(0, 0);
    }

    void
    run() override
    {
        testProtocol();
        testHandshake();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(compression, ripple_data, ripple);

}  // namespace test
}  // namespace ripple
