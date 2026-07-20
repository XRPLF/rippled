#include <xrpl/tx/detail/TxApplySpanNames.h>

#include <gtest/gtest.h>

#include <string_view>

/**
 * Contract tests for the transaction apply-pipeline span constants.
 *
 *  The span names and attribute keys in TxApplySpanNames.h are a cross-component
 *  contract: the collector spanmetrics connector aggregates on these exact
 *  strings (dimensions tx_type, ter_result, stage) and the Grafana
 *  transaction-overview dashboard queries them. A silent rename here would
 *  break per-stage metrics with no compile error, so these tests pin the
 *  literal values. They need no telemetry runtime and run in every build.
 */

using namespace xrpl::telemetry;

TEST(TxApplySpanNames, span_names_are_dot_qualified)
{
    // Full span names feed SpanGuard::hashSpan() in applySteps.cpp.
    EXPECT_EQ(std::string_view(tx_apply_span::preflight), "tx.preflight");
    EXPECT_EQ(std::string_view(tx_apply_span::preclaim), "tx.preclaim");
}

TEST(TxApplySpanNames, operation_suffixes)
{
    // Suffix used with SpanGuard::span(cat, seg::tx, suffix) in Transactor.cpp.
    EXPECT_EQ(std::string_view(tx_apply_span::op::preflight), "preflight");
    EXPECT_EQ(std::string_view(tx_apply_span::op::preclaim), "preclaim");
    EXPECT_EQ(std::string_view(tx_apply_span::op::transactor), "transactor");
}

TEST(TxApplySpanNames, attribute_keys_match_collector_dimensions)
{
    // These keys MUST match docker/telemetry/otel-collector-config.yaml
    // spanmetrics dimensions and TxSpanNames.h (so both span sets aggregate
    // under one dimension).
    EXPECT_EQ(std::string_view(tx_apply_span::attr::stage), "stage");
    EXPECT_EQ(std::string_view(tx_apply_span::attr::txType), "tx_type");
    EXPECT_EQ(std::string_view(tx_apply_span::attr::terResult), "ter_result");
    EXPECT_EQ(std::string_view(tx_apply_span::attr::applied), "applied");
}

TEST(TxApplySpanNames, stage_values_are_the_three_pipeline_stages)
{
    // The stage attribute carries exactly these three values; they become the
    // spanmetrics `stage` dimension cardinality (3) and the dashboard filter.
    EXPECT_EQ(std::string_view(tx_apply_span::val::preflight), "preflight");
    EXPECT_EQ(std::string_view(tx_apply_span::val::preclaim), "preclaim");
    EXPECT_EQ(std::string_view(tx_apply_span::val::apply), "apply");
}
