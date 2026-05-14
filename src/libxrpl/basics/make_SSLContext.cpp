/** @file
 *  TLS context factory for the XRP Ledger node.
 *
 *  Provides two flavors of `boost::asio::ssl::context`: an anonymous variant
 *  used for peer overlay connections (where application-layer node identity
 *  makes certificate-based authentication redundant) and an authenticated
 *  variant used for operator-configured RPC/WebSocket endpoints.  Both share
 *  a common `getContext()` base that enforces TLS 1.2+, DH parameters, and a
 *  hardened AEAD-only cipher list.
 *
 *  @note The TSAN suppression file (`sanitizers/suppressions/tsan.supp`)
 *      contains an explicit entry for this file, acknowledging a benign
 *      initialization race on the `static` locals inside `initAnonymous()`.
 *      The race is benign: all competing initializations produce identical
 *      results and the statics are idempotent once set.
 */
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
        default DH parameters and update `kDEFAULT_DH` accordingly.
 */
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
static constexpr char const kDEFAULT_DH[] =
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
std::string const kDEFAULT_CIPHER_LIST = "TLSv1.2:!CBC:!DSS:!PSK:!eNULL:!aNULL";

/** Install a process-lifetime ephemeral certificate into an SSL context.
 *
 *  Generates a 2048-bit RSA key pair and a self-signed X.509v3 certificate
 *  exactly once per process (via `static` locals), then installs both into
 *  @p context.  Subsequent calls reuse the same key and certificate.
 *
 *  The certificate carries no meaningful identity; it exists only to satisfy
 *  the TLS handshake.  Notable details of the generated certificate:
 *  - `notBefore` is set 25 hours before the current time (midnight-rounded)
 *    to prevent observers from inferring the server's start time.
 *  - Serial number is a fresh 128-bit random value on each process start.
 *  - Extensions: `CA:FALSE`, `keyUsage=digitalSignature`,
 *    `extendedKeyUsage=serverAuth,clientAuth`.
 *
 *  @param context The SSL context to configure.
 *  @note All OpenSSL allocation failures call `logicError()`, which is
 *      non-recoverable.  A server that cannot build its TLS context at
 *      startup has no viable recovery path.
 *  @note `RSA_up_ref()` is called before `EVP_PKEY_assign_RSA()` because
 *      `EVP_PKEY_assign_RSA` takes ownership of the key; the extra reference
 *      prevents the shared `kDEFAULT_RSA` static from being freed when the
 *      `EVP_PKEY` is eventually released.
 */
static void
initAnonymous(boost::asio::ssl::context& context)
{
    using namespace openssl;

    static auto kDEFAULT_RSA = []() {
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

    static auto kDEFAULT_EPHEMERAL_PRIVATE_KEY = []() {
        auto pkey = EVP_PKEY_new();

        if (!pkey)
            logicError("EVP_PKEY_new failed");

        if (RSA_up_ref(kDEFAULT_RSA) != 1)
            logicError("EVP_PKEY_assign_RSA: incrementing reference count failed");

        if (!EVP_PKEY_assign_RSA(pkey, kDEFAULT_RSA))
            logicError("EVP_PKEY_assign_RSA failed");

        return pkey;
    }();

    static auto kDEFAULT_CERT = []() {
        auto x509 = X509_new();

        if (x509 == nullptr)
            logicError("X509_new failed");

        // X.509 encodes version as (desired_version - 1); pass 2 for v3.
        X509_set_version(x509, 2);

        char buf[16] = {0};

        auto const ts = std::time(nullptr) - (25 * 60 * 60);

        int const ret = std::strftime(buf, sizeof(buf) - 1, "%y%m%d000000Z", std::gmtime(&ts));

        buf[ret] = 0;

        if (ASN1_TIME_set_string_X509(X509_get_notBefore(x509), buf) != 1)
            logicError("Unable to set certificate validity date");

        X509_gmtime_adj(X509_get_notAfter(x509), 2 * 365 * 24 * 60 * 60);

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

        X509_set_pubkey(x509, kDEFAULT_EPHEMERAL_PRIVATE_KEY);

        if (!X509_sign(x509, kDEFAULT_EPHEMERAL_PRIVATE_KEY, EVP_sha256()))
            logicError("X509_sign failed");

        return x509;
    }();

    SSL_CTX* const ctx = context.native_handle();

    if (SSL_CTX_use_certificate(ctx, kDEFAULT_CERT) <= 0)
        logicError("SSL_CTX_use_certificate failed");

    if (SSL_CTX_use_PrivateKey(ctx, kDEFAULT_EPHEMERAL_PRIVATE_KEY) <= 0)
        logicError("SSL_CTX_use_PrivateKey failed");
}

/** Load operator-supplied certificate and key material into an SSL context.
 *
 *  Handles three optional file paths.  Each path is skipped if empty.
 *  When @p chainFile is provided, it is read in a PEM loop: the first
 *  certificate block becomes the leaf certificate (unless @p certFile was
 *  already loaded, in which case it is added directly to the chain); all
 *  subsequent blocks are appended as intermediate CA certificates via
 *  `SSL_CTX_add_extra_chain_cert`.  This supports the common deployment
 *  pattern of a single file containing the server cert followed by the
 *  CA chain.
 *
 *  After loading all material, `SSL_CTX_check_private_key` verifies that
 *  the private key matches the leaf certificate's public key, catching
 *  misconfiguration before the server accepts any connections.
 *
 *  @param context   The SSL context to configure.
 *  @param keyFile   Path to the PEM-encoded private key file.
 *  @param certFile  Path to the PEM-encoded leaf certificate file.
 *  @param chainFile Path to a PEM file containing one or more certificates
 *      forming the CA chain (and optionally the leaf if @p certFile is
 *      empty).
 *  @note All failures call `logicError()`, which is non-recoverable.
 *  @note The chain file is opened with `fopen` (known technical debt;
 *      see `// VFALCO Replace fopen() with RAII` in the source).
 */
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

/** Create a hardened TLS context with protocol and cipher constraints.
 *
 *  Constructs a `boost::asio::ssl::context` using the `sslv23` method
 *  identifier — a Boost.Asio naming artifact that means "negotiate the best
 *  mutually supported version" — then immediately disables SSLv2, SSLv3,
 *  TLS 1.0, TLS 1.1, and compression, leaving only TLS 1.2+.  Disabling
 *  compression mitigates CRIME-class attacks.
 *
 *  The cipher list defaults to `kDEFAULT_CIPHER_LIST` if @p cipherList is
 *  empty.  The `!CBC` exclusion in the default list strips all block-cipher
 *  suites, leaving only AEAD constructions (GCM in practice), which sidestep
 *  the BEAST and POODLE attack families.
 *
 *  Hardcoded 2048-bit DH parameters (`kDEFAULT_DH`) are loaded
 *  unconditionally.  TLS 1.2 renegotiation is disabled via
 *  `SSL_OP_NO_RENEGOTIATION` as a belt-and-suspenders mitigation for
 *  CVE-2021-3499 on OpenSSL versions prior to 1.1.1k.
 *
 *  @param cipherList OpenSSL cipher list string; pass an empty string to use
 *      `kDEFAULT_CIPHER_LIST`.
 *  @return A fully configured `ssl::context` ready for anonymous or
 *      authenticated certificate installation.
 *  @note Callers must install certificate material (via `initAnonymous()` or
 *      `initAuthenticated()`) before the context can be used for a handshake.
 */
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
        cipherList = kDEFAULT_CIPHER_LIST;

    if (auto result = SSL_CTX_set_cipher_list(c->native_handle(), cipherList.c_str()); result != 1)
        logicError("SSL_CTX_set_cipher_list failed");

    c->use_tmp_dh({std::addressof(detail::kDEFAULT_DH), sizeof(kDEFAULT_DH)});

    // Belt-and-suspenders mitigation for CVE-2021-3499 (OpenSSL < 1.1.1k).
    SSL_CTX_set_options(c->native_handle(), SSL_OP_NO_RENEGOTIATION);

    return c;
}

}  // namespace openssl::detail

//------------------------------------------------------------------------------

/** Create a TLS context for anonymous peer overlay connections.
 *
 *  Builds a hardened TLS 1.2+ context, installs a process-lifetime
 *  self-signed ephemeral certificate (generated once via `initAnonymous()`),
 *  and sets `verify_none` — peer identity is established at the application
 *  layer via cryptographic node identities, so certificate validation is not
 *  required at the TLS layer.
 *
 *  Used by `OverlayImpl` for all peer-to-peer connections.
 *
 *  @param cipherList OpenSSL cipher list string; pass an empty string to use
 *      the default AEAD-only TLS 1.2 cipher list.
 *  @return A configured `ssl::context` ready for overlay use.
 *  @note The ephemeral certificate and RSA key are shared across all contexts
 *      created by this function within the same process lifetime.
 */
std::shared_ptr<boost::asio::ssl::context>
makeSslContext(std::string const& cipherList)
{
    auto context = openssl::detail::getContext(cipherList);
    openssl::detail::initAnonymous(*context);
    context->set_verify_mode(boost::asio::ssl::verify_none);
    return context;
}

/** Create a TLS context for authenticated RPC/WebSocket endpoints.
 *
 *  Builds a hardened TLS 1.2+ context and loads operator-supplied certificate
 *  and key material from disk via `initAuthenticated()`.  Unlike
 *  `makeSslContext()`, this path does not set `verify_none` and does not
 *  install an ephemeral certificate — callers are expected to present a real
 *  certificate chain trusted by connecting clients (browsers, tooling, etc.).
 *
 *  Used by `ServerHandler` for HTTP/WebSocket-facing RPC ports configured
 *  with `ssl_key`, `ssl_cert`, and/or `ssl_chain` in the config file.
 *
 *  @param keyFile    Path to the PEM-encoded private key file; may be empty.
 *  @param certFile   Path to the PEM-encoded leaf certificate; may be empty.
 *  @param chainFile  Path to a PEM file containing the CA chain (and
 *      optionally the leaf certificate); may be empty.
 *  @param cipherList OpenSSL cipher list string; pass an empty string to use
 *      the default AEAD-only TLS 1.2 cipher list.
 *  @return A configured `ssl::context` ready for authenticated use.
 *  @note If the loaded private key does not match the certificate,
 *      `logicError()` is called (non-recoverable).
 */
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
