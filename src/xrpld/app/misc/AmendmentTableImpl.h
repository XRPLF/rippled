#ifndef XRPL_APP_MISC_AMENDMENTTABLE_H_INCLUDED
#define XRPL_APP_MISC_AMENDMENTTABLE_H_INCLUDED

#include <xrpl/ledger/AmendmentTable.h>

#include <optional>

namespace xrpl {

std::unique_ptr<AmendmentTable>
make_AmendmentTable(
    ServiceRegistry& registry,
    std::chrono::seconds majorityTime,
    std::vector<AmendmentTable::FeatureInfo> const& supported,
    Section const& enabled,
    Section const& vetoed,
    beast::Journal journal);

}  // namespace xrpl

#endif
