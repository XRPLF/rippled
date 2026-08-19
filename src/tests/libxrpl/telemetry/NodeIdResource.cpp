#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/telemetry/SpanNames.h>
#include <xrpl/telemetry/Telemetry.h>

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#ifdef XRPL_ENABLE_TELEMETRY
#include <opentelemetry/nostd/variant.h>
#include <opentelemetry/sdk/resource/resource.h>
#endif

/**
 * Contract tests for the `xrpl.node.id` resource attribute.
 *
 *  `xrpl.node.id` carries the node's base58 public key on both the trace and
 *  the metric OTel resource, so traces and metrics resolve to one node. The
 *  key string is a cross-component contract: the collector, TraceQL queries
 *  and Grafana dashboards all name it literally, and a silent rename would
 *  break them with no compile error. These tests pin the literal key, the
 *  Setup default, and the fact that the value can only arrive through
 *  Telemetry::setNodeId().
 *
 *  Scope limit: the two production resources are built inside TelemetryImpl
 *  (trace resource in start(), metric resource in the constructor) and inside
 *  MetricsRegistry::initExporterAndProvider(). Neither is reachable from this
 *  binary — TelemetryImpl only exists behind an OTLP/HTTP exporter with
 *  background export threads, which a unit test must not spin up (see
 *  GetMeter.cpp), and MetricsRegistry.cpp is not compiled into xrpl_tests in
 *  the telemetry-enabled build. The resource test below therefore pins the SDK
 *  contract those three call sites rely on: the exact key, and a std::string
 *  value landing in the string alternative of the attribute variant rather
 *  than the bool one.
 */

using namespace xrpl;
using namespace xrpl::telemetry;

TEST(NodeIdResource, attribute_key_is_dotted_resource_form)
{
    // The literal the collector, TraceQL and the dashboards all name.
    EXPECT_EQ(std::string_view(attr::nodeId), "xrpl.node.id");

    // Dotted, not the underscore form used for span attributes.
    EXPECT_EQ(std::string_view(attr::nodeId).find('_'), std::string_view::npos);

    // Sibling of the other two xrpl.* resource attributes, and distinct
    // from both.
    EXPECT_EQ(std::string_view(attr::networkId), "xrpl.network.id");
    EXPECT_EQ(std::string_view(attr::networkType), "xrpl.network.type");
    EXPECT_NE(std::string_view(attr::nodeId), std::string_view(attr::networkId));
    EXPECT_NE(std::string_view(attr::nodeId), std::string_view(attr::networkType));

    // Built from the shared segments, so the segment additions are exercised
    // too rather than only the joined result.
    EXPECT_EQ(std::string_view(seg::node), "node");
    EXPECT_EQ(std::string_view(seg::xrpl), "xrpl");
}

TEST(NodeIdResource, setup_node_id_defaults_to_empty)
{
    // Negative path: nothing has called setNodeId(), so there is no value to
    // stamp and the resource builders skip the attribute.
    Telemetry::Setup const s;
    EXPECT_TRUE(s.nodeId.empty());
    EXPECT_EQ(s.nodeId, "");
}

TEST(NodeIdResource, config_parsing_never_populates_node_id)
{
    // nodeId is deliberately not config-driven. Even with an explicit
    // service_instance_id and a node public key argument, makeTelemetrySetup()
    // must leave nodeId empty: Application::setup() is the only writer, via
    // setNodeId().
    Section section;
    section.set("enabled", "1");
    section.set("service_instance_id", "custom-id");

    auto const setup = makeTelemetrySetup(section, "nHUtest123", "2.0.0", 1);

    EXPECT_EQ(setup.serviceInstanceId, "custom-id");
    EXPECT_TRUE(setup.nodeId.empty());
}

TEST(NodeIdResource, set_node_id_on_disabled_path_is_inert)
{
    // The disabled build/config path takes the base-class no-op. Calling it
    // must be safe and must not change any observable state.
    Telemetry::Setup setup;
    setup.enabled = false;

    beast::Journal::Sink& sink = beast::Journal::getNullSink();
    beast::Journal const journal(sink);
    auto telemetry = makeTelemetry(setup, journal);
    ASSERT_NE(telemetry, nullptr);

    telemetry->setNodeId("nHUtest123");

    EXPECT_FALSE(telemetry->isEnabled());
    EXPECT_FALSE(telemetry->shouldTraceRpc());
    EXPECT_FALSE(telemetry->shouldTraceTransactions());
    EXPECT_FALSE(telemetry->shouldTraceConsensus());
    EXPECT_FALSE(telemetry->shouldTracePeer());
    EXPECT_FALSE(telemetry->shouldTraceLedger());
    EXPECT_EQ(telemetry->getConsensusTraceStrategy(), "deterministic");
}

#ifdef XRPL_ENABLE_TELEMETRY

TEST(NodeIdResource, resource_carries_node_id_as_a_string)
{
    namespace otel_resource = opentelemetry::sdk::resource;

    // A base58 node public key: 'n' prefix, 52 characters.
    std::string const nodeId = "n9MozjnGB3tpULewtTsVtuudg5JqYFyV3QFdAtVLzJaxHcBaxuXM";
    ASSERT_EQ(nodeId.size(), 52u);

    otel_resource::ResourceAttributes attrs;
    // std::string, never a string literal: the attribute variant's
    // char-const* overload binds to bool, which would record `true`.
    attrs[std::string(attr::nodeId)] = nodeId;

    auto const resource = otel_resource::Resource::Create(attrs);
    auto const& out = resource.GetAttributes();

    auto const it = out.find("xrpl.node.id");
    ASSERT_NE(it, out.end());

    // The string alternative, not bool — the pitfall the call sites guard.
    ASSERT_TRUE(opentelemetry::nostd::holds_alternative<std::string>(it->second));
    EXPECT_FALSE(opentelemetry::nostd::holds_alternative<bool>(it->second));
    EXPECT_EQ(opentelemetry::nostd::get<std::string>(it->second), nodeId);
}

TEST(NodeIdResource, resource_omits_node_id_when_it_was_never_set)
{
    namespace otel_resource = opentelemetry::sdk::resource;

    // Negative path: the call sites only assign when the value is non-empty,
    // so an unset node ID leaves the key off the resource entirely rather
    // than stamping a blank one.
    Telemetry::Setup const setup;
    ASSERT_TRUE(setup.nodeId.empty());

    otel_resource::ResourceAttributes attrs;
    if (!setup.nodeId.empty())
        attrs[std::string(attr::nodeId)] = setup.nodeId;

    auto const resource = otel_resource::Resource::Create(attrs);
    auto const& out = resource.GetAttributes();

    EXPECT_EQ(out.find("xrpl.node.id"), out.end());

    // The SDK still merges in its own defaults, so the absence above is a
    // real absence and not an empty map.
    EXPECT_FALSE(out.empty());
}

#endif  // XRPL_ENABLE_TELEMETRY
