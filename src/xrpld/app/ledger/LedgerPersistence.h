#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Rules.h>

#include <cstdint>
#include <memory>
#include <tuple>

namespace xrpl {

class ServiceRegistry;
struct Fees;

/** Save, or arrange to save, a fully-validated ledger.

    @param registry The service registry providing access to required services.
    @param ledger The fully-validated ledger to save.
    @param isSynchronous If true, wait for the save to complete.
    @param isCurrent If true, the ledger is the current validated ledger.

    @return false on error.
*/
bool
pendSaveValidated(
    ServiceRegistry& registry,
    std::shared_ptr<Ledger const> const& ledger,
    bool isSynchronous,
    bool isCurrent);

/** Make ledger using info loaded from database.

    @param info Ledger information.
    @param rules Rules to use (may be overwritten by setup()).
    @param fees Fees to use (may be overwritten by setup()).
    @param registry Service registry for dependency injection.
    @param acquire Acquire the ledger if not found locally.
    @return Shared pointer to the ledger.
*/
std::shared_ptr<Ledger>
loadLedgerHelper(
    LedgerHeader const& info,
    Rules const& rules,
    Fees const& fees,
    ServiceRegistry& registry,
    bool acquire);

/** Load a ledger by its sequence number.

    @param ledgerIndex The sequence number of the ledger to load.
    @param rules Rules to use (may be overwritten by setup()).
    @param fees Fees to use (may be overwritten by setup()).
    @param registry Service registry for dependency injection.
    @param acquire Acquire the ledger if not found locally.
    @return Shared pointer to the ledger, or nullptr if not found.
*/
std::shared_ptr<Ledger>
loadByIndex(
    std::uint32_t ledgerIndex,
    Rules const& rules,
    Fees const& fees,
    ServiceRegistry& registry,
    bool acquire = true);

/** Load a ledger by its hash.

    @param ledgerHash The hash of the ledger to load.
    @param rules Rules to use (may be overwritten by setup()).
    @param fees Fees to use (may be overwritten by setup()).
    @param registry Service registry for dependency injection.
    @param acquire Acquire the ledger if not found locally.
    @return Shared pointer to the ledger, or nullptr if not found.
*/
std::shared_ptr<Ledger>
loadByHash(
    uint256 const& ledgerHash,
    Rules const& rules,
    Fees const& fees,
    ServiceRegistry& registry,
    bool acquire = true);

/** Fetch the ledger with the highest sequence contained in the database.

    @param rules Rules to use (may be overwritten by setup()).
    @param fees Fees to use (may be overwritten by setup()).
    @param registry Service registry for dependency injection.
    @return Tuple of (ledger, sequence, hash), or empty if not found.
*/
std::tuple<std::shared_ptr<Ledger>, std::uint32_t, uint256>
getLatestLedger(Rules const& rules, Fees const& fees, ServiceRegistry& registry);

}  // namespace xrpl
