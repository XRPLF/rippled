#pragma once

#include <xrpld/app/paths/RippleLineCache.h>

#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

hash_set<Currency>
accountDestCurrencies(
    AccountID const& account,
    std::shared_ptr<RippleLineCache> const& cache,
    bool includeXRP);

hash_set<Currency>
accountSourceCurrencies(
    AccountID const& account,
    std::shared_ptr<RippleLineCache> const& lrLedger,
    bool includeXRP);

}  // namespace xrpl
