#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/AmendmentTable.h>
#include <xrpl/nodestore/Database.h>

#include <chrono>
#include <memory>
#include <vector>

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
