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
#include <ripple/basics/chrono.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/make_SSLContext.h>
#include <beast/container/aged_unordered_set.h>
#include <beast/module/core/diagnostic/FatalError.h>
#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace ripple {
namespace openssl {
namespace detail {

template <class>
struct custom_delete;

template <>
struct custom_delete <RSA>
{
    void operator() (RSA* rsa) const
    {
        RSA_free (rsa);
    }
};

template <>
struct custom_delete <EVP_PKEY>
{
    void operator() (EVP_PKEY* evp_pkey) const
    {
        EVP_PKEY_free (evp_pkey);
    }
};

template <>
struct custom_delete <X509>
{
    void operator() (X509* x509) const
    {
        X509_free (x509);
    }
};

template <>
struct custom_delete <DH>
{
    void operator() (DH* dh) const
    {
        DH_free(dh);
    }
};

template <class T>
using custom_delete_unique_ptr = std::unique_ptr <T, custom_delete <T>>;

// RSA

using rsa_ptr = custom_delete_unique_ptr <RSA>;

static rsa_ptr rsa_generate_key (int n_bits)
{
    RSA* rsa = RSA_generate_key (n_bits, RSA_F4, nullptr, nullptr);

    if (rsa == nullptr)
        Throw<std::runtime_error> ("RSA_generate_key failed");

    return rsa_ptr (rsa);
}

// EVP_PKEY

using evp_pkey_ptr = custom_delete_unique_ptr <EVP_PKEY>;

static evp_pkey_ptr evp_pkey_new()
{
    EVP_PKEY* evp_pkey = EVP_PKEY_new();

    if (evp_pkey == nullptr)
        Throw<std::runtime_error> ("EVP_PKEY_new failed");

    return evp_pkey_ptr (evp_pkey);
}

static void evp_pkey_assign_rsa (EVP_PKEY* evp_pkey, rsa_ptr&& rsa)
{
    if (! EVP_PKEY_assign_RSA (evp_pkey, rsa.get()))
        Throw<std::runtime_error> ("EVP_PKEY_assign_RSA failed");

    rsa.release();
}

// X509

using x509_ptr = custom_delete_unique_ptr <X509>;

static x509_ptr x509_new()
{
    X509* x509 = X509_new();

    if (x509 == nullptr)
        Throw<std::runtime_error> ("X509_new failed");

    X509_set_version (x509, NID_X509);

    int const margin =                    60 * 60;  //      3600, one hour
    int const length = 10 * 365.25 * 24 * 60 * 60;  // 315576000, ten years

    X509_gmtime_adj (X509_get_notBefore (x509), -margin);
    X509_gmtime_adj (X509_get_notAfter  (x509),  length);

    return x509_ptr (x509);
}

static void x509_set_pubkey (X509* x509, EVP_PKEY* evp_pkey)
{
    X509_set_pubkey (x509, evp_pkey);
}

static void x509_sign (X509* x509, EVP_PKEY* evp_pkey)
{
    if (! X509_sign (x509, evp_pkey, EVP_sha1()))
        Throw<std::runtime_error> ("X509_sign failed");
}

static void ssl_ctx_use_certificate (SSL_CTX* const ctx, x509_ptr& cert)
{
    if (SSL_CTX_use_certificate (ctx, cert.release()) <= 0)
        Throw<std::runtime_error> ("SSL_CTX_use_certificate failed");
}

static void ssl_ctx_use_privatekey (SSL_CTX* const ctx, evp_pkey_ptr& key)
{
    if (SSL_CTX_use_PrivateKey (ctx, key.release()) <= 0)
        Throw<std::runtime_error> ("SSL_CTX_use_PrivateKey failed");
}

// track when SSL connections have last negotiated
struct StaticData
{
    std::mutex lock;
    beast::aged_unordered_set <SSL const*> set;

    StaticData()
        : set (ripple::stopwatch())
        { }
};

using dh_ptr = custom_delete_unique_ptr<DH>;

static
dh_ptr
make_DH(std::string const& params)
{
    auto const* p (
        reinterpret_cast <std::uint8_t const*>(&params [0]));
    DH* const dh = d2i_DHparams (nullptr, &p, params.size ());
    if (p == nullptr)
        beast::FatalError ("d2i_DHparams returned nullptr.",
            __FILE__, __LINE__);
    return dh_ptr(dh);
}

/** Retrieve the raw DH parameters for the requested key size.

    The result is in the binary format expected by the OpenSSL function
    d2i_DHparams and may contain nulls. Use size to determine the actual size.

    If the result is empty, the key size is unsupported. As of June 11, 2015,
    OpenSSL never passes anything other than 512 or 1024.
*/
static
std::string
getRawDHParams (int keySize)
{
    std::string params;

    switch (keySize)
    {
    case 512:
    {
        // These are the DH parameters that OpenCoin has chosen for Ripple
        //
        std::uint8_t const raw512 [] = {
            0x30, 0x46, 0x02, 0x41, 0x00, 0x98, 0x15, 0xd2, 0xd0, 0x08, 0x32, 0xda,
            0xaa, 0xac, 0xc4, 0x71, 0xa3, 0x1b, 0x11, 0xf0, 0x6c, 0x62, 0xb2, 0x35,
            0x8a, 0x10, 0x92, 0xc6, 0x0a, 0xa3, 0x84, 0x7e, 0xaf, 0x17, 0x29, 0x0b,
            0x70, 0xef, 0x07, 0x4f, 0xfc, 0x9d, 0x6d, 0x87, 0x99, 0x19, 0x09, 0x5b,
            0x6e, 0xdb, 0x57, 0x72, 0x4a, 0x7e, 0xcd, 0xaf, 0xbd, 0x3a, 0x97, 0x55,
            0x51, 0x77, 0x5a, 0x34, 0x7c, 0xe8, 0xc5, 0x71, 0x63, 0x02, 0x01, 0x02
        };

        params.resize (sizeof (raw512));
        std::copy (raw512, raw512 + sizeof (raw512), params.begin ());
        break;
    }

    case 1024:
    {
        // These are the DH parameters that Ripple Labs has chosen for Ripple
        //
        std::uint8_t const raw1024 [] = {
            0x30, 0x81, 0x87, 0x02, 0x81, 0x81, 0x00, 0x86, 0xb1, 0x85, 0x36, 0x3d,
            0xbc, 0x0b, 0x03, 0xa5, 0xde, 0x53, 0x23, 0x4c, 0x59, 0xd4, 0x2b, 0x2e,
            0x88, 0xdf, 0x83, 0x8e, 0xab, 0xe9, 0xc9, 0x0f, 0x20, 0x5c, 0x3e, 0x8d,
            0x0e, 0x2c, 0xff, 0xcf, 0x3a, 0xfa, 0x71, 0x67, 0xb2, 0x90, 0xb5, 0x9e,
            0x13, 0x9f, 0xa3, 0x70, 0xb2, 0xdf, 0x8d, 0xa4, 0x91, 0xfb, 0x26, 0xe0,
            0x95, 0xd2, 0xf9, 0x3b, 0xa5, 0x1f, 0xe4, 0x88, 0x0f, 0x65, 0xfc, 0x8e,
            0x58, 0x47, 0x8c, 0x77, 0x93, 0x8c, 0x2d, 0x2a, 0xfa, 0x50, 0xb4, 0xc5,
            0x29, 0xba, 0x65, 0xc4, 0x39, 0xeb, 0x8a, 0xc5, 0x93, 0x39, 0xf9, 0x3c,
            0x15, 0x1e, 0x95, 0x82, 0x0d, 0x02, 0xff, 0x92, 0x4c, 0xc5, 0x07, 0x76,
            0x62, 0xaf, 0xdc, 0xc0, 0x96, 0x95, 0xcf, 0x61, 0x51, 0x17, 0x7c, 0x02,
            0x81, 0xdb, 0xc2, 0x6b, 0x07, 0x03, 0x96, 0x39, 0xcc, 0xde, 0xc9, 0xcd,
            0x5d, 0x77, 0x3b, 0x02, 0x01, 0x02
        };
        params.resize (sizeof (raw1024));
        std::copy (raw1024, raw1024 + sizeof (raw1024), params.begin ());
        break;
    }

    case 2048:
    {
        // These are the DH parameters that Ripple Labs has chosen for Ripple
        //
        std::uint8_t const raw2048 [] = {
            0x30, 0x82, 0x01, 0x08, 0x02, 0x82, 0x01, 0x01, 0x00, 0x8f, 0xca, 0x66,
            0x85, 0x33, 0xcb, 0xcf, 0x36, 0x27, 0xb2, 0x4c, 0xb8, 0x50, 0xb8, 0xf9,
            0x53, 0xf8, 0xb9, 0x2d, 0x1c, 0xa2, 0xad, 0x86, 0x58, 0x29, 0x3b, 0x88,
            0x3e, 0xf5, 0x65, 0xb8, 0xda, 0x22, 0xf4, 0x8b, 0x21, 0x12, 0x18, 0xf7,
            0x16, 0xcd, 0x7c, 0xc7, 0x3a, 0x2d, 0x61, 0xb7, 0x11, 0xf6, 0xb0, 0x65,
            0xa0, 0x5b, 0xa4, 0x06, 0x95, 0x28, 0xa4, 0x4f, 0x76, 0xc0, 0xeb, 0xfa,
            0x95, 0xdf, 0xbf, 0x19, 0x90, 0x64, 0x8f, 0x60, 0xd5, 0x36, 0xba, 0xab,
            0x0d, 0x5a, 0x5c, 0x94, 0xd5, 0xf7, 0x32, 0xd6, 0x2a, 0x76, 0x77, 0x83,
            0x10, 0xc4, 0x2f, 0x10, 0x96, 0x3e, 0x37, 0x84, 0x45, 0x9c, 0xef, 0x33,
            0xf6, 0xd0, 0x2a, 0xa7, 0xce, 0x0a, 0xce, 0x0d, 0xa1, 0xa7, 0x44, 0x5d,
            0x18, 0x3f, 0x4f, 0xa4, 0x23, 0x9c, 0x5d, 0x74, 0x4f, 0xee, 0xdf, 0xaa,
            0x0d, 0x0a, 0x52, 0x57, 0x73, 0xb1, 0xe4, 0xc5, 0x72, 0x93, 0x9d, 0x03,
            0xe9, 0xf5, 0x48, 0x8c, 0xd1, 0xe6, 0x7c, 0x21, 0x65, 0x4e, 0x16, 0x51,
            0xa3, 0x16, 0x51, 0x10, 0x75, 0x60, 0x37, 0x93, 0xb8, 0x15, 0xd6, 0x14,
            0x41, 0x4a, 0x61, 0xc9, 0x1a, 0x4e, 0x9f, 0x38, 0xd8, 0x2c, 0xa5, 0x31,
            0xe1, 0x87, 0xda, 0x1f, 0xa4, 0x31, 0xa2, 0xa4, 0x42, 0x1e, 0xe0, 0x30,
            0xea, 0x2f, 0x9b, 0x77, 0x91, 0x59, 0x3e, 0xd5, 0xd0, 0xc5, 0x84, 0x45,
            0x17, 0x19, 0x74, 0x8b, 0x18, 0xb0, 0xc1, 0xe0, 0xfc, 0x1c, 0xaf, 0xe6,
            0x2a, 0xef, 0x4e, 0x0e, 0x8a, 0x5c, 0xc2, 0x91, 0xb9, 0x2b, 0xf8, 0x17,
            0x8d, 0xed, 0x44, 0xaa, 0x47, 0xaa, 0x52, 0xa2, 0xdb, 0xb6, 0xf5, 0xa1,
            0x88, 0x85, 0xa1, 0xd5, 0x87, 0xb8, 0x07, 0xd3, 0x97, 0xbe, 0x37, 0x74,
            0x72, 0xf1, 0xa8, 0x29, 0xf1, 0xa7, 0x7d, 0x19, 0xc3, 0x27, 0x09, 0xcf,
            0x23, 0x02, 0x01, 0x02
        };
        params.resize (sizeof (raw2048));
        std::copy (raw2048, raw2048 + sizeof (raw2048), params.begin ());
        break;
    }

    default:
        break;
    };

    return params;
}

static
DH*
getDH (int keyLength)
{
    if (keyLength == 512 || keyLength == 1024 || keyLength == 2048)
    {
        static dh_ptr dh = make_DH(getRawDHParams (keyLength));
        return dh.get ();
    }
    else
    {
        beast::FatalError ("unsupported key length", __FILE__, __LINE__);
    }

    return nullptr;
}

static
DH*
tmp_dh_handler (SSL*, int, int key_length)
{
    return DHparams_dup (getDH (key_length));
}

static
bool
disallowRenegotiation (SSL const* ssl, bool isNew)
{
    // Do not allow a connection to renegotiate
    // more than once every 4 minutes

    static StaticData sd;
    std::lock_guard <std::mutex> lock (sd.lock);
    auto const expired (sd.set.clock().now() - std::chrono::minutes(4));

    // Remove expired entries
    for (auto iter (sd.set.chronological.begin ());
        (iter != sd.set.chronological.end ()) && (iter.when () <= expired);
        iter = sd.set.chronological.begin ())
    {
        sd.set.erase (iter);
    }

    auto iter = sd.set.find (ssl);
    if (iter != sd.set.end ())
    {
        if (! isNew)
        {
            // This is a renegotiation and the last negotiation was recent
            return true;
        }

        sd.set.touch (iter);
    }
    else
    {
        sd.set.emplace (ssl);
    }

    return false;
}

static
void
info_handler (SSL const* ssl, int event, int)
{
    if ((ssl->s3) && (event & SSL_CB_HANDSHAKE_START))
    {
        if (disallowRenegotiation (ssl, SSL_in_before (ssl)))
            ssl->s3->flags |= SSL3_FLAGS_NO_RENEGOTIATE_CIPHERS;
    }
}

static
std::string
error_message (std::string const& what,
    boost::system::error_code const& ec)
{
    std::stringstream ss;
    ss <<
        what << ": " <<
        ec.message() <<
        " (" << ec.value() << ")";
    return ss.str();
}

static
void
initCommon (boost::asio::ssl::context& context)
{
    context.set_options (
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::no_sslv3 |
        boost::asio::ssl::context::single_dh_use);

    SSL_CTX_set_tmp_dh_callback (
        context.native_handle (),
        tmp_dh_handler);

    SSL_CTX_set_info_callback (
        context.native_handle (),
        info_handler);
}

static
void
initAnonymous (
    boost::asio::ssl::context& context, std::string const& cipherList)
{
    initCommon(context);
    int const result = SSL_CTX_set_cipher_list (
        context.native_handle (),
        cipherList.c_str ());
    if (result != 1)
        Throw<std::invalid_argument> ("SSL_CTX_set_cipher_list failed");

    using namespace openssl;

    evp_pkey_ptr pkey = evp_pkey_new();
    evp_pkey_assign_rsa (pkey.get(), rsa_generate_key (2048));

    x509_ptr cert = x509_new();
    x509_set_pubkey (cert.get(), pkey.get());
    x509_sign       (cert.get(), pkey.get());

    SSL_CTX* const ctx = context.native_handle();
    ssl_ctx_use_certificate (ctx, cert);
    ssl_ctx_use_privatekey  (ctx, pkey);
}

static
void
initAuthenticated (boost::asio::ssl::context& context,
    std::string key_file, std::string cert_file, std::string chain_file)
{
    initCommon (context);

    SSL_CTX* const ssl = context.native_handle ();

    bool cert_set = false;

    if (! cert_file.empty ())
    {
        boost::system::error_code ec;

        context.use_certificate_file (
            cert_file, boost::asio::ssl::context::pem, ec);

        if (ec)
        {
            beast::FatalError (error_message (
                "Problem with SSL certificate file.", ec).c_str(),
                __FILE__, __LINE__);
        }

        cert_set = true;
    }

    if (! chain_file.empty ())
    {
        // VFALCO Replace fopen() with RAII
        FILE* f = fopen (chain_file.c_str (), "r");

        if (!f)
        {
            beast::FatalError (error_message (
                "Problem opening SSL chain file.", boost::system::error_code (errno,
                boost::system::generic_category())).c_str(),
                __FILE__, __LINE__);
        }

        try
        {
            for (;;)
            {
                X509* const x = PEM_read_X509 (f, nullptr, nullptr, nullptr);

                if (x == nullptr)
                    break;

                if (! cert_set)
                {
                    if (SSL_CTX_use_certificate (ssl, x) != 1)
                        beast::FatalError ("Problem retrieving SSL certificate from chain file.",
                            __FILE__, __LINE__);

                    cert_set = true;
                }
                else if (SSL_CTX_add_extra_chain_cert (ssl, x) != 1)
                {
                    X509_free (x);
                    beast::FatalError ("Problem adding SSL chain certificate.",
                        __FILE__, __LINE__);
                }
            }

            fclose (f);
        }
        catch (std::exception const&)
        {
            fclose (f);
            beast::FatalError ("Reading the SSL chain file generated an exception.",
                __FILE__, __LINE__);
        }
    }

    if (! key_file.empty ())
    {
        boost::system::error_code ec;

        context.use_private_key_file (key_file,
            boost::asio::ssl::context::pem, ec);

        if (ec)
        {
            beast::FatalError (error_message (
                "Problem using the SSL private key file.", ec).c_str(),
                __FILE__, __LINE__);
        }
    }

    if (SSL_CTX_check_private_key (ssl) != 1)
    {
        beast::FatalError ("Invalid key in SSL private key file.",
            __FILE__, __LINE__);
    }
}

} // detail
} // openssl

//------------------------------------------------------------------------------
std::shared_ptr<boost::asio::ssl::context>
make_SSLContext()
{
    std::shared_ptr<boost::asio::ssl::context> context =
        std::make_shared<boost::asio::ssl::context> (
            boost::asio::ssl::context::sslv23);
    // By default, allow anonymous DH.
    openssl::detail::initAnonymous (
        *context, "ALL:!LOW:!EXP:!MD5:@STRENGTH");
    // VFALCO NOTE, It seems the WebSocket context never has
    // set_verify_mode called, for either setting of WEBSOCKET_SECURE
    context->set_verify_mode (boost::asio::ssl::verify_none);
    return context;
}

std::shared_ptr<boost::asio::ssl::context>
make_SSLContextAuthed (std::string const& key_file,
    std::string const& cert_file, std::string const& chain_file)
{
    std::shared_ptr<boost::asio::ssl::context> context =
        std::make_shared<boost::asio::ssl::context> (
            boost::asio::ssl::context::sslv23);
    openssl::detail::initAuthenticated(*context,
        key_file, cert_file, chain_file);
    return context;
}

} // ripple

