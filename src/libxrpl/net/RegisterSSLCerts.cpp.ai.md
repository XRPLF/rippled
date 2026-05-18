# `RegisterSSLCerts.cpp` — Bridging OS Trust Stores to OpenSSL

`RegisterSSLCerts.cpp` solves a fundamental portability problem in SSL certificate trust: OpenSSL does not automatically consult the Windows certificate store, so TLS connections that would succeed on Linux or macOS silently fail peer verification on Windows unless the trust anchors are loaded explicitly. This single-function translation unit handles that gap while keeping non-Windows platforms on the zero-cost path provided by Boost.Asio itself.

## The One Public Function

`registerSSLCerts(ctx, ec, j)` populates a `boost::asio::ssl::context` with the platform's trusted root certificates. Its three parameters follow the XRPL convention: a Boost.Asio SSL context to configure, an error code to set on failure (rather than throwing), and a `beast::Journal` for structured logging. The function never throws — errors propagate through `ec` or are logged as warnings and skipped.

## Non-Windows Path

On Linux and macOS the entire body is a single line:

```cpp
ctx.set_default_verify_paths(ec);
```

Boost.Asio delegates this to OpenSSL's `SSL_CTX_set_default_verify_paths`, which searches the standard system locations (`/etc/ssl/certs`, the directory pointed to by `SSL_CERT_DIR`, etc.). No bridging is required because OpenSSL was built to understand those locations natively.

## Windows Path

Windows stores its trusted root certificates in the CryptoAPI "ROOT" system store, in a format and location entirely opaque to OpenSSL. The Windows code path manually bridges the two APIs.

**Opening the CryptoAPI store.** `CertOpenSystemStore(0, "ROOT")` retrieves a handle to the Windows root CA store. The handle is immediately wrapped in a `std::unique_ptr` with a custom deleter (`CertCloseStore`) so the store is released regardless of how the function exits. If this call fails, `GetLastError()` is translated into a `boost::system::error_code` in the `system_category` and the function returns early.

**Creating an empty OpenSSL trust store.** `X509_STORE_new()` allocates a fresh `X509_STORE` (also wrapped in a `unique_ptr` with `X509_STORE_free`). This will accumulate the translated certificates before being installed into the SSL context. A failure here pulls the OpenSSL error via `ERR_get_error()` and assigns it to `ec` in Asio's SSL error category.

**Iterating and translating certificates.** `CertEnumCertificatesInStore` walks every certificate in the Windows store. Each certificate's encoded bytes are in DER format — the same wire encoding OpenSSL uses — so `d2i_X509` can decode them directly without a format conversion. The decoded `X509*` is wrapped in a `unique_ptr<X509, decltype(X509_free)*>`. Per-cert failures (decode or store-add errors) are non-fatal: the loop logs a warning via the journal, clears the OpenSSL error queue, and continues to the next certificate. This means a single corrupt or unsupported certificate in the Windows store doesn't abort the whole population process.

**Ownership transfer on store-add.** `X509_STORE_add_cert` follows an unusual ownership convention: on success, the store takes ownership of the `X509*`. The code handles this explicitly — `x509.release()` is called only when `X509_STORE_add_cert` returns `1`, preventing the `unique_ptr` destructor from double-freeing the certificate. On failure, the `unique_ptr` retains ownership and cleans up normally.

**Installing the store into the SSL context.** `SSL_CTX_set_cert_store` replaces the SSL context's existing trust store with the newly built one. This also transfers ownership, so `store.release()` is called to prevent the `unique_ptr` from freeing memory that is now owned by the context.

## The Macro Collision Problem

The file ends with a block of `#undef` directives that is easy to overlook but critical for unity builds:

```cpp
#undef X509_NAME
#undef X509_EXTENSIONS
#undef X509_CERT_PAIR
#undef PKCS7_ISSUER_AND_SERIAL
#undef OCSP_REQUEST
#undef OCSP_RESPONSE
```

`<wincrypt.h>` defines these names as macros, and OpenSSL uses the same names as struct identifiers and function names. Both headers are included in this translation unit, meaning the macros shadow the OpenSSL declarations. This is tolerable within a single `.cpp` file, but in a unity build — where multiple `.cpp` files are concatenated into one compilation unit — the macros would leak into subsequent files and corrupt the OpenSSL API. The `#undef`s at the bottom of the file, outside the `xrpl` namespace, surgically remove the pollution after it is no longer needed.

## How It Is Used

The primary caller is `HTTPClientSSLContext` (in `include/xrpl/net/HTTPClientSSLContext.h`), which calls `registerSSLCerts` in its constructor when no explicit verify-file is provided. If `registerSSLCerts` sets `ec` and no custom verify-directory is configured either, `HTTPClientSSLContext` converts the error into a `std::runtime_error` — failing fast rather than silently accepting an SSL context that cannot verify peers.