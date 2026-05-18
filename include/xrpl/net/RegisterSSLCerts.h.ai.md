# `include/xrpl/net/RegisterSSLCerts.h`

This header declares a single cross-platform utility function, `registerSSLCerts`, responsible for populating a Boost.Asio SSL context with the operating system's trusted root certificates. It exists because Boost.Asio does not abstract platform-specific certificate stores on its own — specifically, it has no built-in mechanism for reading the Windows CryptoAPI trust store — so XRPL provides this thin adapter to give the rest of the networking stack a uniform interface for setting up TLS trust anchors.

## The Function

```cpp
void registerSSLCerts(boost::asio::ssl::context&, boost::system::error_code&, beast::Journal);
```

The function follows Boost's error-handling convention: failure is reported through an output `error_code` rather than by throwing, which lets callers decide whether a cert-registration failure is fatal. The `beast::Journal` parameter enables diagnostic logging during the potentially fallible certificate enumeration on Windows.

## Platform Implementations

The implementation in `src/libxrpl/net/RegisterSSLCerts.cpp` diverges on `BOOST_OS_WINDOWS`.

**POSIX (Linux/macOS):** The implementation is a single line — `ctx.set_default_verify_paths(ec)` — which instructs OpenSSL to search standard OS locations (e.g., `/etc/ssl/certs` on Linux, system keychain paths on macOS). The `// NOLINTNEXTLINE(bugprone-unused-return-value)` comment acknowledges that Boost.Asio's overload returns a value that's intentionally discarded in favor of the `error_code` path.

**Windows:** The function manually bridges between two trust store representations. It opens the Windows "ROOT" system certificate store via `CertOpenSystemStore`, then allocates a new OpenSSL `X509_STORE`. For each DER-encoded certificate returned by `CertEnumCertificatesInStore`, it calls `d2i_X509` to decode it from the Windows binary format into an OpenSSL `X509*` object, adds it to the OpenSSL store with `X509_STORE_add_cert`, and finally installs that store into the SSL context via `SSL_CTX_set_cert_store`. Both the Windows HCERTSTORE and the OpenSSL `X509_STORE` are managed through `std::unique_ptr` with custom deleters, ensuring clean teardown on any error path.

An important non-obvious detail lives at the end of the `.cpp` file: a cluster of `#undef` directives removes macros that `<wincrypt.h>` defines with the same names as OpenSSL's X.509 types (`X509_NAME`, `X509_EXTENSIONS`, etc.). Without these undefs, including this translation unit into a unity build would silently corrupt OpenSSL symbol lookups in every subsequent translation unit that uses X.509 types — a subtle, hard-to-diagnose build failure. The undefs are placed after the closing `}` to ensure they take effect after the function body but cannot affect this TU itself.

## Error Handling Design

Individual certificate failures on Windows are non-fatal by design. If `d2i_X509` fails or `X509_STORE_add_cert` fails for a particular certificate, the function logs a warning through the `beast::Journal` and continues to the next certificate in the store. This is deliberate: a partially-populated trust store is still useful, and a single malformed certificate in the OS store should not prevent all outbound TLS connections. Fatal errors — failure to open the store or failure to allocate the OpenSSL store — set `ec` and return early.

## Primary Consumer

`HTTPClientSSLContext` (in `include/xrpl/net/HTTPClientSSLContext.h`) is the direct caller. Its constructor invokes `registerSSLCerts` on its `boost::asio::ssl::context` unless a specific verify file has been provided. If `registerSSLCerts` returns an error but a custom `sslVerifyDir` is also configured, the error is silently tolerated — the directory path is added as a fallback. If neither a verify file nor a verify directory is available and `registerSSLCerts` fails, the constructor throws `std::runtime_error`, making TLS context construction fail loudly rather than silently using an empty trust store.