#pragma once

#include <xrpl/basics/Slice.h>

#include <cstddef>
#include <cstdint>

namespace xrpl {

/**
 * Structural validation for MPTStructuredData schemas and data.
 *
 * A schema is a sequence of one-byte type codes describing a packed
 * record layout. Data is the corresponding values packed back-to-back
 * in schema order: big-endian integers, VL-prefixed str/bin (standard
 * XRPL variable-length encoding, byte counts), arrays as a one-byte
 * element count followed by that many elements, and tuples with no
 * framing of their own (the markers exist only in the schema).
 *
 * These functions validate structure only. No values are materialized
 * and no semantic checks are performed beyond the bool 0x00/0x01 rule.
 */

/**
 * Schema type codes. Codes not listed here are reserved and malformed.
 */
enum class SchemaType : std::uint8_t {
    Boolean = 0x01,   // 1 byte, 0x00 or 0x01
    U8 = 0x02,        // 1 byte
    U16 = 0x03,       // 2 bytes, big-endian
    U32 = 0x04,       // 4 bytes, big-endian
    U64 = 0x05,       // 8 bytes, big-endian
    U128 = 0x06,      // 16 bytes
    U256 = 0x07,      // 32 bytes
    Xfl = 0x08,       // 8 bytes, XLS-17 floating point
    Account = 0x09,   // 20 bytes, AccountID
    Currency = 0x0A,  // 20 bytes, 160-bit currency code
    H160 = 0x0B,      // 20 bytes
    H256 = 0x0C,      // 32 bytes
    PubKey = 0x0D,    // 33 bytes, compressed public key
    Str = 0x0E,       // VL prefix + UTF-8 bytes
    Bin = 0x0F,       // VL prefix + opaque bytes
    Array = 0x20,     // followed by one element type; data: count byte + elements
    TupleOpen = 0x30,
    TupleClose = 0x31,
};

/**
 * Maximum array/tuple nesting depth a schema may declare.
 */
constexpr std::size_t kMaxSchemaDepth = 8;

/**
 * Check that a schema is well-formed.
 *
 * Well-formed means: non-empty, no longer than kMaxSchemaLength, every
 * code is a known SchemaType, every array code is followed by an
 * element type, every tupleOpen has a matching tupleClose, and nesting
 * does not exceed kMaxSchemaDepth.
 *
 * An array element type must also read at least one data byte. A
 * zero-width element (an empty tuple, or a tuple of empty tuples) would
 * let the one-byte element count buy up to 255 replays of it without
 * consuming input, and nesting arrays would multiply that per level.
 */
[[nodiscard]] bool
isWellFormedSchema(Slice schema);

/**
 * Check that data decodes exactly against a well-formed schema.
 *
 * Walks both inputs with two cursors. Every field must be present at
 * its declared width (VL-prefixed for str/bin), array counts must be
 * consistent with the remaining input, and both cursors must end
 * exactly exhausted: truncated data and trailing bytes both fail.
 *
 * The schema is re-validated during the walk, so a malformed schema
 * returns false rather than misreading data.
 */
[[nodiscard]] bool
dataMatchesSchema(Slice schema, Slice data);

}  // namespace xrpl
