/**
 * Server's hostname.
 *
 * This name is used in the validated public data and must match what the
 * clients think it is.
 */
exports.serverName = "local.auth1.ripple.com";

/**
 * Reference to the server's RSA key.
 */
exports.rsa = require('./rsa.json');

/**
 * Domains this authentication server accepts cross-origin requests from.
 *
 * exports.allowedOriginDomains = ['example.com', 'client.example.net'];
 *
 * You can allow requests from any origin using:
 *
 * exports.allowedOriginDomains = ['*'];
 */
//exports.allowedOriginDomains = ['ripple.com'];

/**
 * Whether to allow subdomains of the above.
 */
exports.allowSubdomainOrigin = true;

/**
 * Whether to allow origins that aren't using SSL.
 */
exports.allowNonSslOrigin = false;
