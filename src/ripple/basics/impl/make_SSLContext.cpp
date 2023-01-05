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

#include <ripple/basics/contract.h>
#include <ripple/basics/make_SSLContext.h>
#include <ctime>
#include <stdexcept>

namespace ripple {
namespace openssl {
namespace detail {

/** The default strength of self-signed RSA certifices.

    Per NIST Special Publication 800-57 Part 3, 2048-bit RSA is still
    considered acceptably secure. Generally, we would want to go above
    and beyond such recommendations (e.g. by using 3072 or 4096 bits)
    but there is a computational cost associated with that may not
    be worth paying, considering that:

    - We regenerate a new ephemeral certificate and a securely generated
      random private key every time the server is started; and
    - There should not be any truly secure information (e.g. seeds or private
      keys) that gets relayed to the server anyways over these RPCs.

      @note If you increase the number of bits you need to generate new
            default DH parameters and update defaultDH  accordingly.
 * */
int defaultRSAKeyBits = 2048;

/** The default DH parameters.

    These were generated using the OpenSSL command: `openssl dhparam 2048`
    by Nik Bougalis <nikb@bougalis.net> on May, 29, 2022.

    It is safe to use this, but if you want you can generate different
    parameters and put them here. There's no easy way to change this
    via the config file at this time.

    @note If you increase the number of bits you need to update
          defaultRSAKeyBits accordingly.
 */
static constexpr char const defaultDH[] =
    "-----BEGIN DH PARAMETERS-----\n"
    "MIIBCAKCAQEApKSWfR7LKy0VoZ/SDCObCvJ5HKX2J93RJ+QN8kJwHh+uuA8G+t8Q\n"
    "MDRjL5HanlV/sKN9HXqBc7eqHmmbqYwIXKUt9MUZTLNheguddxVlc2IjdP5i9Ps8\n"
    "l7su8tnP0l1JvC6Rfv3epRsEAw/ZW/lC2IwkQPpOmvnENQhQ6TgrUzcGkv4Bn0X6\n"
    "pxrDSBpZ+45oehGCUAtcbY8b02vu8zPFoxqo6V/+MIszGzldlik5bVqrJpVF6E8C\n"
    "tRqHjj6KuDbPbjc+pRGvwx/BSO3SULxmYu9J1NOk090MU1CMt6IJY7TpEc9Xrac9\n"
    "9yqY3xXZID240RRcaJ25+U4lszFPqP+CEwIBAg==\n"
    "-----END DH PARAMETERS-----";

/** The default list of ciphers we accept over TLS.

    Generally we include cipher suites that are part of TLS v1.2, but
    we specifically exclude:

    - the DSS cipher suites (!DSS);
    - cipher suites using pre-shared keys (!PSK);
    - cipher suites that don't offer encryption (!eNULL); and
    - cipher suites that don't offer authentication (!aNULL).

    @note Server administrators can override this default list, on either a
          global or per-port basis, using the `ssl_ciphers` directive in the
          config file.
 */
std::string const defaultCipherList = "TLSv1.2:!CBC:!DSS:!PSK:!eNULL:!aNULL";

static void
initAnonymous(boost::asio::ssl::context& context)
{
    using namespace openssl;

    static auto defaultRSA = []() {
        BIGNUM* bn = BN_new();
        BN_set_word(bn, RSA_F4);

        auto rsa = RSA_new();

        if (!rsa)
            LogicError("RSA_new failed");

        if (RSA_generate_key_ex(rsa, defaultRSAKeyBits, bn, nullptr) != 1)
            LogicError("RSA_generate_key_ex failure");

        BN_clear_free(bn);

        return rsa;
    }();

    static auto defaultEphemeralPrivateKey = []() {
        auto pkey = EVP_PKEY_new();

        if (!pkey)
            LogicError("EVP_PKEY_new failed");

        // We need to up the reference count of here, since we are retaining a
        // copy of the key for (potential) reuse.
        if (RSA_up_ref(defaultRSA) != 1)
            LogicError(
                "EVP_PKEY_assign_RSA: incrementing reference count failed");

        if (!EVP_PKEY_assign_RSA(pkey, defaultRSA))
            LogicError("EVP_PKEY_assign_RSA failed");

        return pkey;
    }();

    static auto defaultCert = []() {
        auto x509 = X509_new();

        if (x509 == nullptr)
            LogicError("X509_new failed");

        // According to the standards (X.509 et al), the value should be one
        // less than the actualy certificate version we want. Since we want
        // version 3, we must use a 2.
        X509_set_version(x509, 2);

        // To avoid leaking information about the precise time that the
        // server started up, we adjust the validity period:
        char buf[16] = {0};

        auto const ts = std::time(nullptr) - (25 * 60 * 60);

        int ret = std::strftime(
            buf, sizeof(buf) - 1, "%y%m%d000000Z", std::gmtime(&ts));

        buf[ret] = 0;

        if (ASN1_TIME_set_string_X509(X509_get_notBefore(x509), buf) != 1)
            LogicError("Unable to set certificate validity date");

        // And make it valid for two years
        X509_gmtime_adj(X509_get_notAfter(x509), 2 * 365 * 24 * 60 * 60);

        // Set a serial number
        if (auto b = BN_new(); b != nullptr)
        {
            if (BN_rand(b, 128, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY))
            {
                if (auto a = ASN1_INTEGER_new(); a != nullptr)
                {
                    if (BN_to_ASN1_INTEGER(b, a))
                        X509_set_serialNumber(x509, a);

                    ASN1_INTEGER_free(a);
                }
            }

            BN_clear_free(b);
        }

        // Some certificate details
        {
            X509V3_CTX ctx;

            X509V3_set_ctx_nodb(&ctx);
            X509V3_set_ctx(&ctx, x509, x509, nullptr, nullptr, 0);

            if (auto ext = X509V3_EXT_conf_nid(
                    nullptr, &ctx, NID_basic_constraints, "critical,CA:FALSE"))
            {
                X509_add_ext(x509, ext, -1);
                X509_EXTENSION_free(ext);
            }

            if (auto ext = X509V3_EXT_conf_nid(
                    nullptr,
                    &ctx,
                    NID_ext_key_usage,
                    "critical,serverAuth,clientAuth"))
            {
                X509_add_ext(x509, ext, -1);
                X509_EXTENSION_free(ext);
            }

            if (auto ext = X509V3_EXT_conf_nid(
                    nullptr, &ctx, NID_key_usage, "critical,digitalSignature"))
            {
                X509_add_ext(x509, ext, -1);
                X509_EXTENSION_free(ext);
            }

            if (auto ext = X509V3_EXT_conf_nid(
                    nullptr, &ctx, NID_subject_key_identifier, "hash"))
            {
                X509_add_ext(x509, ext, -1);
                X509_EXTENSION_free(ext);
            }
        }

        // And a private key
        X509_set_pubkey(x509, defaultEphemeralPrivateKey);

        if (!X509_sign(x509, defaultEphemeralPrivateKey, EVP_sha256()))
            LogicError("X509_sign failed");

        return x509;
    }();

    SSL_CTX* const ctx = context.native_handle();

    if (SSL_CTX_use_certificate(ctx, defaultCert) <= 0)
        LogicError("SSL_CTX_use_certificate failed");

    if (SSL_CTX_use_PrivateKey(ctx, defaultEphemeralPrivateKey) <= 0)
        LogicError("SSL_CTX_use_PrivateKey failed");
}

static void
initAuthenticated(
    boost::asio::ssl::context& context,
    std::string const& key_file,
    std::string const& cert_file,
    std::string const& chain_file)
{
    auto fmt_error = [](boost::system::error_code ec) -> std::string {
        return " [" + std::to_string(ec.value()) + ": " + ec.message() + "]";
    };

    SSL_CTX* const ssl = context.native_handle();

    bool cert_set = false;

    if (!cert_file.empty())
    {
        boost::system::error_code ec;

        context.use_certificate_file(
            cert_file, boost::asio::ssl::context::pem, ec);

        if (ec)
            LogicError("Problem with SSL certificate file" + fmt_error(ec));

        cert_set = true;
    }

    if (!chain_file.empty())
    {
        // VFALCO Replace fopen() with RAII
        FILE* f = fopen(chain_file.c_str(), "r");

        if (!f)
        {
            LogicError(
                "Problem opening SSL chain file" +
                fmt_error(boost::system::error_code(
                    errno, boost::system::generic_category())));
        }

        try
        {
            for (;;)
            {
                X509* const x = PEM_read_X509(f, nullptr, nullptr, nullptr);

                if (x == nullptr)
                    break;

                if (!cert_set)
                {
                    if (SSL_CTX_use_certificate(ssl, x) != 1)
                        LogicError(
                            "Problem retrieving SSL certificate from chain "
                            "file.");

                    cert_set = true;
                }
                else if (SSL_CTX_add_extra_chain_cert(ssl, x) != 1)
                {
                    X509_free(x);
                    LogicError("Problem adding SSL chain certificate.");
                }
            }

            fclose(f);
        }
        catch (std::exception const&)
        {
            fclose(f);
            LogicError("Reading the SSL chain file generated an exception.");
        }
    }

    if (!key_file.empty())
    {
        boost::system::error_code ec;

        context.use_private_key_file(
            key_file, boost::asio::ssl::context::pem, ec);

        if (ec)
        {
            LogicError(
                "Problem using the SSL private key file" + fmt_error(ec));
        }
    }

    if (SSL_CTX_check_private_key(ssl) != 1)
    {
        LogicError("Invalid key in SSL private key file.");
    }
}

std::shared_ptr<boost::asio::ssl::context>
get_context(std::string cipherList)
{
    auto c = std::make_shared<boost::asio::ssl::context>(
        boost::asio::ssl::context::sslv23);

    c->set_options(
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::no_sslv3 |
        boost::asio::ssl::context::no_tlsv1 |
        boost::asio::ssl::context::no_tlsv1_1 |
        boost::asio::ssl::context::single_dh_use |
        boost::asio::ssl::context::no_compression);

    if (cipherList.empty())
        cipherList = defaultCipherList;

    if (auto result =
            SSL_CTX_set_cipher_list(c->native_handle(), cipherList.c_str());
        result != 1)
        LogicError("SSL_CTX_set_cipher_list failed");

    c->use_tmp_dh({std::addressof(detail::defaultDH), sizeof(defaultDH)});

    // Disable all renegotiation support in TLS v1.2. This can help prevent
    // exploitation of the bug described in CVE-2021-3499 (for details see
    // https://www.openssl.org/news/secadv/20210325.txt) when linking
    // against OpenSSL versions prior to 1.1.1k.
    SSL_CTX_set_options(c->native_handle(), SSL_OP_NO_RENEGOTIATION);

    return c;
}

}  // namespace detail
}  // namespace openssl

//------------------------------------------------------------------------------
std::shared_ptr<boost::asio::ssl::context>
make_SSLContext(std::string const& cipherList)
{
    auto context = openssl::detail::get_context(cipherList);
    openssl::detail::initAnonymous(*context);
    // VFALCO NOTE, It seems the WebSocket context never has
    // set_verify_mode called, for either setting of WEBSOCKET_SECURE
    context->set_verify_mode(boost::asio::ssl::verify_none);
    return context;
}

std::shared_ptr<boost::asio::ssl::context>
make_SSLContextAuthed(
    std::string const& keyFile,
    std::string const& certFile,
    std::string const& chainFile,
    std::string const& cipherList)
{
    auto context = openssl::detail::get_context(cipherList);
    openssl::detail::initAuthenticated(*context, keyFile, certFile, chainFile);
    return context;
}

}  // namespace ripple
