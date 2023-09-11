//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2017 Ripple Labs Inc.

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
#ifndef RIPPLE_TEST_TRUSTED_PUBLISHER_SERVER_H_INCLUDED
#define RIPPLE_TEST_TRUSTED_PUBLISHER_SERVER_H_INCLUDED

#include <ripple/basics/base64.h>
#include <ripple/basics/random.h>
#include <ripple/basics/strHex.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Sign.h>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/lexical_cast.hpp>
#include <test/jtx/envconfig.h>

#include <memory>
#include <thread>

namespace ripple {
namespace test {

class TrustedPublisherServer
    : public std::enable_shared_from_this<TrustedPublisherServer>
{
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

    using req_type =
        boost::beast::http::request<boost::beast::http::string_body>;
    using resp_type =
        boost::beast::http::response<boost::beast::http::string_body>;
    using error_code = boost::system::error_code;

    socket_type sock_;
    endpoint_type ep_;
    boost::asio::ip::tcp::acceptor acceptor_;
    // Generates a version 1 validator list, using the int parameter as the
    // actual version.
    std::function<std::string(int)> getList_;
    // Generates a version 2 validator list, using the int parameter as the
    // actual version.
    std::function<std::string(int)> getList2_;

    // The SSL context is required, and holds certificates
    bool useSSL_;
    boost::asio::ssl::context sslCtx_{boost::asio::ssl::context::tlsv12};

    SecretKey publisherSecret_;
    PublicKey publisherPublic_;

    // Load a signed certificate into the ssl context, and configure
    // the context for use with a server.
    inline void
    load_server_certificate()
    {
        sslCtx_.set_password_callback(
            [](std::size_t, boost::asio::ssl::context_base::password_purpose) {
                return "test";
            });

        sslCtx_.set_options(
            boost::asio::ssl::context::default_workarounds |
            boost::asio::ssl::context::no_sslv2 |
            boost::asio::ssl::context::single_dh_use);

        sslCtx_.use_certificate_chain(
            boost::asio::buffer(cert().data(), cert().size()));

        sslCtx_.use_private_key(
            boost::asio::buffer(key().data(), key().size()),
            boost::asio::ssl::context::file_format::pem);

        sslCtx_.use_tmp_dh(boost::asio::buffer(dh().data(), dh().size()));
    }

    struct BlobInfo
    {
        BlobInfo(std::string b, std::string s) : blob(b), signature(s)
        {
        }

        // base-64 encoded JSON containing the validator list.
        std::string blob;
        // hex-encoded signature of the blob using the publisher's signing key
        std::string signature;
    };

public:
    struct Validator
    {
        PublicKey masterPublic;
        PublicKey signingPublic;
        std::string manifest;
    };

    static std::string
    makeManifestString(
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
        sign(
            st,
            HashPrefix::manifest,
            *publicKeyType(pk),
            sk,
            sfMasterSignature);

        Serializer s;
        st.add(s);

        return base64_encode(
            std::string(static_cast<char const*>(s.data()), s.size()));
    }

    static Validator
    randomValidator()
    {
        auto const secret = randomSecretKey();
        auto const masterPublic = derivePublicKey(KeyType::ed25519, secret);
        auto const signingKeys = randomKeyPair(KeyType::secp256k1);
        return {
            masterPublic,
            signingKeys.first,
            makeManifestString(
                masterPublic,
                secret,
                signingKeys.first,
                signingKeys.second,
                1)};
    }

    // TrustedPublisherServer must be accessed through a shared_ptr.
    // This constructor is only public so std::make_shared has access.
    // The function`make_TrustedPublisherServer` should be used to create
    // instances.
    // The `futures` member is expected to be structured as
    // effective / expiration time point pairs for use in version 2 UNLs
    TrustedPublisherServer(
        boost::asio::io_context& ioc,
        std::vector<Validator> const& validators,
        NetClock::time_point validUntil,
        std::vector<
            std::pair<NetClock::time_point, NetClock::time_point>> const&
            futures,
        bool useSSL = false,
        int version = 1,
        bool immediateStart = true,
        int sequence = 1)
        : sock_{ioc}
        , ep_{beast::IP::Address::from_string(
                  ripple::test::getEnvLocalhostAddr()),
              // 0 means let OS pick the port based on what's available
              0}
        , acceptor_{ioc}
        , useSSL_{useSSL}
        , publisherSecret_{randomSecretKey()}
        , publisherPublic_{derivePublicKey(KeyType::ed25519, publisherSecret_)}
    {
        auto const keys = randomKeyPair(KeyType::secp256k1);
        auto const manifest = makeManifestString(
            publisherPublic_, publisherSecret_, keys.first, keys.second, 1);

        std::vector<BlobInfo> blobInfo;
        blobInfo.reserve(futures.size() + 1);
        auto const [data, blob] = [&]() -> std::pair<std::string, std::string> {
            // Builds the validator list, then encodes it into a blob.
            std::string data = "{\"sequence\":" + std::to_string(sequence) +
                ",\"expiration\":" +
                std::to_string(validUntil.time_since_epoch().count()) +
                ",\"validators\":[";

            for (auto const& val : validators)
            {
                data += "{\"validation_public_key\":\"" +
                    strHex(val.masterPublic) + "\",\"manifest\":\"" +
                    val.manifest + "\"},";
            }
            data.pop_back();
            data += "]}";
            std::string blob = base64_encode(data);
            return std::make_pair(data, blob);
        }();
        auto const sig = strHex(sign(keys.first, keys.second, makeSlice(data)));
        blobInfo.emplace_back(blob, sig);
        getList_ = [blob = blob, sig, manifest, version](int interval) {
            // Build the contents of a version 1 format UNL file
            std::stringstream l;
            l << "{\"blob\":\"" << blob << "\""
              << ",\"signature\":\"" << sig << "\""
              << ",\"manifest\":\"" << manifest << "\""
              << ",\"refresh_interval\": " << interval
              << ",\"version\":" << version << '}';
            return l.str();
        };
        for (auto const& future : futures)
        {
            std::string data = "{\"sequence\":" + std::to_string(++sequence) +
                ",\"effective\":" +
                std::to_string(future.first.time_since_epoch().count()) +
                ",\"expiration\":" +
                std::to_string(future.second.time_since_epoch().count()) +
                ",\"validators\":[";

            // Use the same set of validators for simplicity
            for (auto const& val : validators)
            {
                data += "{\"validation_public_key\":\"" +
                    strHex(val.masterPublic) + "\",\"manifest\":\"" +
                    val.manifest + "\"},";
            }
            data.pop_back();
            data += "]}";
            std::string blob = base64_encode(data);
            auto const sig =
                strHex(sign(keys.first, keys.second, makeSlice(data)));
            blobInfo.emplace_back(blob, sig);
        }
        getList2_ = [blobInfo, manifest, version](int interval) {
            // Build the contents of a version 2 format UNL file
            // Use `version + 1` to get 2 for most tests, but have
            // a "bad" version number for tests that provide an override.
            std::stringstream l;
            for (auto const& info : blobInfo)
            {
                l << "{\"blob\":\"" << info.blob << "\""
                  << ",\"signature\":\"" << info.signature << "\"},";
            }
            std::string blobs = l.str();
            blobs.pop_back();
            l.str(std::string());
            l << "{\"blobs_v2\": [ " << blobs << "],\"manifest\":\"" << manifest
              << "\""
              << ",\"refresh_interval\": " << interval
              << ",\"version\":" << (version + 1) << '}';
            return l.str();
        };

        if (useSSL_)
        {
            // This holds the self-signed certificate used by the server
            load_server_certificate();
        }
    }

    void
    start()
    {
        error_code ec;
        acceptor_.open(ep_.protocol());
        acceptor_.set_option(
            boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
        acceptor_.bind(ep_);
        acceptor_.listen(boost::asio::socket_base::max_connections);
        acceptor_.async_accept(
            sock_,
            [wp = std::weak_ptr<TrustedPublisherServer>{shared_from_this()}](
                error_code ec) {
                if (auto p = wp.lock())
                {
                    p->on_accept(ec);
                }
            });
    }

    void
    stop()
    {
        error_code ec;
        acceptor_.close(ec);
        // TODO consider making this join
        // any running do_peer threads
    }

    ~TrustedPublisherServer()
    {
        stop();
    }

    endpoint_type
    local_endpoint() const
    {
        return acceptor_.local_endpoint();
    }

    PublicKey const&
    publisherPublic() const
    {
        return publisherPublic_;
    }

    /* CA/self-signed certs :
     *
     * The following three methods return certs/keys used by
     * server and/or client to do the SSL handshake. These strings
     * were generated using the script below. The server key and cert
     * are used to configure the server (see load_server_certificate
     * above). The ca.crt should be used to configure the client
     * when ssl verification is enabled.
     *
     *    note:
     *        cert()    ==> server.crt
     *        key()     ==> server.key
     *        ca_cert() ==> ca.crt
     *        dh()      ==> dh.pem
     ```
        #!/usr/bin/env bash

        mkdir -p /tmp/__certs__
        pushd /tmp/__certs__
        rm *.crt *.key *.pem

        # generate CA
        openssl genrsa -out ca.key 2048
        openssl req -new -x509 -nodes -days 10000 -key ca.key -out ca.crt \
            -subj "/C=US/ST=CA/L=Los
     Angeles/O=rippled-unit-tests/CN=example.com" # generate private cert
        openssl genrsa -out server.key 2048
        # Generate certificate signing request
        # since our unit tests can run in either ipv4 or ipv6 mode,
        # we need to use extensions (subjectAltName) so that we can
        # associate both ipv4 and ipv6 localhost addresses with this cert
        cat >"extras.cnf" <<EOF
        [req]
        req_extensions = v3_req
        distinguished_name = req_distinguished_name

        [req_distinguished_name]

        [v3_req]
        subjectAltName = @alt_names

        [alt_names]
        DNS.1 = localhost
        IP.1 = ::1
        EOF
        openssl req -new -key server.key -out server.csr \
            -config extras.cnf \
            -subj "/C=US/ST=California/L=San
     Francisco/O=rippled-unit-tests/CN=127.0.0.1" \

        # Create public certificate by signing with our CA
        openssl x509 -req -days 10000 -in server.csr -CA ca.crt -CAkey ca.key
     -out server.crt \ -extfile extras.cnf -set_serial 01 -extensions v3_req

        # generate DH params for server
        openssl dhparam -out dh.pem 2048
        # verify certs
        openssl verify -CAfile ca.crt server.crt
        openssl x509 -in server.crt -text -noout
        popd
     ```
    */
    static std::string const&
    cert()
    {
        static std::string const cert{R"cert(
-----BEGIN CERTIFICATE-----
MIIDczCCAlugAwIBAgIBATANBgkqhkiG9w0BAQsFADBjMQswCQYDVQQGEwJVUzEL
MAkGA1UECAwCQ0ExFDASBgNVBAcMC0xvcyBBbmdlbGVzMRswGQYDVQQKDBJyaXBw
bGVkLXVuaXQtdGVzdHMxFDASBgNVBAMMC2V4YW1wbGUuY29tMB4XDTIyMDIwNTIz
NDk0M1oXDTQ5MDYyMzIzNDk0M1owazELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNh
bGlmb3JuaWExFjAUBgNVBAcMDVNhbiBGcmFuY2lzY28xGzAZBgNVBAoMEnJpcHBs
ZWQtdW5pdC10ZXN0czESMBAGA1UEAwwJMTI3LjAuMC4xMIIBIjANBgkqhkiG9w0B
AQEFAAOCAQ8AMIIBCgKCAQEAueZ1hgRxwPgfeVx2AdngUYx7zYcaxcGYXyqi7izJ
qTuBUcVcTRC/9Ip67RAEhfcgGudRS/a4Sv1ljwiRknSCcD/ZjzOFDLgbqYGSZNEs
+T/qkwmc/L+Pbzf85HM7RjeGOd6NDQy9+oOBbUtqpTxcSGa4ln+YBFUSeoS1Aa9f
n9vrxnWX9LgTu5dSWzH5TqFIti+Zs/v0PFjEivBIAOHPslmnzg/wCr99I6z9CAR3
zVDe7+sxR//ivpeVE7FWjgkGixnUpZAqn69zNkJjMLNXETgOYskZdMIgbVOMr+0q
S1Uj77mhwxKfpnB6TqUVvWLBvmBDzPjf0m0NcCf9UAjqPwIDAQABoyowKDAmBgNV
HREEHzAdgglsb2NhbGhvc3SHEAAAAAAAAAAAAAAAAAAAAAEwDQYJKoZIhvcNAQEL
BQADggEBAJkUFNS0CeEAKvo0ttzooXnCDH3esj2fwmLJQYLUGsAF8DFrFHTqZEcx
hFRdr0ftEb/VKpV9dVF6xtSoMU56kHOnhbHEWADyqdKUkCDjrGBet5QdWmEwNV2L
nYrwGQBAybMt/+1XMUV8HeLFJNHnyxfQYcW0fUsrmNGk8W0kzWuuq88qbhfXZAIx
KiXrzYpLlM0RlpWXRfYQ6mTdSrRrLnEo5MklizVgNB8HYX78lxa06zP08oReQcfT
GSGO8NEEq8BTVmp69zD1JyfvQcXzsi7WtkAX+/EOFZ7LesnZ6VsyjZ74wECCaQuD
X1yu/XxHqchM+DOzzVw6wRKaM7Zsk80=
-----END CERTIFICATE-----
)cert"};
        return cert;
    }

    static std::string const&
    key()
    {
        static std::string const key{R"pkey(
-----BEGIN RSA PRIVATE KEY-----
MIIEpAIBAAKCAQEAueZ1hgRxwPgfeVx2AdngUYx7zYcaxcGYXyqi7izJqTuBUcVc
TRC/9Ip67RAEhfcgGudRS/a4Sv1ljwiRknSCcD/ZjzOFDLgbqYGSZNEs+T/qkwmc
/L+Pbzf85HM7RjeGOd6NDQy9+oOBbUtqpTxcSGa4ln+YBFUSeoS1Aa9fn9vrxnWX
9LgTu5dSWzH5TqFIti+Zs/v0PFjEivBIAOHPslmnzg/wCr99I6z9CAR3zVDe7+sx
R//ivpeVE7FWjgkGixnUpZAqn69zNkJjMLNXETgOYskZdMIgbVOMr+0qS1Uj77mh
wxKfpnB6TqUVvWLBvmBDzPjf0m0NcCf9UAjqPwIDAQABAoIBAEC9MDpOu+quvg8+
kt4MKSFdIhQuM7WguNaTe5AkSspDrcJzT7SK275mp259QIYCzMxxuA8TSZTb8A1C
t6dgKbi7k6FaGMCYMRHzzK6NZfMbPi6cj245q9LYlZpdQswuM/FdPpPH1zUxrNYK
CIaooZ6ZHzlSD/eaRMgkBQEkONHrZZtEinLIvKedwssPCaXkIISmt7MFQTDOlxkf
K0Mt1mnRREPYbYSfPEEfIyy/KDIiB5AzgGt+uPOn8Oeb1pSqy69jpYcfhSj+bo4S
UV6qTuTfBd4qkkNI6d/Z7DcDJFFlfloG/vVgGk/beWNnL2e39vzxiebB3w+MQn4F
Wyx5mCECgYEA22z1/ihqt9LIAWtP42oSS3S/RxlFzpp5d7QfNqFnEoVgeRhQzleP
pRJIzVXpMYBxexZYqZA/q8xBSggz+2gmRoYnW20VIzl14DsSH378ye3FRwJB0tLy
dWU8DC7ZB5XQCTvI9UY3voJNToknODw7RCNO1h3V3T1y6JRLdcLskk8CgYEA2OLy
aE5bvsUaLBSv7W9NFhSuZ0p9Y0pFmRgHI7g8i/AgRZ0BgiE8u8OZSHmPJPMaNs/h
YIEIrlsgDci1PzwrUYseRp/aiVE1kyev09/ihqRXTPpLQu6h/d63KRe/06W3t5X3
Dmfj49hH5zGPBI/0y1ECV/n0fwnRhxSv7fNr3RECgYBEuFpOUAAkNApZj29ErNqv
8Q9ayAp5yx1RpQLFjEUIoub05e2gwgGF1DUiwc43p59iyjvYVwnp1x13fxwwl4yt
N6Sp2H7vOja1lCp33MB0yVeohodw7InsxFjLA/0KiBvQWH32exhIPOzTNNcooIx7
KYeuPUfWc0FCn/cGGZcXtwKBgQC1hp1k99CKBuY05suoanOWe5DNGud/ZvaBgD7Z
gqYKadxY52QPyknOzZNJuZQ5VM8n+S2lW9osNFDLuKUaW/3Vrh6U9c4vCC1TEPB0
4PnzvzDiWMsNJjWnCfU7C4meVyFBIt84y3NNjAQCWNRe+S3lzdOsVqRwf4NDD+l/
uzEYQQKBgQCJczIlwobm1Y6O41hbGZhZL/CGMNS6Z0INi2yasV0WDqYlh7XayHMD
cK55dMILcbHqeIBq/wR6sIhw6IJcaDBfFfrJiKKDilfij2lHxR2FQrEngtTCCRV+
ZzARzaWhQPvbDqEtLJDWuXZNXfL8/PTIs5NmuKuQ8F4+gQJpkQgwaw==
-----END RSA PRIVATE KEY-----
)pkey"};
        return key;
    }

    static std::string const&
    ca_cert()
    {
        static std::string const cert{R"cert(
-----BEGIN CERTIFICATE-----
MIIDpzCCAo+gAwIBAgIUWc45WqaaNuaSLoFYTMC/Mjfqw/gwDQYJKoZIhvcNAQEL
BQAwYzELMAkGA1UEBhMCVVMxCzAJBgNVBAgMAkNBMRQwEgYDVQQHDAtMb3MgQW5n
ZWxlczEbMBkGA1UECgwScmlwcGxlZC11bml0LXRlc3RzMRQwEgYDVQQDDAtleGFt
cGxlLmNvbTAeFw0yMjAyMDUyMzQ5MDFaFw00OTA2MjMyMzQ5MDFaMGMxCzAJBgNV
BAYTAlVTMQswCQYDVQQIDAJDQTEUMBIGA1UEBwwLTG9zIEFuZ2VsZXMxGzAZBgNV
BAoMEnJpcHBsZWQtdW5pdC10ZXN0czEUMBIGA1UEAwwLZXhhbXBsZS5jb20wggEi
MA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQC0f2JBW2XNW2wT5/ajX2qxmUY+
aNJGfpV6gZ5CmwdQpbHrPPvJoskxwsCyr3GifzT/GtCpmb1fiu59uUAPxQEYCxiq
V+HchX4g4Vl27xKJ0P+usxuEED9v7TCteKum9u9eMZ8UDF0fspXcnWGs9fXlyoTj
uTRP1SBQllk44DPc/KzlrtH+QNXmr9XQnP8XvwWCgJXMx87voxEGiFFOVhkSSAOv
v+OUGgEuq0NPgwv2LHBlYHSdkoU9F5Z/TmkCAFMShbyoUjldIz2gcWXjN2tespGo
D6qYvasvPIpmcholBBkc0z8QDt+RNq+Wzrults7epJXy/u+txGK9cHCNlLCpAgMB
AAGjUzBRMB0GA1UdDgQWBBS1oydh+YyqDNOFKYOvOtVMWKqV4zAfBgNVHSMEGDAW
gBS1oydh+YyqDNOFKYOvOtVMWKqV4zAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3
DQEBCwUAA4IBAQCDPyGKQwQ8Lz0yEgvIl/Uo9BtwAzlvjrLM/39qhStLQqDGSs2Q
xFIbtjzjuLf5vR3q6OJ62CCvzqXgHkJ+hzVN/tAvyliGTdjJrK+xv1M5a+XipO2f
c9lb4gRbFL/DyoeoWgb1Rkv3gFf0FlCYH+ZUcYb9ZYCRlGtFgOcxJI2g+T7jSLFp
8+hSzQ6W5Sp9L6b5iJyCww1vjBvBqzNyZMNeB4gXGtd6z9vMDSvKboTdGD7wcFB+
mRMyNekaRw+Npy4Hjou5sx272cXHHmPCSF5TjwdaibSaGjx1k0Q50mOf7S9KG5b5
7X1e3FekJlaD02EBEhtkXURIxogOQALdFncj
-----END CERTIFICATE-----
)cert"};
        return cert;
    }

    static std::string const&
    dh()
    {
        static std::string const dh{R"dh(
-----BEGIN DH PARAMETERS-----
MIIBCAKCAQEAp2I2fWEUZ3sCNfitSRC/MdAhJE/bS+NO0O2tWdIdlvmIFE6B5qhC
sGW9ojrQT8DTxBvGAcbjr/jagmlE3BV4oSnxyhP37G2mDvMOJ29J3NvFD/ZFAW0d
BvZJ1RNvMu29NmVCyt6/jgzcqrqnami9uD93aK+zaVrlPsPEYM8xB19HXwqsEYCL
ux2B7sqXm9Ts74HPg/EV+pcVon9phxNWxxgHlOvFc2QjZ3hXH++kzmJ4vs7N/XDB
xbEQ+TUZ5jbJGSeBqNFKFeuOUQGJ46Io0jBSYd4rSmKUXkvElQwR+n7KF3jy1uAt
/8hzd8tHn9TyW7Q2/CPkOA6dCXzltpOSowIBAg==
-----END DH PARAMETERS-----
)dh"};
        return dh;
    }

private:
    struct lambda
    {
        int id;
        TrustedPublisherServer& self;
        socket_type sock;
        boost::asio::executor_work_guard<boost::asio::executor> work;
        bool ssl;

        lambda(
            int id_,
            TrustedPublisherServer& self_,
            socket_type&& sock_,
            bool ssl_)
            : id(id_)
            , self(self_)
            , sock(std::move(sock_))
            , work(sock_.get_executor())
            , ssl(ssl_)
        {
        }

        void
        operator()()
        {
            self.do_peer(id, std::move(sock), ssl);
        }
    };

    void
    on_accept(error_code ec)
    {
        if (ec || !acceptor_.is_open())
            return;

        static int id_ = 0;
        std::thread{lambda{++id_, *this, std::move(sock_), useSSL_}}.detach();
        acceptor_.async_accept(
            sock_,
            [wp = std::weak_ptr<TrustedPublisherServer>{shared_from_this()}](
                error_code ec) {
                if (auto p = wp.lock())
                {
                    p->on_accept(ec);
                }
            });
    }

    void
    do_peer(int id, socket_type&& s, bool ssl)
    {
        using namespace boost::beast;
        using namespace boost::asio;
        socket_type sock(std::move(s));
        flat_buffer sb;
        error_code ec;
        std::optional<ssl_stream<ip::tcp::socket&>> ssl_stream;

        if (ssl)
        {
            // Construct the stream around the socket
            ssl_stream.emplace(sock, sslCtx_);
            // Perform the SSL handshake
            ssl_stream->handshake(ssl::stream_base::server, ec);
            if (ec)
                return;
        }

        for (;;)
        {
            resp_type res;
            req_type req;
            try
            {
                if (ssl)
                    http::read(*ssl_stream, sb, req, ec);
                else
                    http::read(sock, sb, req, ec);

                if (ec)
                    break;

                std::string_view const path = req.target();
                res.insert("Server", "TrustedPublisherServer");
                res.version(req.version());
                res.keep_alive(req.keep_alive());
                bool prepare = true;

                if (boost::starts_with(path, "/validators2"))
                {
                    res.result(http::status::ok);
                    res.insert("Content-Type", "application/json");
                    if (path == "/validators2/bad")
                        res.body() = "{ 'bad': \"2']";
                    else if (path == "/validators2/missing")
                        res.body() = "{\"version\": 2}";
                    else
                    {
                        int refresh = 5;
                        constexpr char const* refreshPrefix =
                            "/validators2/refresh/";
                        if (boost::starts_with(path, refreshPrefix))
                            refresh = boost::lexical_cast<unsigned int>(
                                path.substr(strlen(refreshPrefix)));
                        res.body() = getList2_(refresh);
                    }
                }
                else if (boost::starts_with(path, "/validators"))
                {
                    res.result(http::status::ok);
                    res.insert("Content-Type", "application/json");
                    if (path == "/validators/bad")
                        res.body() = "{ 'bad': \"1']";
                    else if (path == "/validators/missing")
                        res.body() = "{\"version\": 1}";
                    else
                    {
                        int refresh = 5;
                        constexpr char const* refreshPrefix =
                            "/validators/refresh/";
                        if (boost::starts_with(path, refreshPrefix))
                            refresh = boost::lexical_cast<unsigned int>(
                                path.substr(strlen(refreshPrefix)));
                        res.body() = getList_(refresh);
                    }
                }
                else if (boost::starts_with(path, "/textfile"))
                {
                    prepare = false;
                    res.result(http::status::ok);
                    res.insert("Content-Type", "text/example");
                    // if huge was requested, lie about content length
                    std::uint64_t cl =
                        boost::starts_with(path, "/textfile/huge")
                        ? std::numeric_limits<uint64_t>::max()
                        : 1024;
                    res.content_length(cl);
                    if (req.method() == http::verb::get)
                    {
                        std::stringstream body;
                        for (auto i = 0; i < 1024; ++i)
                            body << static_cast<char>(rand_int<short>(32, 126)),
                                res.body() = body.str();
                    }
                }
                else if (boost::starts_with(path, "/sleep/"))
                {
                    auto const sleep_sec =
                        boost::lexical_cast<unsigned int>(path.substr(7));
                    std::this_thread::sleep_for(
                        std::chrono::seconds(sleep_sec));
                }
                else if (boost::starts_with(path, "/redirect"))
                {
                    if (boost::ends_with(path, "/301"))
                        res.result(http::status::moved_permanently);
                    else if (boost::ends_with(path, "/302"))
                        res.result(http::status::found);
                    else if (boost::ends_with(path, "/307"))
                        res.result(http::status::temporary_redirect);
                    else if (boost::ends_with(path, "/308"))
                        res.result(http::status::permanent_redirect);

                    std::stringstream location;
                    if (boost::starts_with(path, "/redirect_to/"))
                    {
                        location << path.substr(13);
                    }
                    else if (!boost::starts_with(path, "/redirect_nolo"))
                    {
                        location
                            << (ssl ? "https://" : "http://")
                            << local_endpoint()
                            << (boost::starts_with(path, "/redirect_forever/")
                                    ? path
                                    : "/validators");
                    }
                    if (!location.str().empty())
                        res.insert("Location", location.str());
                }
                else
                {
                    // unknown request
                    res.result(boost::beast::http::status::not_found);
                    res.insert("Content-Type", "text/html");
                    res.body() = "The file '" + std::string(path) +
                        "' was not "
                        "found";
                }

                if (prepare)
                    res.prepare_payload();
            }
            catch (std::exception const& e)
            {
                res = {};
                res.result(boost::beast::http::status::internal_server_error);
                res.version(req.version());
                res.insert("Server", "TrustedPublisherServer");
                res.insert("Content-Type", "text/html");
                res.body() =
                    std::string{"An internal error occurred"} + e.what();
                res.prepare_payload();
            }

            if (ssl)
                write(*ssl_stream, res, ec);
            else
                write(sock, res, ec);

            if (ec || req.need_eof())
                break;
        }

        // Perform the SSL shutdown
        if (ssl)
            ssl_stream->shutdown(ec);
    }
};

inline std::shared_ptr<TrustedPublisherServer>
make_TrustedPublisherServer(
    boost::asio::io_context& ioc,
    std::vector<TrustedPublisherServer::Validator> const& validators,
    NetClock::time_point validUntil,
    std::vector<std::pair<NetClock::time_point, NetClock::time_point>> const&
        futures,
    bool useSSL = false,
    int version = 1,
    bool immediateStart = true,
    int sequence = 1)
{
    auto const r = std::make_shared<TrustedPublisherServer>(
        ioc, validators, validUntil, futures, useSSL, version, sequence);
    if (immediateStart)
        r->start();
    return r;
}

}  // namespace test
}  // namespace ripple
#endif
