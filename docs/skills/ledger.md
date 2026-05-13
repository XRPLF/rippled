# Ledger

Each ledger is an immutable snapshot: header (seq, hashes, close time) + state SHAMap + transaction SHAMap. `LedgerMaster` is the central coordinator.

## Key Invariants

- Once `setImmutable()` is called, the ledger and its SHAMaps cannot change; only immutable ledgers can be set in `LedgerHolder`
- Every server always has an open ledger; the open ledger cannot close until previous consensus completes
- Ledger header hashes to the ledger's identity hash; includes state root, tx root, parent hash, total coins, close time
- `LedgerMaster` tracks: `mPubLedger` (last published), `mValidLedger` (last validated), `mLedgerHistory` (cache)
- Validation requires minimum trusted validations (`minVal`); filtered by Negative UNL

## Common Bug Patterns

- Modifying a ledger after `setImmutable()` corrupts shared state; always check `mImmutable` before mutation
- Gap detection: if ledgers 603 and 600 exist but 601-602 are missing, `LedgerMaster` requests 602 first, then backfills 601
- `InboundLedger::gotData()` queues data for processing; calling `done()` before all data arrives creates incomplete ledgers
- `checkAccept` won't accept a ledger that isn't ahead of the last validated ledger; stale validations are silently ignored

## Ledger Entry Types

- Defined in `ledger_entries.macro` using `LEDGER_ENTRY(type, code, class, name, fields)`
- Each entry has an `SOTemplate` defining required/optional fields
- Key computation: `Indexes.cpp` computes unique keys (keylets) for each ledger object type
- `STLedgerEntry` wraps the serialized data with type-safe field access

## Review Checklist

- New ledger entry types: add to `ledger_entries.macro`, implement keylet in `Indexes.cpp`
- Verify `LedgerCleaner` can handle the new entry type for repair
- Check that acquisition code handles the entry in both `InboundLedger` and `LedgerMaster`

## Key Patterns

### Immutability Guard
```cpp
// After this, no mutations allowed on the ledger or its SHAMaps
inline void SHAMap::setImmutable()
{
    XRPL_ASSERT(state_ != SHAMapState::Invalid, "...");
    state_ = SHAMapState::Immutable;
}
// VERIFY: code never calls peek()/insert()/erase() after setImmutable()
```

### New Ledger Entry Keylet
```cpp
// REQUIRED: every new ledger entry type needs unique keylet computation
Keylet keylet::myEntry(AccountID const& id)
{
    return {ltMY_ENTRY,
        sha512Half(std::uint16_t(spaceMyEntry), id)};
}
// Also add to ledger_entries.macro and Indexes.cpp
```

## Key Files

- `src/xrpld/app/ledger/Ledger.h` - ledger class
- `src/xrpld/app/ledger/detail/LedgerMaster.cpp` - central coordinator
- `src/xrpld/app/ledger/detail/InboundLedger.cpp` - ledger acquisition
- `include/xrpl/protocol/detail/ledger_entries.macro` - entry type definitions
- `src/libxrpl/protocol/Indexes.cpp` - keylet computation
