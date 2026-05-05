#pragma once

// NOLINTBEGIN(readability-identifier-naming)

#include <xrpl/protocol/KnownFormats.h>

#include <map>
#include <string>
#include <vector>

namespace xrpl {
/** Identifiers for on-ledger objects.

    Each ledger object requires a unique type identifier, which is stored within the object itself;
   this makes it possible to iterate the entire ledger and determine each object's type and verify
   that the object you retrieved from a given hash matches the expected type.

    @warning Since these values are stored inside objects stored on the ledger they are part of the
   protocol.
   **Changing them should be avoided because without special handling, this will result in a hard
   fork.**

    @note Values outside this range may be used internally by the code for various purposes, but
   attempting to use such values to identify on-ledger objects will result in an invariant failure.

    @note When retiring types, the specific values should not be removed but should be marked as
   [[deprecated]]. This is to avoid accidental reuse of identifiers.

    @todo The C++ language does not enable checking for duplicate values here.
          If it becomes possible then we should do this.

    @ingroup protocol
*/
// Protocol-critical, hundreds of usages
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum LedgerEntryType : std::uint16_t {

#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY

#define LEDGER_ENTRY(tag, value, ...) tag = value,

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")

    //---------------------------------------------------------------------------
    /** A special type, matching any ledger entry type.

        The value does not represent a concrete type, but rather is used in contexts where the
       specific type of a ledger object is unimportant, unknown or unavailable.

        Objects with this special type cannot be created or stored on the ledger.

        \sa keylet::unchecked
    */
    LtAny = 0,

    /** A special type, matching any ledger type except directory nodes.

        The value does not represent a concrete type, but rather is used in contexts where the
       ledger object must not be a directory node but its specific type is otherwise unimportant,
       unknown or unavailable.

        Objects with this special type cannot be created or stored on the ledger.

        \sa keylet::child
     */
    LtChild = 0x1CD2,

    //---------------------------------------------------------------------------
    /** A legacy, deprecated type.

        \deprecated **This object type is not supported and should not be used.**
                    Support for this type of object was never implemented.
                    No objects of this type were ever created.
     */
    LtNickname [[deprecated("This object type is not supported and should not be used.")]] = 0x006e,

    /** A legacy, deprecated type.

        \deprecated **This object type is not supported and should not be used.**
                    Support for this type of object was never implemented.
                    No objects of this type were ever created.
     */
    LtContract [[deprecated("This object type is not supported and should not be used.")]] = 0x0063,

    /** A legacy, deprecated type.

        \deprecated **This object type is not supported and should not be used.**
                    Support for this type of object was never implemented.
                    No objects of this type were ever created.
     */
    LtGeneratorMap [[deprecated("This object type is not supported and should not be used.")]] =
        0x0067,
};

/** Ledger object flags.

    These flags are specified in ledger objects and modify their behavior.

    @warning Ledger object flags form part of the protocol.
    **Changing them should be avoided because without special handling, this will result in a hard
   fork.**

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

#undef ALL_LEDGER_FLAGS

// clang-format off

#define XMACRO(LEDGER_OBJECT, LSF_FLAG, LSF_FLAG2)                                                                                 \
    LEDGER_OBJECT(AccountRoot,                                                                                                     \
        LSF_FLAG(LsfPasswordSpent, 0x00010000)                  /* True, if password set fee is spent. */                          \
        LSF_FLAG(LsfRequireDestTag, 0x00020000)                 /* True, to require a DestinationTag for payments. */              \
        LSF_FLAG(LsfRequireAuth, 0x00040000)                    /* True, to require a authorization to hold IOUs. */               \
        LSF_FLAG(LsfDisallowXrp, 0x00080000)                    /* True, to disallow sending XRP. */                               \
        LSF_FLAG(LsfDisableMaster, 0x00100000)                  /* True, force regular key */                                      \
        LSF_FLAG(LsfNoFreeze, 0x00200000)                       /* True, cannot freeze ripple states */                            \
        LSF_FLAG(LsfGlobalFreeze, 0x00400000)                   /* True, all assets frozen */                                      \
        LSF_FLAG(LsfDefaultRipple, 0x00800000)                  /* True, incoming trust lines allow rippling by default */         \
        LSF_FLAG(LsfDepositAuth, 0x01000000)                    /* True, all deposits require authorization */                     \
        LSF_FLAG(LsfDisallowIncomingNfTokenOffer, 0x04000000)   /* True, reject new incoming NFT offers */                         \
        LSF_FLAG(LsfDisallowIncomingCheck, 0x08000000)          /* True, reject new checks */                                      \
        LSF_FLAG(LsfDisallowIncomingPayChan, 0x10000000)        /* True, reject new paychans */                                    \
        LSF_FLAG(LsfDisallowIncomingTrustline, 0x20000000)      /* True, reject new trustlines (only if no issued assets) */       \
        LSF_FLAG(LsfAllowTrustLineLocking, 0x40000000)          /* True, enable trustline locking */                               \
        LSF_FLAG(LsfAllowTrustLineClawback, 0x80000000))        /* True, enable clawback */                                        \
                                                                                                                                   \
    LEDGER_OBJECT(Offer,                                                                                                           \
        LSF_FLAG(LsfPassive, 0x00010000)                                                                                           \
        LSF_FLAG(LsfSell, 0x00020000)                           /* True, offer was placed as a sell. */                            \
        LSF_FLAG(LsfHybrid, 0x00040000))                        /* True, offer is hybrid. */                                       \
                                                                                                                                   \
    LEDGER_OBJECT(RippleState,                                                                                                     \
        LSF_FLAG(LsfLowReserve, 0x00010000)                     /* True, if entry counts toward reserve. */                        \
        LSF_FLAG(LsfHighReserve, 0x00020000)                                                                                       \
        LSF_FLAG(LsfLowAuth, 0x00040000)                                                                                           \
        LSF_FLAG(LsfHighAuth, 0x00080000)                                                                                          \
        LSF_FLAG(LsfLowNoRipple, 0x00100000)                                                                                       \
        LSF_FLAG(LsfHighNoRipple, 0x00200000)                                                                                      \
        LSF_FLAG(LsfLowFreeze, 0x00400000)                      /* True, low side has set freeze flag */                           \
        LSF_FLAG(LsfHighFreeze, 0x00800000)                     /* True, high side has set freeze flag */                          \
        LSF_FLAG(LsfAmmNode, 0x01000000)                        /* True, trust line to AMM. */                                     \
                                                                /* Used by client apps to identify payments via AMM. */            \
        LSF_FLAG(LsfLowDeepFreeze, 0x02000000)                  /* True, low side has set deep freeze flag */                      \
        LSF_FLAG(LsfHighDeepFreeze, 0x04000000))                /* True, high side has set deep freeze flag */                     \
                                                                                                                                   \
    LEDGER_OBJECT(SignerList,                                                                                                      \
        LSF_FLAG(LsfOneOwnerCount, 0x00010000))                 /* True, uses only one OwnerCount */                               \
                                                                                                                                   \
    LEDGER_OBJECT(DirNode,                                                                                                         \
        LSF_FLAG(LsfNfTokenBuyOffers, 0x00000001)                                                                                  \
        LSF_FLAG(LsfNfTokenSellOffers, 0x00000002))                                                                                \
                                                                                                                                   \
    LEDGER_OBJECT(NFTokenOffer,                                                                                                    \
        LSF_FLAG(LsfSellNfToken, 0x00000001))                                                                                      \
                                                                                                                                   \
    LEDGER_OBJECT(MPTokenIssuance,                                                                                                 \
        LSF_FLAG(LsfMptLocked, 0x00000001)                      /* Also used in ltMPTOKEN */                                       \
        LSF_FLAG(LsfMptCanLock, 0x00000002)                                                                                        \
        LSF_FLAG(LsfMptRequireAuth, 0x00000004)                                                                                    \
        LSF_FLAG(LsfMptCanEscrow, 0x00000008)                                                                                      \
        LSF_FLAG(LsfMptCanTrade, 0x00000010)                                                                                       \
        LSF_FLAG(LsfMptCanTransfer, 0x00000020)                                                                                    \
        LSF_FLAG(LsfMptCanClawback, 0x00000040))                                                                                   \
                                                                                                                                   \
    LEDGER_OBJECT(MPTokenIssuanceMutable,                                                                                          \
        LSF_FLAG(LsmfMptCanMutateCanLock, 0x00000002)                                                                              \
        LSF_FLAG(LsmfMptCanMutateRequireAuth, 0x00000004)                                                                          \
        LSF_FLAG(LsmfMptCanMutateCanEscrow, 0x00000008)                                                                            \
        LSF_FLAG(LsmfMptCanMutateCanTrade, 0x00000010)                                                                             \
        LSF_FLAG(LsmfMptCanMutateCanTransfer, 0x00000020)                                                                          \
        LSF_FLAG(LsmfMptCanMutateCanClawback, 0x00000040)                                                                          \
        LSF_FLAG(LsmfMptCanMutateMetadata, 0x00010000)                                                                             \
        LSF_FLAG(LsmfMptCanMutateTransferFee, 0x00020000))                                                                         \
                                                                                                                                   \
    LEDGER_OBJECT(MPToken,                                                                                                         \
        LSF_FLAG2(lsfMPTLocked, 0x00000001)                                                                                        \
        LSF_FLAG(LsfMptAuthorized, 0x00000002)                                                                                     \
        LSF_FLAG(LsfMptamm, 0x00000004))                                                                                           \
                                                                                                                                   \
    LEDGER_OBJECT(Credential,                                                                                                      \
        LSF_FLAG(LsfAccepted, 0x00010000))                                                                                         \
                                                                                                                                   \
    LEDGER_OBJECT(Vault,                                                                                                           \
        LSF_FLAG(LsfVaultPrivate, 0x00010000))                                                                                     \
                                                                                                                                   \
    LEDGER_OBJECT(Loan,                                                                                                            \
        LSF_FLAG(LsfLoanDefault, 0x00010000)                                                                                       \
        LSF_FLAG(LsfLoanImpaired, 0x00020000)                                                                                      \
        LSF_FLAG(LsfLoanOverpayment, 0x00040000))               /* True, loan allows overpayments */

// clang-format on

// Create all the flag values as an enum.
//
// example:
// enum LedgerSpecificFlags {
//     lsfPasswordSpent = 0x00010000,
//     lsfRequireDestTag = 0x00020000,
//     ...
// };
#define TO_VALUE(name, value) name = (value),
#define NULL_NAME(name, values) values
#define NULL_OUTPUT(name, value)
// Bitwise flag enum
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum LedgerSpecificFlags : std::uint32_t { XMACRO(NULL_NAME, TO_VALUE, NULL_OUTPUT) };

// Create getter functions for each set of flags using Meyer's singleton pattern.
// This avoids static initialization order fiasco while still providing efficient access.
// This is used below in `getAllLedgerFlags()` to generate the server_definitions RPC output.
//
// example:
// inline LedgerFlagMap const& getAccountRootFlags() {
//     static LedgerFlagMap const flags = {
//         {"lsfPasswordSpent", 0x00010000},
//         {"lsfRequireDestTag", 0x00020000},
//     ...};
//     return flags;
// }
using LedgerFlagMap = std::map<std::string, std::uint32_t>;
#define VALUE_TO_MAP(name, value) {#name, value},
#define TO_MAP(name, values)                         \
    inline LedgerFlagMap const& get##name##Flags()   \
    {                                                \
        static LedgerFlagMap const flags = {values}; \
        return flags;                                \
    }
XMACRO(TO_MAP, VALUE_TO_MAP, VALUE_TO_MAP)

// Create a getter function for all ledger flag maps using Meyer's singleton pattern.
// This is used to generate the server_definitions RPC output.
//
// example:
// inline std::vector<std::pair<std::string, LedgerFlagMap>> const& getAllLedgerFlags() {
//     static std::vector<std::pair<std::string, LedgerFlagMap>> const flags = {
//         {"AccountRoot", getAccountRootFlags()},
//     ...};
//     return flags;
// }
#define ALL_LEDGER_FLAGS(name, values) {#name, get##name##Flags()},
inline std::vector<std::pair<std::string, LedgerFlagMap>> const&
getAllLedgerFlags()
{
    static std::vector<std::pair<std::string, LedgerFlagMap>> const kFLAGS = {
        XMACRO(ALL_LEDGER_FLAGS, NULL_OUTPUT, NULL_OUTPUT)};
    return kFLAGS;
}

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

    // Fields shared by all ledger entry formats:
    static std::vector<SOElement> const&
    getCommonFields();
};

}  // namespace xrpl

// NOLINTEND(readability-identifier-naming)
