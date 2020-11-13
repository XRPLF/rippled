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
MIIDczCCAlugAwIBAgIBATANBgkqhkiG9w0BAQUFADBjMQswCQYDVQQGEwJVUzEL
MAkGA1UECAwCQ0ExFDASBgNVBAcMC0xvcyBBbmdlbGVzMRswGQYDVQQKDBJyaXBw
bGVkLXVuaXQtdGVzdHMxFDASBgNVBAMMC2V4YW1wbGUuY29tMB4XDTE5MDgwNzE3
MzM1OFoXDTQ2MTIyMzE3MzM1OFowazELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNh
bGlmb3JuaWExFjAUBgNVBAcMDVNhbiBGcmFuY2lzY28xGzAZBgNVBAoMEnJpcHBs
ZWQtdW5pdC10ZXN0czESMBAGA1UEAwwJMTI3LjAuMC4xMIIBIjANBgkqhkiG9w0B
AQEFAAOCAQ8AMIIBCgKCAQEA5Ky0UE9K+gFOznfwBvq2HfnQOOPGtVf4G9m63b5V
QNJYCSNiYxkGZW72ESM3XA8BledlkV9pwIm17+7ucB1Ed3efQjQDq2RSk5LDYDaa
r0Qzzy0EC3b9+AKA6mAoVY6s1Qws/YvM4esz0H+SVvtVcFqA46kRWhJN7M5ic1lu
d58fAq04BHqi5zOEOzfHJYPGUgQOxRTHluYkkkBrL2xioHHnOROshW+PIYFiAc/h
WPzuihPHnKaziPRw+O6O8ysnCxycQHgqtvx73T52eJdLxtr3ToRWaY/8VF/Cog5c
uvWEtg6EucGOszIH8O7eJWaJpVpAfZIX+c62MQWLpOLi/QIDAQABoyowKDAmBgNV
HREEHzAdgglsb2NhbGhvc3SHEAAAAAAAAAAAAAAAAAAAAAEwDQYJKoZIhvcNAQEF
BQADggEBAOhLAO/e0lGi9TZ2HiVi4sJ7KVQaBQHGhfsysILoQNHrNqDypPc/ZrSa
WQ2OqyUeltMnUdN5S1h3MKRZlbAeBQlwkPdjTzlzWkCMWB5BsfIGy5ovqmNQ7zPa
Khg5oxq3mU8ZLiJP4HngyU+hOOCt5tttex2S8ubjFT+3C3cydLKEOXCUPspaVkKn
Eq8WSBoYTvyUVmSi6+m6HGiowWsM5Qgj93IRW6JCbkgfPeKXC/5ykAPQcFHwNaKT
rpWokcavZyMbVjRsbzCQcc7n2j7tbLOu2svSLy6oXwG6n/bEagl5WpN2/TzQuwe7
f5ktutc4DDJSV7fuYYCuGumrHAjcELE=
-----END CERTIFICATE-----
)cert"};
        return cert;
    }

    static std::string const&
    key()
    {
        static std::string const key{R"pkey(
-----BEGIN RSA PRIVATE KEY-----
MIIEpQIBAAKCAQEA5Ky0UE9K+gFOznfwBvq2HfnQOOPGtVf4G9m63b5VQNJYCSNi
YxkGZW72ESM3XA8BledlkV9pwIm17+7ucB1Ed3efQjQDq2RSk5LDYDaar0Qzzy0E
C3b9+AKA6mAoVY6s1Qws/YvM4esz0H+SVvtVcFqA46kRWhJN7M5ic1lud58fAq04
BHqi5zOEOzfHJYPGUgQOxRTHluYkkkBrL2xioHHnOROshW+PIYFiAc/hWPzuihPH
nKaziPRw+O6O8ysnCxycQHgqtvx73T52eJdLxtr3ToRWaY/8VF/Cog5cuvWEtg6E
ucGOszIH8O7eJWaJpVpAfZIX+c62MQWLpOLi/QIDAQABAoIBACf8mzs/4lh9Sg6I
ooxV4uqy+Fo6WlDzpQsZs7d6xOWk4ogWi+nQQnISSS0N/2w1o41W/UfCa3ejnRDr
sv4f4A0T+eFVvx6FWHs9urRkWAA16OldccufbyGjLm/NiMANRuOqUWO0woru2gyn
git7n6EZ8lfdBI+/i6jRHh4VkV+ROt5Zbp9zuJsj0yMqJH7J6Ebtl1jAF6PemLBL
yxdiYqR8LKTunTGGP/L+4K5a389oPDcJ1+YX805NEopmfrIhPr+BQYdz8905aVFk
FSS4TJy23EhFLzKf3+iSept6Giim+2yy2rv1RPCKgjOXbJ+4LD48xumDol6XWgYr
1CBzQIECgYEA/jBEGOjV02a9A3C5RJxZMawlGwGrvvALG2UrKvwQc595uxwrUw9S
Mn3ZQBEGnEWwEf44jSpWzp8TtejMxEvrU5243eWgwif1kqr1Mcj54DR7Qm15/hsj
M3nA2WscVG2OHBs4AwzMCHE2vfEAkbz71s6xonhg6zvsC26Zy3hYPqkCgYEA5k3k
OuCeG5FXW1/GzhvVFuhl6msNKzuUnLmJg6500XPny5Xo7W3RMvjtTM2XLt1USU6D
arMCCQ1A8ku1SoFdSw5RC6Fl8ZoUFBz9FPPwT6usQssGyFxiiqdHLvTlk12NNCk3
vJYsdQ+v/dKuZ8T4U3GTgQSwPTj6J0kJUf5y2jUCgYEA+hi/R8r/aArz+kiU4T78
O3Vm5NWWCD3ij8fQ23A7N6g3e7RRpF20wF02vmSCHowqmumI9swrsQyvthIiNxmD
pzfORvXCYIY0h2SR77QQt1qr1EYm+6/zyJgI+WL78s4APwNA7y9OKRhLhkN0DfDl
0Qp5mKPcqFbC/tSJmbsFCFECgYEAwlLC2rMgdV5jeWQNGWf+mv+ozu1ZBTuWn88l
qwiO5RSJZwysp3nb5MiJYh6vDAoQznIDDQrSEtUuEcOzypPxJh2EYO3kWMGLY5U6
Lm3OPUs7ZHhu1qytMRUISSS2eWucc4C72NJV3MhJ1T/pjQF0DuRsc5aDJoVm/bLw
vFCYlGkCgYEAgBDIIqdo1th1HE95SQfpP2wV/jA6CPamIciNwS3bpyhDBqs9oLUc
qzXidOpXAVYg1wl/BqpaCQcmmhCrnSLJYdOMpudVyLCCfYmBJ0bs2DCAe5ibGbL7
VruAOjS4yBepkXJU9xwKHxDmgTo/oQ5smq7SNOUWDSElVI/CyZ0x7qA=
-----END RSA PRIVATE KEY-----
)pkey"};
        return key;
    }

    static std::string const&
    ca_cert()
    {
        static std::string const cert{R"cert(
-----BEGIN CERTIFICATE-----
MIIDQjCCAioCCQDxKQafEvp+VTANBgkqhkiG9w0BAQsFADBjMQswCQYDVQQGEwJV
UzELMAkGA1UECAwCQ0ExFDASBgNVBAcMC0xvcyBBbmdlbGVzMRswGQYDVQQKDBJy
aXBwbGVkLXVuaXQtdGVzdHMxFDASBgNVBAMMC2V4YW1wbGUuY29tMB4XDTE5MDgw
NzE3MzM1OFoXDTQ2MTIyMzE3MzM1OFowYzELMAkGA1UEBhMCVVMxCzAJBgNVBAgM
AkNBMRQwEgYDVQQHDAtMb3MgQW5nZWxlczEbMBkGA1UECgwScmlwcGxlZC11bml0
LXRlc3RzMRQwEgYDVQQDDAtleGFtcGxlLmNvbTCCASIwDQYJKoZIhvcNAQEBBQAD
ggEPADCCAQoCggEBAO9oqh72ttM7hjPnbMcJw0EuyULocEn2hlg4HE4YtzaxlRIz
dHm8nMkG/9yGmHBCuue/Gzssm/CzlduGezae01p8eaFUuEJsjxdrXe89Wk2QH+dm
Fn+SRbGcHaaTV/cyJrvusG7pOu95HL2eebuwiZ+tX5JP01R732iQt8Beeygh/W4P
n2f//fAxbdAIWzx2DH6cmSNe6lpoQe/MN15o8V3whutcC3fkis6wcA7BKZcdVdL2
daFWA6mt4SPWldOfWQVAIX4vRvheWPy34OLCgx+wZWg691Lwd1F+paarKombatUt
vKMTeolFYl3zkZZMYvR0Oyrt5NXUhRfmG7xR3bkCAwEAATANBgkqhkiG9w0BAQsF
AAOCAQEAggKO5WdtU67QPcAdo1Uar0SFouvVLwxJvoKlQ5rqF3idd0HnFVy7iojW
G2sZq7z8SNDMkUXZLbcbYNRyrZI0PdjfI0kyNpaa3pEcPcR8aOcTEOtW6V67FrPG
8aNYpr6a8PPq12aHzPSNjlUGot/qffGIQ0H2OqdWMOUXMMFnmH2KnnWi46Aq3gaF
uyHGrEczjJAK7NTzP8A7fbrmT00Sn6ft1FriQyhvDkUgPXBGWKpOFO84V27oo0ZL
xXQHDWcpX+8yNKynjafkXLx6qXwcySF2bKcTIRsxlN6WNRqZ+wqpNStkjuoFkYR/
IfW9PBfO/gCtNJQ+lqpoTd3kLBCAng==
-----END CERTIFICATE-----
)cert"};
        return cert;
    }

    static std::string const&
    dh()
    {
        static std::string const dh{R"dh(
-----BEGIN DH PARAMETERS-----
MIIBCAKCAQEAnJaaKu3U2a7ZVBvIC+NVNHXo9q6hNCazze+4pwXAKBVXH0ozInEw
WKozYxVJLW7dvDHdjdFOSuTLQDqaPW9zVMQKM0BKu81+JyfJi7C3HYKUw7ECVHp4
DLvhDe6N5eBj/t1FUwcfS2VNIx4QcJiw6FH3CwNNee1fIi5VTRJp2GLUuMCHkT/I
FTODJ+Anw12cJqLdgQfV74UV/Y7JCQl3/DOIy+2YkmX8vWVHX1h6EI5Gw4a3jgqF
gVyCOWoVCfgu37H5e7ERyoAxigiP8hMqoGpmJUYJghVKWoFgNUqXw+guVJ56eIuH
0wVs/LXflOZ42PJAiwv4LTNOtpG2pWGjOwIBAg==
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

                auto path = req.target().to_string();
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
                        std::chrono::seconds{sleep_sec});
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
                    res.body() = "The file '" + path + "' was not found";
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
