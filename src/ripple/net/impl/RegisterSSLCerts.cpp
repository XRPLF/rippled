//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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
#include <ripple/net/RegisterSSLCerts.h>
#include <boost/predef.h>

#if BOOST_OS_WINDOWS
#include <boost/asio/ssl/error.hpp>
#include <boost/system/error_code.hpp>
#include <memory>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <wincrypt.h>
#endif

namespace ripple {

void
registerSSLCerts(
    boost::asio::ssl::context& ctx,
    boost::system::error_code& ec,
    beast::Journal j)
{
#if BOOST_OS_WINDOWS
    auto certStoreDelete = [](void* h) {
        if (h != nullptr)
            CertCloseStore(h, 0);
    };
    std::unique_ptr<void, decltype(certStoreDelete)> hStore{
        CertOpenSystemStore(0, "ROOT"), certStoreDelete};

    if (!hStore)
    {
        ec = boost::system::error_code(
            GetLastError(), boost::system::system_category());
        return;
    }

    ERR_clear_error();

    std::unique_ptr<X509_STORE, decltype(X509_STORE_free)*> store{
        X509_STORE_new(), X509_STORE_free};

    if (!store)
    {
        ec = boost::system::error_code(
            static_cast<int>(::ERR_get_error()),
            boost::asio::error::get_ssl_category());
        return;
    }

    auto warn = [&](std::string const& mesg) {
        // Buffer based on asio recommended size
        char buf[256];
        ::ERR_error_string_n(ec.value(), buf, sizeof(buf));
        JLOG(j.warn()) << mesg << " " << buf;
        ::ERR_clear_error();
    };

    PCCERT_CONTEXT pContext = NULL;
    while ((pContext = CertEnumCertificatesInStore(hStore.get(), pContext)) !=
           NULL)
    {
        const unsigned char* pbCertEncoded = pContext->pbCertEncoded;
        std::unique_ptr<X509, decltype(X509_free)*> x509{
            d2i_X509(NULL, &pbCertEncoded, pContext->cbCertEncoded), X509_free};
        if (!x509)
        {
            warn("Error decoding certificate");
            continue;
        }

        if (X509_STORE_add_cert(store.get(), x509.get()) != 1)
        {
            warn("Error adding certificate");
        }
        else
        {
            // Successfully adding to the store took ownership
            x509.release();
        }
    }

    // This takes ownership of the store
    SSL_CTX_set_cert_store(ctx.native_handle(), store.release());

#else
    ctx.set_default_verify_paths(ec);
#endif
}

}  // namespace ripple

// There is a very unpleasant interaction between <wincrypt> and
// openssl x509 types (namely the former has macros that stomp
// on the latter), these undefs allow this TU to be safely used in
// unity builds without messing up subsequent TUs.
#if BOOST_OS_WINDOWS
#undef X509_NAME
#undef X509_EXTENSIONS
#undef X509_CERT_PAIR
#undef PKCS7_ISSUER_AND_SERIAL
#undef OCSP_REQUEST
#undef OCSP_RESPONSE
#endif
