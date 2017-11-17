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
#include <ripple/net/RegisterSSLCerts.h>
#if BEAST_WINDOWS
#include <wincrypt.h>
#endif

namespace ripple {

void
registerSSLCerts(boost::asio::ssl::context& ctx, boost::system::error_code& ec)
{
#if BEAST_WINDOWS
    HCERTSTORE hStore = CertOpenSystemStore(0, "ROOT");
    if (hStore == NULL)
    {
        return;
    }

    X509_STORE* store = X509_STORE_new();
    PCCERT_CONTEXT pContext = NULL;
    while ((pContext = CertEnumCertificatesInStore(hStore, pContext)) != NULL)
    {
        X509* x509 = d2i_X509(
            NULL,
            (const unsigned char**)&pContext->pbCertEncoded,
            pContext->cbCertEncoded);
        if (x509 != NULL)
        {
            X509_STORE_add_cert(store, x509);
            X509_free(x509);
        }
    }

    CertFreeCertificateContext(pContext);
    CertCloseStore(hStore, 0);

    SSL_CTX_set_cert_store(ctx.native_handle(), store);
#else

    ctx.set_default_verify_paths(ec);
#endif
}

}  // namespace ripple
