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

#ifndef RIPPLE_PROTOCOL_LEDGERFORMATS_H_INCLUDED
#define RIPPLE_PROTOCOL_LEDGERFORMATS_H_INCLUDED

#include <xrpl/protocol/KnownFormats.h>

#include <map>

namespace ripple {

/** Identifiers for on-ledger objects.

    Each ledger object requires a unique type identifier, which is stored
    within the object itself; this makes it possible to iterate the entire
    ledger and determine each object's type and verify that the object you
    retrieved from a given hash matches the expected type.

    @warning Since these values are stored inside objects stored on the ledger
             they are part of the protocol. **Changing them should be avoided
             because without special handling, this will result in a hard
   fork.**

    @note Values outside this range may be used internally by the code for
          various purposes, but attempting to use such values to identify
          on-ledger objects will results in an invariant failure.

    @note When retiring types, the specific values should not be removed but
          should be marked as [[deprecated]]. This is to avoid accidental
          reuse of identifiers.

    @todo The C++ language does not enable checking for duplicate values
          here. If it becomes possible then we should do this.

    @ingroup protocol
*/
// clang-format off
enum LedgerEntryType : std::uint16_t
{

#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY

#define LEDGER_ENTRY(tag, value, ...) tag = value,

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")

    //---------------------------------------------------------------------------
    /** A special type, matching any ledger entry type.

        The value does not represent a concrete type, but rather is used in
        contexts where the specific type of a ledger object is unimportant,
        unknown or unavailable.

        Objects with this special type cannot be created or stored on the
        ledger.

        \sa keylet::unchecked
    */
    ltANY = 0,

    /** A special type, matching any ledger type except directory nodes.

        The value does not represent a concrete type, but rather is used in
        contexts where the ledger object must not be a directory node but
        its specific type is otherwise unimportant, unknown or unavailable.

        Objects with this special type cannot be created or stored on the
        ledger.

        \sa keylet::child
     */
    ltCHILD = 0x1CD2,

    //---------------------------------------------------------------------------
    /** A legacy, deprecated type.

        \deprecated **This object type is not supported and should not be used.**
                    Support for this type of object was never implemented.
                    No objects of this type were ever created.
     */
    ltNICKNAME [[deprecated("This object type is not supported and should not be used.")]] = 0x006e,

    /** A legacy, deprecated type.

        \deprecated **This object type is not supported and should not be used.**
                    Support for this type of object was never implemented.
                    No objects of this type were ever created.
     */
    ltCONTRACT [[deprecated("This object type is not supported and should not be used.")]] = 0x0063,

    /** A legacy, deprecated type.

        \deprecated **This object type is not supported and should not be used.**
                    Support for this type of object was never implemented.
                    No objects of this type were ever created.
     */
    ltGENERATOR_MAP [[deprecated("This object type is not supported and should not be used.")]]  = 0x0067,
};
// clang-format off

/** Ledger object flags.

    These flags are specified in ledger objects and modify their behavior.

    @warning Ledger object flags form part of the protocol. **Changing them
             should be avoided because without special handling, this will
             result in a hard fork.**

    @ingroup protocol
*/

#pragma push_macro("XMACRO")
#pragma push_macro("TO_VALUE")
#pragma push_macro("VALUE_TO_MAP")
#pragma push_macro("NULL_NAME")
#pragma push_macro("TO_MAP")
#pragma push_macro("ALL_LEDGER_FLAGS")

#undef XMACRO
#undef TO_VALUE
#undef VALUE_TO_MAP
#undef NULL_NAME
#undef TO_MAP

// clang-format off
#undef ALL_LEDGER_FLAGS

#define XMACRO(LEDGER_OBJECT, LSF_FLAG, LSF_FLAG2)                   \
    LEDGER_OBJECT(AccountRoot,                                    \
        LSF_FLAG(lsfPasswordSpent, 0x00010000)                    \
        LSF_FLAG(lsfRequireDestTag, 0x00020000)                   \
        LSF_FLAG(lsfRequireAuth, 0x00040000)                      \
        LSF_FLAG(lsfDisallowXRP, 0x00080000)                      \
        LSF_FLAG(lsfDisableMaster, 0x00100000)                    \
        LSF_FLAG(lsfNoFreeze, 0x00200000)                         \
        LSF_FLAG(lsfGlobalFreeze, 0x00400000)                     \
        LSF_FLAG(lsfDefaultRipple, 0x00800000)                    \
        LSF_FLAG(lsfDepositAuth, 0x01000000)                      \
        LSF_FLAG(lsfDisallowIncomingNFTokenOffer, 0x04000000)     \
        LSF_FLAG(lsfDisallowIncomingCheck, 0x08000000)            \
        LSF_FLAG(lsfDisallowIncomingPayChan, 0x10000000)          \
        LSF_FLAG(lsfDisallowIncomingTrustline, 0x20000000)        \
        LSF_FLAG(lsfAllowTrustLineLocking, 0x40000000)            \
        LSF_FLAG(lsfAllowTrustLineClawback, 0x80000000))          \
                                                                  \
    LEDGER_OBJECT(Offer,                                          \
        LSF_FLAG(lsfPassive, 0x00010000)                          \
        LSF_FLAG(lsfSell, 0x00020000)                             \
        LSF_FLAG(lsfHybrid, 0x00040000))                          \
                                                                  \
    LEDGER_OBJECT(RippleState,                                    \
        LSF_FLAG(lsfLowReserve, 0x00010000)                       \
        LSF_FLAG(lsfHighReserve, 0x00020000)                      \
        LSF_FLAG(lsfLowAuth, 0x00040000)                          \
        LSF_FLAG(lsfHighAuth, 0x00080000)                         \
        LSF_FLAG(lsfLowNoRipple, 0x00100000)                      \
        LSF_FLAG(lsfHighNoRipple, 0x00200000)                     \
        LSF_FLAG(lsfLowFreeze, 0x00400000)                        \
        LSF_FLAG(lsfHighFreeze, 0x00800000)                       \
        LSF_FLAG(lsfAMMNode, 0x01000000)                          \
        LSF_FLAG(lsfLowDeepFreeze, 0x02000000)                    \
        LSF_FLAG(lsfHighDeepFreeze, 0x04000000))                  \
                                                                  \
    LEDGER_OBJECT(SignerList,                                     \
        LSF_FLAG(lsfOneOwnerCount, 0x00010000))                   \
                                                                  \
    LEDGER_OBJECT(DirNode,                                        \
        LSF_FLAG(lsfNFTokenBuyOffers, 0x00000001)                 \
        LSF_FLAG(lsfNFTokenSellOffers, 0x00000002))               \
                                                                  \
    LEDGER_OBJECT(NFTokenOffer,                                   \
        LSF_FLAG(lsfSellNFToken, 0x00000001))                     \
                                                                  \
    LEDGER_OBJECT(MPTokenIssuance,                                \
        LSF_FLAG(lsfMPTLocked, 0x00000001)                        \
        LSF_FLAG(lsfMPTCanLock, 0x00000002)                       \
        LSF_FLAG(lsfMPTRequireAuth, 0x00000004)                   \
        LSF_FLAG(lsfMPTCanEscrow, 0x00000008)                     \
        LSF_FLAG(lsfMPTCanTrade, 0x00000010)                      \
        LSF_FLAG(lsfMPTCanTransfer, 0x00000020)                   \
        LSF_FLAG(lsfMPTCanClawback, 0x00000040))                  \
                                                                  \
    LEDGER_OBJECT(MPTokenIssuanceMutable,                         \
        LSF_FLAG(lsmfMPTCanMutateCanLock, 0x00000002)             \
        LSF_FLAG(lsmfMPTCanMutateRequireAuth, 0x00000004)         \
        LSF_FLAG(lsmfMPTCanMutateCanEscrow, 0x00000008)           \
        LSF_FLAG(lsmfMPTCanMutateCanTrade, 0x00000010)            \
        LSF_FLAG(lsmfMPTCanMutateCanTransfer, 0x00000020)         \
        LSF_FLAG(lsmfMPTCanMutateCanClawback, 0x00000040)         \
        LSF_FLAG(lsmfMPTCanMutateMetadata, 0x00010000)            \
        LSF_FLAG(lsmfMPTCanMutateTransferFee, 0x00020000))        \
                                                                  \
    LEDGER_OBJECT(MPToken,                                        \
        LSF_FLAG2(lsfMPTLocked, 0x00000001)                       \
        LSF_FLAG(lsfMPTAuthorized, 0x00000002))                   \
                                                                  \
    LEDGER_OBJECT(Credential,                                     \
        LSF_FLAG(lsfAccepted, 0x00010000))                        \
                                                                  \
    LEDGER_OBJECT(Vault,                                          \
        LSF_FLAG(lsfVaultPrivate, 0x00010000))

// clang-format on

// Create all the flag values as an enum.
// example:
// enum LedgerSpecificFlags {
//     lsfPasswordSpent = 0x00010000,
//     lsfRequireDestTag = 0x00020000,
//     ...
// };
#define TO_VALUE(name, value) name = value,
#define NULL_NAME(name, values) values
#define NULL_OUTPUT(name, value)
enum LedgerSpecificFlags : std::uint32_t {
    XMACRO(NULL_NAME, TO_VALUE, NULL_OUTPUT)
};

// Create maps for each set of flags.
// This is used below in `ALL_LEDGER_FLAGS` to generate the server_definitions
// RPC output. example: std::map<std::string, std::uint32_t> const
// AccountRootFlags =
// {{"lsfPasswordSpent", 0x00010000}, {"lsfRequireDestTag", 0x00020000}, ...};
using LedgerFlagMap = std::map<std::string, std::uint32_t>;
#define VALUE_TO_MAP(name, value) {#name, value},
#define TO_MAP(name, values) LedgerFlagMap const name##Flags = {values};
XMACRO(TO_MAP, VALUE_TO_MAP, VALUE_TO_MAP)

// Create a list of all ledger flag maps.
// This is used to generate the server_definitions RPC output.
// example:
// std::vector<std::pair<std::string, LedgerFlagMap>> const allLedgerFlags = {
//     {"AccountRoot", AccountRootFlags},
#define ALL_LEDGER_FLAGS(name, values) {#name, name##Flags},
std::vector<std::pair<std::string, LedgerFlagMap>> const allLedgerFlags = {
    XMACRO(ALL_LEDGER_FLAGS, NULL_OUTPUT, NULL_OUTPUT)};

#undef XMACRO
#undef TO_VALUE
#undef VALUE_TO_MAP
#undef NULL_NAME
#undef NULL_OUTPUT
#undef TO_MAP
#undef ALL_LEDGER_FLAGS

#pragma pop_macro("XMACRO")
#pragma pop_macro("TO_VALUE")
#pragma pop_macro("VALUE_TO_MAP")
#pragma pop_macro("NULL_NAME")
#pragma pop_macro("TO_MAP")
#pragma pop_macro("ALL_LEDGER_FLAGS")

//------------------------------------------------------------------------------

/** Holds the list of known ledger entry formats.
 */
class LedgerFormats : public KnownFormats<LedgerEntryType, LedgerFormats>
{
private:
    /** Create the object.
        This will load the object with all the known ledger formats.
    */
    LedgerFormats();

public:
    static LedgerFormats const&
    getInstance();

    // Fields shared by all txFormats:
    static std::initializer_list<SOElement> const&
    getCommonFields();
};

}  // namespace ripple

#endif
