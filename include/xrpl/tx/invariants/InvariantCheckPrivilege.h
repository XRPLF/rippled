#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxSettings.h>  // IWYU pragma: export

namespace xrpl {

/*
assert(enforce)

There are several asserts (or XRPL_ASSERTs) in invariant check files that check
a variable named `enforce` when an invariant fails. At first glance, those
asserts may look incorrect, but they are not.

Those asserts take advantage of two facts:
1. `asserts` are not (normally) executed in release builds.
2. Invariants should *never* fail, except in tests that specifically modify
   the open ledger to break them.

This makes `assert(enforce)` sort of a second-layer of invariant enforcement
aimed at _developers_. It's designed to fire if a developer writes code that
violates an invariant, and runs it in unit tests or a develop build that _does
not have the relevant amendments enabled_. It's intentionally a pain in the neck
so that bad code gets caught and fixed as early as possible.
*/

// `enum Privilege` and its `operator|` live in <xrpl/protocol/TxSettings.h>,
// alongside the TxSettings struct that carries them out of transactions.macro.

bool
hasPrivilege(STTx const& tx, Privilege priv);

}  // namespace xrpl
