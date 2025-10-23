#ifndef XRPL_TX_IMPL_SIGNER_ENTRIES_H_INCLUDED
#define XRPL_TX_IMPL_SIGNER_ENTRIES_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>  // NotTEC

#include <xrpl/basics/Expected.h>        //
#include <xrpl/beast/utility/Journal.h>  // beast::Journal
#include <xrpl/protocol/TER.h>           // temMALFORMED
#include <xrpl/protocol/UintTypes.h>     // AccountID

#include <optional>
#include <string_view>

namespace ripple {

// Forward declarations
class STObject;

// Support for SignerEntries that is needed by a few Transactors.
//
// SignerEntries is represented as a std::vector<SignerEntries::SignerEntry>.
// There is no direct constructor for SignerEntries.
//
//  o A std::vector<SignerEntries::SignerEntry> is a SignerEntries.
//  o More commonly, SignerEntries are extracted from an STObject by
//    calling SignerEntries::deserialize().
class SignerEntries
{
public:
    explicit SignerEntries() = delete;

    struct SignerEntry
    {
        AccountID account;
        std::uint16_t weight;
        std::optional<uint256> tag;

        SignerEntry(
            AccountID const& inAccount,
            std::uint16_t inWeight,
            std::optional<uint256> inTag)
            : account(inAccount), weight(inWeight), tag(inTag)
        {
        }

        // For sorting to look for duplicate accounts
        friend bool
        operator<(SignerEntry const& lhs, SignerEntry const& rhs)
        {
            return lhs.account < rhs.account;
        }

        friend bool
        operator==(SignerEntry const& lhs, SignerEntry const& rhs)
        {
            return lhs.account == rhs.account;
        }
    };

    // Deserialize a SignerEntries array from the network or from the ledger.
    //
    // obj Contains a SignerEntries field that is an STArray.
    // journal For reporting error conditions.
    // annotation Source of SignerEntries, like "ledger" or "transaction".
    static Expected<std::vector<SignerEntry>, NotTEC>
    deserialize(
        STObject const& obj,
        beast::Journal journal,
        std::string_view annotation);
};

}  // namespace ripple

#endif  // XRPL_TX_IMPL_SIGNER_ENTRIES_H_INCLUDED
