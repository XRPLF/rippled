#pragma once

#include <test/formal_verification/ffi/protocol/ledger_entries/AccountRootFFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/CredentialFFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/DepositPreauthFFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/LoanBrokerFFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/LoanFFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/MPTokenFFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/MPTokenIssuanceFFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/PermissionedDomainFFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/RippleStateFFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/VaultFFI.h>

#include <xrpl/protocol/STLedgerEntry.h>

#include <lean/lean.h>

#include <cstdint>
#include <memory>

extern "C" {
uint16_t
lean_ledger_entry_code(lean_object* entry);
uint8_t
lean_ledger_entry_type_of_code(uint16_t code);
}

namespace xrpl::test::formal_verification {

// The modeled ledger entries
#define XRPL_LEAN_LEDGER_ENTRIES(X) \
    X(AccountRoot)                  \
    X(Credential)                   \
    X(DepositPreauth)               \
    X(Loan)                         \
    X(LoanBroker)                   \
    X(MPToken)                      \
    X(MPTokenIssuance)              \
    X(PermissionedDomain)           \
    X(RippleState)                  \
    X(Vault)

class LedgerEntryFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;

    // The entry's rippled LedgerEntryType code (matches ledger_entries::*::entryType).
    std::uint16_t
    code() const
    {
        return leanGet<std::uint16_t>(lean_ledger_entry_code);
    }

    std::shared_ptr<SLE const>
    toSle() const
    {
        switch (code())
        {
#define X(Name)                           \
    case ledger_entries::Name::entryType: \
        return leanInnerAs<Name##FFI>().toCpp().getSle();
            XRPL_LEAN_LEDGER_ENTRIES(X)
#undef X
            default:
                return nullptr;
        }
    }
};

}  // namespace xrpl::test::formal_verification
