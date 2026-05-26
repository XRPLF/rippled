#include <xrpl/basics/make_SSLContext.h>

#include <xrpl/basics/contract.h>

#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/verify_mode.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/detail/generic_category.hpp>

#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/objects.h>  // IWYU pragma: keep
#include <openssl/ossl_typ.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cerrno>
#include <cstdio>
#include <ctime>
#include <exception>
#include <memory>
#include <string>

namespace xrpl {

namespace openssl::detail {

/** The default strength of self-signed RSA certificates.

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
int gDefaultRsaKeyBits = 2048;

/** The default DH parameters.

    These were generated using the OpenSSL command: `openssl dhparam 2048`
    by Nik Bougalis <nikb@bougalis.net> on May, 29, 2022.

    It is safe to use this, but if you want you can generate different
    parameters and put them here. There's no easy way to change this
    via the config file at this time.

    @note If you increase the number of bits you need to update
          defaultRSAKeyBits accordingly.
 */
static constexpr char kDefaultDh[] =
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
std::string const kDefaultCipherList = "TLSv1.2:!CBC:!DSS:!PSK:!eNULL:!aNULL";

static void
initAnonymous(boost::asio::ssl::context& context)
{
    using namespace openssl;

    static auto kDefaultRsa = []() {
        BIGNUM* bn = BN_new();
        BN_set_word(bn, RSA_F4);

        auto rsa = RSA_new();

        if (!rsa)
            logicError("RSA_new failed");

        if (RSA_generate_key_ex(rsa, gDefaultRsaKeyBits, bn, nullptr) != 1)
            logicError("RSA_generate_key_ex failure");

        BN_clear_free(bn);

        return rsa;
    }();

    static auto kDefaultEphemeralPrivateKey = []() {
        auto pkey = EVP_PKEY_new();

        if (!pkey)
            logicError("EVP_PKEY_new failed");

        // We need to up the reference count of here, since we are retaining a
        // copy of the key for (potential) reuse.
        if (RSA_up_ref(kDefaultRsa) != 1)
            logicError("EVP_PKEY_assign_RSA: incrementing reference count failed");

        if (!EVP_PKEY_assign_RSA(pkey, kDefaultRsa))
            logicError("EVP_PKEY_assign_RSA failed");

        return pkey;
    }();

    static auto kDefaultCert = []() {
        auto x509 = X509_new();

        if (x509 == nullptr)
            logicError("X509_new failed");

        // According to the standards (X.509 et al), the value should be one
        // less than the actually certificate version we want. Since we want
        // version 3, we must use a 2.
        X509_set_version(x509, 2);

        // To avoid leaking information about the precise time that the
        // server started up, we adjust the validity period:
        char buf[16] = {0};

        auto const ts = std::time(nullptr) - (25 * 60 * 60);

        int const ret = std::strftime(buf, sizeof(buf) - 1, "%y%m%d000000Z", std::gmtime(&ts));

        buf[ret] = 0;

        if (ASN1_TIME_set_string_X509(X509_get_notBefore(x509), buf) != 1)
            logicError("Unable to set certificate validity date");

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

            if (auto ext =
                    X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, "critical,CA:FALSE"))
            {
                X509_add_ext(x509, ext, -1);
                X509_EXTENSION_free(ext);
            }

            if (auto ext = X509V3_EXT_conf_nid(
                    nullptr, &ctx, NID_ext_key_usage, "critical,serverAuth,clientAuth"))
            {
                X509_add_ext(x509, ext, -1);
                X509_EXTENSION_free(ext);
            }

            if (auto ext =
                    X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage, "critical,digitalSignature"))
            {
                X509_add_ext(x509, ext, -1);
                X509_EXTENSION_free(ext);
            }

            if (auto ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_key_identifier, "hash"))
            {
                X509_add_ext(x509, ext, -1);
                X509_EXTENSION_free(ext);
            }
        }

        // And a private key
        X509_set_pubkey(x509, kDefaultEphemeralPrivateKey);

        if (!X509_sign(x509, kDefaultEphemeralPrivateKey, EVP_sha256()))
            logicError("X509_sign failed");

        return x509;
    }();

    SSL_CTX* const ctx = context.native_handle();

    if (SSL_CTX_use_certificate(ctx, kDefaultCert) <= 0)
        logicError("SSL_CTX_use_certificate failed");

    if (SSL_CTX_use_PrivateKey(ctx, kDefaultEphemeralPrivateKey) <= 0)
        logicError("SSL_CTX_use_PrivateKey failed");
}

static void
initAuthenticated(
    boost::asio::ssl::context& context,
    std::string const& keyFile,
    std::string const& certFile,
    std::string const& chainFile)
{
    auto fmtError = [](boost::system::error_code ec) -> std::string {
        return " [" + std::to_string(ec.value()) + ": " + ec.message() + "]";
    };

    SSL_CTX* const ssl = context.native_handle();

    bool certSet = false;

    if (!certFile.empty())
    {
        boost::system::error_code ec;

        // NOLINTNEXTLINE(bugprone-unused-return-value)
        context.use_certificate_file(certFile, boost::asio::ssl::context::pem, ec);

        if (ec)
            logicError("Problem with SSL certificate file" + fmtError(ec));

        certSet = true;
    }

    if (!chainFile.empty())
    {
        // VFALCO Replace fopen() with RAII
        FILE* f = fopen(chainFile.c_str(), "r");

        if (f == nullptr)
        {
            logicError(
                "Problem opening SSL chain file" +
                fmtError(boost::system::error_code(errno, boost::system::generic_category())));
        }

        try
        {
            for (;;)
            {
                X509* const x = PEM_read_X509(f, nullptr, nullptr, nullptr);

                if (x == nullptr)
                    break;

                if (!certSet)
                {
                    if (SSL_CTX_use_certificate(ssl, x) != 1)
                    {
                        logicError(
                            "Problem retrieving SSL certificate from chain "
                            "file.");
                    }

                    certSet = true;
                }
                else if (SSL_CTX_add_extra_chain_cert(ssl, x) != 1)
                {
                    X509_free(x);
                    logicError("Problem adding SSL chain certificate.");
                }
            }

            fclose(f);
        }
        catch (std::exception const& ex)
        {
            fclose(f);
            logicError(
                std::string("Reading the SSL chain file generated an exception: ") + ex.what());
        }
    }

    if (!keyFile.empty())
    {
        boost::system::error_code ec;

        // NOLINTNEXTLINE(bugprone-unused-return-value)
        context.use_private_key_file(keyFile, boost::asio::ssl::context::pem, ec);

        if (ec)
        {
            logicError("Problem using the SSL private key file" + fmtError(ec));
        }
    }

    if (SSL_CTX_check_private_key(ssl) != 1)
    {
        logicError("Invalid key in SSL private key file.");
    }
}

std::shared_ptr<boost::asio::ssl::context>
getContext(std::string cipherList)
{
    auto c = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);

    c->set_options(
        boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::no_sslv3 | boost::asio::ssl::context::no_tlsv1 |
        boost::asio::ssl::context::no_tlsv1_1 | boost::asio::ssl::context::single_dh_use |
        boost::asio::ssl::context::no_compression);

    if (cipherList.empty())
        cipherList = kDefaultCipherList;

    if (auto result = SSL_CTX_set_cipher_list(c->native_handle(), cipherList.c_str()); result != 1)
        logicError("SSL_CTX_set_cipher_list failed");

    c->use_tmp_dh({std::addressof(detail::kDefaultDh), sizeof(kDefaultDh)});

    // Disable all renegotiation support in TLS v1.2. This can help prevent
    // exploitation of the bug described in CVE-2021-3499 (for details see
    // https://www.openssl.org/news/secadv/20210325.txt) when linking
    // against OpenSSL versions prior to 1.1.1k.
    SSL_CTX_set_options(c->native_handle(), SSL_OP_NO_RENEGOTIATION);

    return c;
}

}  // namespace openssl::detail

//------------------------------------------------------------------------------
std::shared_ptr<boost::asio::ssl::context>
makeSslContext(std::string const& cipherList)
{
    auto context = openssl::detail::getContext(cipherList);
    openssl::detail::initAnonymous(*context);
    // VFALCO NOTE, It seems the WebSocket context never has
    // set_verify_mode called, for either setting of WEBSOCKET_SECURE
    context->set_verify_mode(boost::asio::ssl::verify_none);
    return context;
}

std::shared_ptr<boost::asio::ssl::context>
makeSslContextAuthed(
    std::string const& keyFile,
    std::string const& certFile,
    std::string const& chainFile,
    std::string const& cipherList)
{
    auto context = openssl::detail::getContext(cipherList);
    openssl::detail::initAuthenticated(*context, keyFile, certFile, chainFile);
    return context;
}

}  // namespace xrpl
