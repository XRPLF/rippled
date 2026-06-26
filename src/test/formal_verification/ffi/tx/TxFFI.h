#pragma once

#include <test/formal_verification/ffi/LeanObjectFFI.h>
#include <test/formal_verification/ffi/ledger/LedgerFFI.h>
#include <test/formal_verification/ledger/LedgerConverters.h>

#include <lean/lean.h>

extern "C" {
lean_object*
lean_process_tx(lean_object* ledger, lean_object* txn, lean_object* rules);
}

namespace xrpl::test::formal_verification {

inline LeanTerResult
processTx(LedgerFFI& ledger, LeanObjectFFI const& txn, LeanObjectFFI const& rules)
{
    return leanTerResult(ledger.leanApplyView(lean_process_tx, txn, rules));
}

}  // namespace xrpl::test::formal_verification
