#pragma once

#include <test/formal_verification/ffi/ledger/LedgerEntryFFI.h>
#include <test/formal_verification/ffi/protocol/FeesFFI.h>
#include <test/formal_verification/ffi/protocol/LedgerHeaderFFI.h>
#include <test/formal_verification/ffi/protocol/STAmountFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>

#include <lean/lean.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

extern "C" {
lean_object*
lean_ledger_create_empty(lean_object* unit);
lean_object*
lean_ledger_header_set(lean_object* ledger, lean_object* header);
lean_object*
lean_ledger_header_fetch(lean_object* ledger);
lean_object*
lean_ledger_fees_set(lean_object* ledger, lean_object* fees);
lean_object*
lean_ledger_fees_fetch(lean_object* ledger);
lean_object*
lean_ledger_add(lean_object* ledger, lean_object* entry);
lean_object*
lean_ledger_read(lean_object* ledger, lean_object* key);
lean_object*
lean_ledger_keys(lean_object* ledger);
}

namespace xrpl::test::formal_verification {

class LeanViewResult;

class LedgerFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;

    // Run a read-only Lean view op `lean_fn(thisLedger, args...) : Ledger × Except String α`
    template <class Fn, class... A>
    LeanViewResult
    leanReadView(Fn fn, A&&... args) const;

    // Run a mutating Lean view op; adopt the post-op ledger into *this, so the caller's handle
    // reflects the change
    template <class Fn, class... A>
    LeanViewResult
    leanApplyView(Fn fn, A&&... args);

    LedgerHeaderFFI
    header() const
    {
        return leanGetWrapped<LedgerHeaderFFI>(lean_ledger_header_fetch);
    }
    FeesFFI
    fees() const
    {
        return leanGetWrapped<FeesFFI>(lean_ledger_fees_fetch);
    }
    std::vector<uint256>
    keys() const
    {
        return leanGetList<UInt256FFI>(lean_ledger_keys);
    }

    std::optional<LedgerEntryFFI>
    read(uint256 const& key) const
    {
        return leanOptCall<LedgerEntryFFI>(lean_ledger_read, UInt256FFI::build(key));
    }
};

// The `Except String α` outcome of a Lean view op. Provides ok()/error()/okValue().
class LeanViewResult
{
    LeanObjectFFI except_;

public:
    explicit LeanViewResult(lean_object* except) : except_(except)
    {
    }
    bool
    ok() const
    {
        return exceptOk(except_.raw());
    }
    std::string
    error() const
    {
        return std::string(lean_string_cstr(exceptVal(except_.raw())));
    }
    lean_object*
    okValue() const
    {
        return exceptVal(except_.raw());
    }
};

template <class Fn, class... A>
inline LeanViewResult
LedgerFFI::leanReadView(Fn fn, A&&... args) const
{
    LeanObjectFFI pair(leanCallSelf(fn, std::forward<A>(args)...));
    return LeanViewResult(retain(pairSecond(pair.raw())));
}

template <class Fn, class... A>
inline LeanViewResult
LedgerFFI::leanApplyView(Fn fn, A&&... args)
{
    LeanObjectFFI pair(leanCallSelf(fn, std::forward<A>(args)...));
    reset(retain(pairFirst(pair.raw())));  // adopt the post-op ledger into *this
    return LeanViewResult(retain(pairSecond(pair.raw())));
}

class LedgerFFIBuilder : public LeanObjectFFI
{
    // Wrap `inner` in the LedgerEntry constructor for ledger entry `type`, then add it.
    LedgerFFIBuilder&
    addWithType(LedgerEntryType type, lean_object* inner)
    {
        leanSet(
            lean_ledger_add,
            leanWrapCtor(lean_ledger_entry_type_of_code(static_cast<uint16_t>(type)), inner));
        return *this;
    }

public:
    LedgerFFIBuilder() : LeanObjectFFI(leanEmptyOf(lean_ledger_create_empty))
    {
    }

    LedgerFFIBuilder&
    header(LedgerHeaderFFI h)
    {
        leanSet(lean_ledger_header_set, leanPrep(h));
        return *this;
    }
    LedgerFFIBuilder&
    fees(FeesFFI f)
    {
        leanSet(lean_ledger_fees_set, leanPrep(f));
        return *this;
    }

#define X(Name)                                                           \
    LedgerFFIBuilder& add(Name##FFI e)                                    \
    {                                                                     \
        return addWithType(ledger_entries::Name::entryType, leanPrep(e)); \
    }
    XRPL_LEAN_LEDGER_ENTRIES(X)
#undef X

    LedgerFFI
    build()
    {
        return leanBuildAs<LedgerFFI>();
    }
};

}  // namespace xrpl::test::formal_verification
