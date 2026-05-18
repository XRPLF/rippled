# `include/xrpl/basics/make_SSLContext.h`

This header is the public interface for creating TLS/SSL contexts used across the XRPL node's two distinct network-facing subsystems: the peer-to-peer overlay network and the HTTP/WebSocket RPC server. It declares exactly two factory functions, keeping all OpenSSL implementation details confined to the corresponding `.cpp` translation unit.

## The Two Security Modes

The header reflects a deliberate architectural split in how XRPL secures its connections:

`make_SSLContext(cipherList)` creates a context for **anonymous TLS**, used between validator/relay nodes in the overlay network. Peer identity in the overlay is not established by TLS certificates — it is established by XRPL's own cryptographic node identity scheme. TLS here serves purely as a transport encryption layer, so a self-signed ephemeral certificate is sufficient. The returned context is configured with `verify_none`, meaning neither side validates the other's certificate.

`make_SSLContextAuthed(keyFile, certFile, chainFile, cipherList)` creates a context for **certificate-authenticated TLS**, used by the RPC/HTTP server when an operator supplies their own key and certificate files. This is appropriate for external-facing endpoints where clients (wallets, applications, monitoring tools) need server identity assurance. The `chainFile` parameter supports intermediate certificate chains, allowing operators to use CA-issued certificates without embedding the full chain in the certificate file itself.

## Shared Security Baseline

Both functions delegate to an internal `get_context()` helper that enforces a shared hardened baseline regardless of authentication mode:

- SSLv2, SSLv3, TLSv1.0, and TLSv1.1 are all disabled — only TLS 1.2 and above are accepted.
- TLS compression is disabled (mitigates CRIME-class attacks).
- TLS renegotiation is disabled via `SSL_OP_NO_RENEGOTIATION`, which guards against CVE-2021-3499 on older OpenSSL versions.
- The default cipher list `"TLSv1.2:!CBC:!DSS:!PSK:!eNULL:!aNULL"` excludes block-cipher modes (CBC), DSS-based suites, pre-shared key suites, and any suites lacking encryption or authentication.
- Pre-generated 2048-bit Diffie-Hellman parameters are embedded directly in the binary (generated via `openssl dhparam 2048`). Hardcoding these avoids the startup latency of runtime generation and is safe because DH parameters are not secret — only the ephemeral DH keypairs need to remain secret, and those are handled by `single_dh_use`.

## Self-Signed Certificate Design

The anonymous context's certificate generation reveals several non-obvious defensive choices. The RSA key and X.509 certificate are created once as function-local statics, meaning they are shared across every call to `make_SSLContext` within a process lifetime. This is intentional: the certificate is ephemeral by design (it is regenerated on each server restart), so there is no value in creating multiple distinct certificates per connection.

The certificate validity start time is backdated by 25 hours. This prevents a network observer from inferring the server's precise startup time from the certificate's `notBefore` field — a subtle privacy consideration that reduces side-channel information leakage. The certificate is set valid for two years from creation, and carries a 128-bit randomly generated serial number to avoid collisions in logs or caches.

X.509v3 extensions are set to mark the certificate as a non-CA leaf (`CA:FALSE`), restrict key usage to `digitalSignature`, and allow the certificate for both `serverAuth` and `clientAuth` extended key usage — the latter because overlay connections are mutually encrypted (not strictly client/server asymmetric).

## Error Handling Philosophy

All failures in context construction call `LogicError()`, which terminates the process. This is appropriate because SSL context creation is a startup-time prerequisite: if the TLS layer cannot be initialized (e.g., invalid cipher list, missing key file, mismatched key and certificate), the node cannot operate safely and there is no meaningful recovery path. The authenticated path additionally calls `SSL_CTX_check_private_key` to verify that the loaded private key actually corresponds to the loaded certificate before returning, catching misconfigured deployments at startup rather than at connection time.

## Callers

In `OverlayImpl.cpp`, `make_SSLContext("")` (empty cipher list, falling back to the default) is called unconditionally during overlay setup — peer connections always use anonymous TLS. In `ServerHandler.cpp`, the choice between the two functions is made at port configuration time: if any of `ssl_key`, `ssl_cert`, or `ssl_chain` are populated, `make_SSLContextAuthed` is called; otherwise, `make_SSLContext` is used. Operators can override the cipher list on a per-port basis via the `ssl_ciphers` config directive, which is passed through as the `cipherList` argument.