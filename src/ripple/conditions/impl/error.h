//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#ifndef RIPPLE_CONDITIONS_ERROR_H
#define RIPPLE_CONDITIONS_ERROR_H

#include <system_error>
#include <string>

namespace ripple {
namespace cryptoconditions {
namespace der {

/** Error types for asn.1 der coders
 */
enum class Error {
    /// Integer would not fit in the bounds of the specified type
    integerBounds = 1,
    /** There is more content data in a group than expected. For example: after
        decoding a group from a slice, the slice is not empty.
     */  
    longGroup,
    /** There is less content data in a group than expected. For example: trying to
        decode a string of length 10 from a slice of length 9.
     */
    shortGroup,
    /// Encoding is not a valid der encoding
    badDerEncoding,
    /// This implementation only supports tag numbers that will fit in a
    /// std::uint64_t
    tagOverflow,
    /// A decoded preamble did not match an expected preamble
    preambleMismatch,
    /// A decoded contentLength did not match an expected contentLength
    contentLengthMismatch,
    /// Choice tag did not match a known type
    unknownChoiceTag,
    /// Serialization exceeds implementation limit
    largeSize,
    /// Specified preimage exceeds implementation limit
    preimageTooLong,
    /// Specified rsa modulus size is out of range (129 and 512 bytes, inclusive)
    rsaModulusSizeRangeError,
    /// Requested type not supported
    unsupportedType,
    /// Supported by der, but not this implementation
    unsupported,
    /** Programming error. For example: detecting more pops than pushes on the
        group stack.
     */ 
    logicError
};

/** Convert an error enum to an std::error_code
 */
std::error_code
make_error_code(Error e);
}  // der
}  // cryptoconditions
}  // ripple

namespace std
{

template<>
struct is_error_code_enum<ripple::cryptoconditions::der::Error>
{
    static bool const value = true;
};

} // std


#endif
