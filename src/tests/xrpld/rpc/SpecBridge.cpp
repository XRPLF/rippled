#include <xrpld/rpc/detail/SpecBridge.hpp>

#include <xrpld/rpc/detail/XrplJsonView.hpp>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

#include <gtest/gtest.h>
#include <rpcspec/Errors.hpp>
#include <rpcspec/Types.hpp>
#include <rpcspec/handlers/ledger/Spec.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace xrpl {

namespace {

using FieldView = rpc::XrplJsonFieldView;
using ObjectView = rpc::XrplJsonObjectView;

json::Value
makeParams()
{
    json::Value params{json::ValueType::Object};
    params["str"] = "hello";
    params["num"] = 42;
    params["big"] = 3000000000U;
    params["neg"] = -7;
    params["real"] = 1.5;
    params["flag"] = true;
    params["nothing"] = json::Value{json::ValueType::Null};

    json::Value nested{json::ValueType::Object};
    nested["inner"] = "deep";
    params["obj"] = nested;

    json::Value list{json::ValueType::Array};
    list.append("a");
    list.append("b");
    params["arr"] = list;

    return params;
}

}  // namespace

TEST(XrplJsonView, field_types)
{
    auto params = makeParams();
    ObjectView const root{params};

    auto const str = root.child("str");
    EXPECT_TRUE(str.present());
    EXPECT_EQ(str.key(), "str");
    EXPECT_TRUE(str.isString());
    EXPECT_EQ(str.asString(), "hello");
    EXPECT_TRUE(str.is<std::string>());
    EXPECT_FALSE(str.isBool());
    EXPECT_FALSE(str.isInt64());
    EXPECT_FALSE(str.isObject());
    EXPECT_FALSE(str.isArray());

    // uint32-ness is about the value, not the storage: a non-negative Int answers true.
    auto const num = root.child("num");
    EXPECT_TRUE(num.isInt64());
    EXPECT_EQ(num.asInt64(), 42);
    EXPECT_TRUE(num.isUint32());
    EXPECT_EQ(num.asUint32(), 42U);
    EXPECT_TRUE(num.is<std::int64_t>());
    EXPECT_TRUE(num.is<std::uint32_t>());

    auto const big = root.child("big");
    EXPECT_TRUE(big.isInt64());
    EXPECT_EQ(big.asInt64(), 3000000000LL);
    EXPECT_TRUE(big.isUint32());
    EXPECT_EQ(big.asUint32(), 3000000000U);

    auto const neg = root.child("neg");
    EXPECT_TRUE(neg.isInt64());
    EXPECT_EQ(neg.asInt64(), -7);
    EXPECT_FALSE(neg.isUint32());
    EXPECT_FALSE(neg.is<std::uint32_t>());

    auto const real = root.child("real");
    EXPECT_TRUE(real.isDouble());
    EXPECT_EQ(real.asDouble(), 1.5);
    EXPECT_TRUE(real.is<double>());
    EXPECT_FALSE(real.isInt64());

    auto const flag = root.child("flag");
    EXPECT_TRUE(flag.isBool());
    EXPECT_TRUE(flag.asBool());
    EXPECT_TRUE(flag.is<bool>());

    // A boolean is not a number, though json::Value::isIntegral() says otherwise.
    EXPECT_FALSE(flag.isInt64());
    EXPECT_FALSE(flag.isUint32());

    // Present, but null: no predicate claims it.
    auto const nothing = root.child("nothing");
    EXPECT_TRUE(nothing.present());
    EXPECT_FALSE(nothing.isString());
    EXPECT_FALSE(nothing.isInt64());
    EXPECT_FALSE(nothing.isBool());
    EXPECT_FALSE(nothing.isDouble());
    EXPECT_FALSE(nothing.isObject());
    EXPECT_FALSE(nothing.isArray());

    auto const obj = root.child("obj");
    EXPECT_TRUE(obj.isObject());
    EXPECT_TRUE(obj.is<::rpc::spec::JsonObject>());
    EXPECT_EQ(obj.objectSize(), 1U);
    EXPECT_EQ(obj.arraySize(), 0U);

    auto const arr = root.child("arr");
    EXPECT_TRUE(arr.isArray());
    EXPECT_TRUE(arr.is<::rpc::spec::JsonArray>());
    EXPECT_EQ(arr.arraySize(), 2U);
    EXPECT_EQ(arr.objectSize(), 0U);
}

TEST(XrplJsonView, field_absent)
{
    auto params = makeParams();
    ObjectView const root{params};

    // Every predicate has to answer for an absent field: that is how a spec asks whether
    // an optional field was supplied.
    auto const missing = root.child("nope");
    EXPECT_FALSE(missing.present());
    EXPECT_EQ(missing.key(), "nope");
    EXPECT_FALSE(missing.isString());
    EXPECT_FALSE(missing.isInt64());
    EXPECT_FALSE(missing.isUint32());
    EXPECT_FALSE(missing.isBool());
    EXPECT_FALSE(missing.isDouble());
    EXPECT_FALSE(missing.isObject());
    EXPECT_FALSE(missing.isArray());
    EXPECT_EQ(missing.arraySize(), 0U);
    EXPECT_EQ(missing.objectSize(), 0U);
    EXPECT_FALSE(missing.is<std::string>());
    EXPECT_FALSE(missing.is<::rpc::spec::JsonObject>());

    // Walking down from an absent field stays absent rather than faulting.
    EXPECT_FALSE(missing.child("deeper").present());
    EXPECT_FALSE(missing.element(0).present());

    // Same for a non-object asked for a child, or a non-array for an element.
    EXPECT_FALSE(root.child("str").child("inner").present());
    EXPECT_FALSE(root.child("str").element(0).present());
}

TEST(XrplJsonView, field_navigation)
{
    auto params = makeParams();
    ObjectView const root{params};

    auto const inner = root.child("obj").child("inner");
    EXPECT_TRUE(inner.present());
    EXPECT_EQ(inner.asString(), "deep");
    EXPECT_EQ(inner.key(), "inner");

    // An element borrows its parent's key.
    auto const first = root.child("arr").element(0);
    EXPECT_TRUE(first.present());
    EXPECT_EQ(first.asString(), "a");
    EXPECT_EQ(first.key(), "arr");
    EXPECT_EQ(root.child("arr").element(1).asString(), "b");
    EXPECT_FALSE(root.child("arr").element(2).present());
}

TEST(XrplJsonView, field_set)
{
    auto params = makeParams();
    ObjectView root{params};

    // What a modifier does: normalise in place, so the converter after it sees that.
    root.child("str").set(std::string_view{"world"});
    EXPECT_EQ(params["str"].asString(), "world");

    root.child("num").set(std::uint32_t{7});
    EXPECT_EQ(params["num"].asUInt(), 7U);

    root.child("neg").set(std::int64_t{-1});
    EXPECT_EQ(params["neg"].asInt(), -1);

    root.child("flag").set(false);
    EXPECT_FALSE(params["flag"].asBool());

    root.child("real").set(2.5);
    EXPECT_EQ(params["real"].asDouble(), 2.5);

    // Write access reaches through a child.
    root.child("obj").child("inner").set(std::string_view{"changed"});
    EXPECT_EQ(params["obj"]["inner"].asString(), "changed");
}

TEST(XrplJsonView, field_set_spans_both_32_bit_types)
{
    auto params = makeParams();
    ObjectView root{params};

    // The spec's toNumber modifier produces values up to UINT32_MAX. json::Int is 32 bits,
    // so routing everything through it would wrap those negative and lose the value.
    root.child("num").set(std::int64_t{3000000000});
    auto const big = root.child("num");
    EXPECT_TRUE(big.isUint32());
    EXPECT_EQ(big.asUint32(), 3000000000U);
    EXPECT_EQ(big.asInt64(), 3000000000LL);

    root.child("neg").set(std::int64_t{-2000000000});
    auto const negative = root.child("neg");
    EXPECT_TRUE(negative.isInt64());
    EXPECT_EQ(negative.asInt64(), -2000000000LL);
    EXPECT_FALSE(negative.isUint32());
}

TEST(XrplJsonView, object_root)
{
    auto params = makeParams();

    ObjectView const root{params};
    EXPECT_TRUE(root.isObject());
    EXPECT_FALSE(root.isArray());
    EXPECT_TRUE(root.child("str").present());
    EXPECT_FALSE(root.child("nope").present());

    // Must not create the key: the non-const operator[] would have inserted a null.
    EXPECT_FALSE(params.isMember("nope"));

    json::Value array{json::ValueType::Array};
    ObjectView const arrayRoot{array};
    EXPECT_TRUE(arrayRoot.isArray());
    EXPECT_FALSE(arrayRoot.isObject());
    EXPECT_FALSE(arrayRoot.child("anything").present());
}

TEST(SpecBridge, shared_spec_parses_json_value)
{
    json::Value params{json::ValueType::Object};
    params["ledger_index"] = 42U;
    params["binary"] = true;
    params["transactions"] = true;

    auto const input = ::rpc::spec::handlers::ledger::kSpec.parse(params, 2);
    ASSERT_TRUE(input.has_value());

    EXPECT_TRUE(input->binary);
    EXPECT_TRUE(input->transactions);
    EXPECT_FALSE(input->expand);
    ASSERT_TRUE(std::holds_alternative<std::uint32_t>(input->ledger.value));
    EXPECT_EQ(std::get<std::uint32_t>(input->ledger.value), 42U);
}

TEST(SpecBridge, shared_spec_reports_a_bad_json_value)
{
    json::Value params{json::ValueType::Object};
    params["ledger_index"] = "not-a-ledger";

    auto const input = ::rpc::spec::handlers::ledger::kSpec.parse(params, 2);
    ASSERT_FALSE(input.has_value());
    EXPECT_EQ(std::get<::rpc::XrpldError>(input.error().code), RpcInvalidParams);
}

TEST(SpecBridge, shared_spec_warns_on_a_deprecated_field)
{
    json::Value params{json::ValueType::Object};
    params["type"] = "hashes";

    auto const warnings = ::rpc::spec::handlers::ledger::kSpec.check(params, 2);
    ASSERT_EQ(warnings.size(), 1U);
    EXPECT_EQ(warnings[0].code, ::rpc::WarningCode::WarnRpcDeprecated);
}

TEST(SpecBridge, inject_spec_warnings)
{
    std::string const kDeprecatedBase{
        ::rpc::getWarningInfo(::rpc::WarningCode::WarnRpcDeprecated).message};

    {
        json::Value out{json::ValueType::Object};
        rpc::injectSpecWarnings(out, {});
        EXPECT_FALSE(out.isMember(jss::warnings));
    }
    {
        json::Value out{json::ValueType::Object};
        rpc::injectSpecWarnings(
            out,
            {
                {
                    .code = ::rpc::WarningCode::WarnRpcDeprecated,
                    .field = "type",
                    .message = "Field 'type' is deprecated.",
                },
            });

        ASSERT_TRUE(out[jss::warnings].isArray());
        ASSERT_EQ(out[jss::warnings].size(), 1U);
        EXPECT_EQ(out[jss::warnings][0u][jss::id].asInt(), WarnRpcFieldsDeprecated);
        EXPECT_EQ(
            out[jss::warnings][0u][jss::message], kDeprecatedBase + " Field 'type' is deprecated.");
    }
    {
        // One entry per code, both details appended.
        json::Value out{json::ValueType::Object};
        rpc::injectSpecWarnings(
            out,
            {
                {
                    .code = ::rpc::WarningCode::WarnRpcDeprecated,
                    .field = "type",
                    .message = "Field 'type' is deprecated.",
                },
                {
                    .code = ::rpc::WarningCode::WarnRpcDeprecated,
                    .field = "ledger",
                    .message = "Field 'ledger' is deprecated.",
                },
            });

        ASSERT_EQ(out[jss::warnings].size(), 1U);
        EXPECT_EQ(
            out[jss::warnings][0u][jss::message],
            kDeprecatedBase + " Field 'type' is deprecated. Field 'ledger' is deprecated.");
    }
    {
        // Ordered by code, so the response does not depend on field visit order.
        json::Value out{json::ValueType::Object};
        rpc::injectSpecWarnings(
            out,
            {
                {
                    .code = ::rpc::WarningCode::WarnRpcDeprecated,
                    .field = "type",
                    .message = "Field 'type' is deprecated.",
                },
                {.code = ::rpc::WarningCode::WarnRpcOutdated, .field = "", .message = ""},
            });

        ASSERT_EQ(out[jss::warnings].size(), 2U);
        EXPECT_EQ(
            out[jss::warnings][0u][jss::id].asInt(),
            static_cast<int>(::rpc::WarningCode::WarnRpcOutdated));
        EXPECT_EQ(out[jss::warnings][1u][jss::id].asInt(), WarnRpcFieldsDeprecated);
    }
    {
        // A handler's own warnings are kept.
        json::Value out{json::ValueType::Object};
        json::Value existing{json::ValueType::Array};
        json::Value& entry = existing.append(json::Value{json::ValueType::Object});
        entry[jss::id] = WarnRpcUnsupportedMajority;
        out[jss::warnings] = existing;

        rpc::injectSpecWarnings(
            out,
            {
                {
                    .code = ::rpc::WarningCode::WarnRpcDeprecated,
                    .field = "type",
                    .message = "Field 'type' is deprecated.",
                },
            });

        ASSERT_EQ(out[jss::warnings].size(), 2U);
        EXPECT_EQ(out[jss::warnings][0u][jss::id].asInt(), WarnRpcUnsupportedMajority);
        EXPECT_EQ(out[jss::warnings][1u][jss::id].asInt(), WarnRpcFieldsDeprecated);
    }
}

}  // namespace xrpl
