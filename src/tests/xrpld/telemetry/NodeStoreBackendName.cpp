/**
 * GTest unit tests for nodeStoreBackendName(), the value of the `backend`
 * label on the NodeStore gauges.
 *
 * Every test calls the real NodeStore factory registry; no factory is stubbed.
 */

#include <xrpld/telemetry/NodeStoreBackendName.h>

#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>

#include <gtest/gtest.h>

#include <string>

using namespace xrpl;

namespace {

/**
 * Builds a `[node_db]` section holding only a `type` key.
 *
 * @param type Value for the `type` key, spelled as an operator would.
 * @return The section, named like the real config section.
 */
Section
nodeDbWithType(std::string const& type)
{
    Section section{Sections::kNodeDatabase};
    section.set(Keys::kType, type);
    return section;
}

}  // namespace

TEST(NodeStoreBackendName, canonical_type_gives_factory_name)
{
    EXPECT_EQ(telemetry::nodeStoreBackendName(nodeDbWithType("NuDB")), "NuDB");
    EXPECT_EQ(telemetry::nodeStoreBackendName(nodeDbWithType("Memory")), "Memory");
    EXPECT_EQ(telemetry::nodeStoreBackendName(nodeDbWithType("none")), "none");
}

TEST(NodeStoreBackendName, type_case_does_not_split_the_value)
{
    EXPECT_EQ(telemetry::nodeStoreBackendName(nodeDbWithType("nudb")), "NuDB");
    EXPECT_EQ(telemetry::nodeStoreBackendName(nodeDbWithType("MEMORY")), "Memory");
}

TEST(NodeStoreBackendName, rocksdb_follows_the_registered_factory)
{
    // RocksDB is registered only when the build has it, so read the
    // expectation from the manager rather than hard-coding it.
    auto const* factory = node_store::Manager::instance().find("rocksdb");
    // std::string on both arms: a string_view arm would make the result a view
    // of the temporary getName() returns.
    std::string const expected =
        factory != nullptr ? factory->getName() : std::string{telemetry::kUnknownNodeStoreBackend};
    EXPECT_EQ(telemetry::nodeStoreBackendName(nodeDbWithType("rocksdb")), expected);
}

TEST(NodeStoreBackendName, unknown_type_gives_unknown)
{
    EXPECT_EQ(
        telemetry::nodeStoreBackendName(nodeDbWithType("NotABackend")),
        telemetry::kUnknownNodeStoreBackend);
}

TEST(NodeStoreBackendName, missing_type_gives_unknown)
{
    Section const empty{Sections::kNodeDatabase};
    EXPECT_EQ(telemetry::nodeStoreBackendName(empty), telemetry::kUnknownNodeStoreBackend);
}
