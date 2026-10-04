/**
 * @file SHAMapStoreSpanNames.cpp
 * Pins the rotation span contract: literal names the collector keep-policy,
 * expected_spans.json and the dashboards match on, and rotationOutcome() over
 * its whole domain, so no exit of SHAMapStoreImp::run's rotation block can
 * leave the root span without an outcome.
 *
 * Compiled in every build: the header holds only constants and a constexpr
 * function, so no OTel SDK is needed.
 */

#include <xrpld/app/misc/SHAMapStoreSpanNames.h>

#include <xrpl/telemetry/SpanNames.h>

#include <gtest/gtest.h>

#include <string_view>

using namespace xrpl::telemetry;

TEST(SHAMapStoreSpanNames, root_and_phase_names_are_the_exported_literals)
{
    namespace ns = nodestore_span;
    EXPECT_EQ(std::string_view{seg::nodestore}, "nodestore");
    EXPECT_EQ(std::string_view{ns::rotateFull}, "nodestore.rotate");
    EXPECT_EQ(std::string_view{ns::phase::clearPrior}, "clear_prior");
    EXPECT_EQ(std::string_view{ns::phase::copy}, "copy");
    EXPECT_EQ(std::string_view{ns::phase::freshenFetch}, "freshen.fetch");
    EXPECT_EQ(std::string_view{ns::phase::newBackend}, "new_backend");
    EXPECT_EQ(std::string_view{ns::phase::clearCaches}, "clear_caches");
    EXPECT_EQ(std::string_view{ns::phase::swap}, "swap");
    EXPECT_EQ(std::string_view{ns::phase::healthWait}, "health_wait");
}

TEST(SHAMapStoreSpanNames, attribute_keys_are_bare_snake_case_literals)
{
    namespace a = nodestore_span::attr;
    EXPECT_EQ(std::string_view{a::ledgerSeq}, "ledger_seq");
    EXPECT_EQ(std::string_view{a::lastRotated}, "last_rotated");
    EXPECT_EQ(std::string_view{a::nodeCount}, "node_count");
    EXPECT_EQ(std::string_view{a::keyCount}, "key_count");
    EXPECT_EQ(std::string_view{a::copyForwards}, "copy_forwards");
    EXPECT_EQ(std::string_view{a::cache}, "cache");
    EXPECT_EQ(std::string_view{a::keysCopied}, "keys_copied");
    EXPECT_EQ(std::string_view{a::nodesCopied}, "nodes_copied");
    EXPECT_EQ(std::string_view{a::serverMode}, "server_mode");
    EXPECT_EQ(std::string_view{a::missingLedgers}, "missing_ledgers");
    EXPECT_EQ(std::string_view{a::outcome}, "outcome");
}

TEST(SHAMapStoreSpanNames, rotation_outcome_covers_every_exit)
{
    using nodestore_span::RotationExit;
    using nodestore_span::rotationOutcome;
    static_assert(rotationOutcome(RotationExit::Complete) == "complete");
    static_assert(rotationOutcome(RotationExit::Expired) == "expired");
    static_assert(rotationOutcome(RotationExit::Stopping) == "stopping");
    static_assert(rotationOutcome(RotationExit::MissingNode) == "missing_node");
    EXPECT_EQ(rotationOutcome(RotationExit::Complete), "complete");
    EXPECT_EQ(rotationOutcome(RotationExit::Expired), "expired");
    EXPECT_EQ(rotationOutcome(RotationExit::Stopping), "stopping");
    EXPECT_EQ(rotationOutcome(RotationExit::MissingNode), "missing_node");
}
