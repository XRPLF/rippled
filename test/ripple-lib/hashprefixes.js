'use strict';

// TODO: move in helpers from serializedtypes to utils
function toBytes(n) {
  return [n >>> 24, n >>> 16 & 0xff, n >>> 8 & 0xff, n & 0xff];
}

/**
 * Prefix for hashing functions.
 *
 * These prefixes are inserted before the source material used to
 * generate various hashes. This is done to put each hash in its own
 * "space." This way, two different types of objects with the
 * same binary data will produce different hashes.
 *
 * Each prefix is a 4-byte value with the last byte set to zero
 * and the first three bytes formed from the ASCII equivalent of
 * some arbitrary string. For example "TXN".
 */

// transaction plus signature to give transaction ID
exports.HASH_TX_ID = 0x54584E00; // 'TXN'
// transaction plus metadata
exports.HASH_TX_NODE = 0x534E4400; // 'TND'
// inner node in tree
exports.HASH_INNER_NODE = 0x4D494E00; // 'MIN'
// leaf node in tree
exports.HASH_LEAF_NODE = 0x4D4C4E00; // 'MLN'
// inner transaction to sign
exports.HASH_TX_SIGN = 0x53545800; // 'STX'
// inner transaction to sign (TESTNET)
exports.HASH_TX_SIGN_TESTNET = 0x73747800; // 'stx'
// inner transaction to multisign
exports.HASH_TX_MULTISIGN = 0x534D5400; // 'SMT'

Object.keys(exports).forEach(function (k) {
  exports[k + '_BYTES'] = toBytes(exports[k]);
});

