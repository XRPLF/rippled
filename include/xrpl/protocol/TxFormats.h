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

#ifndef XRPL_PROTOCOL_TXFORMATS_H_INCLUDED
#define XRPL_PROTOCOL_TXFORMATS_H_INCLUDED

#include <xrpl/protocol/KnownFormats.h>

namespace ripple {

/** Transaction type identifiers.

    These are part of the binary message format.

    @ingroup protocol
*/
/** Transaction type identifiers

    Each ledger object requires a unique type identifier, which is stored
    within the object itself; this makes it possible to iterate the entire
    ledger and determine each object's type and verify that the object you
    retrieved from a given hash matches the expected type.

    @warning Since these values are included in transactions, which are signed
             objects, and used by the code to determine the type of transaction
             being invoked, they are part of the protocol. **Changing them
             should be avoided because without special handling, this will
             result in a hard fork.**

    @note When retiring types, the specific values should not be removed but
          should be marked as [[deprecated]]. This is to avoid accidental
          reuse of identifiers.

    @todo The C++ language does not enable checking for duplicate values
          here. If it becomes possible then we should do this.

    @ingroup protocol
*/
// clang-format off
enum TxType : std::uint16_t
{

#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define TRANSACTION(tag, value, ...) tag = value,

#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")

    /** This transaction type is deprecated; it is retained for historical purposes. */
    ttNICKNAME_SET [[deprecated("This transaction type is not supported and should not be used.")]] = 6,

    /** This transaction type is deprecated; it is retained for historical purposes. */
    ttCONTRACT [[deprecated("This transaction type is not supported and should not be used.")]] = 9,

    /** This identifier was never used, but the slot is reserved for historical purposes. */
    ttSPINAL_TAP [[deprecated("This transaction type is not supported and should not be used.")]] = 11,

    /** This transaction type installs a hook. */
    ttHOOK_SET [[maybe_unused]] = 22,
};
// clang-format on

/** Manages the list of known transaction formats.
 */
class TxFormats : public KnownFormats<TxType, TxFormats>
{
private:
    /** Create the object.
        This will load the object with all the known transaction formats.
    */
    TxFormats();

public:
    static TxFormats const&
    getInstance();
};

}  // namespace ripple

#endif
