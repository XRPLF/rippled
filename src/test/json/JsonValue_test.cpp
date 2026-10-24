#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>

namespace xrpl {

struct JsonValue_test : public beast::unit_test::Suite
{
    void
    testAsBool()
    {
        testcase("asBool");

        using json::Value;

        // Null and empty containers are false.
        BEAST_EXPECT(!Value().asBool());
        BEAST_EXPECT(!Value(json::ValueType::Array).asBool());
        BEAST_EXPECT(!Value(json::ValueType::Object).asBool());

        // Non-empty containers are true.
        {
            Value arr(json::ValueType::Array);
            arr.append(Value(1));
            BEAST_EXPECT(arr.asBool());

            Value obj(json::ValueType::Object);
            obj["key"] = Value(1);
            BEAST_EXPECT(obj.asBool());
        }

        // Boolean values pass through.
        BEAST_EXPECT(Value(true).asBool());
        BEAST_EXPECT(!Value(false).asBool());

        // Signed integers.
        BEAST_EXPECT(!Value(Value::Int{0}).asBool());
        BEAST_EXPECT(Value(Value::Int{1}).asBool());
        BEAST_EXPECT(Value(Value::Int{-1}).asBool());

        // Unsigned integers. asBool() must read the unsigned union member;
        // in particular values that only exist in the unsigned range (e.g.
        // kMaxUInt, which is negative when reinterpreted as signed) must
        // still be reported as true.
        BEAST_EXPECT(!Value(Value::UInt{0}).asBool());
        BEAST_EXPECT(Value(Value::UInt{1}).asBool());
        BEAST_EXPECT(Value(Value::kMaxUInt).asBool());

        // Reals.
        BEAST_EXPECT(!Value(0.0).asBool());
        BEAST_EXPECT(Value(1.5).asBool());

        // Strings: only the empty string is false.
        BEAST_EXPECT(!Value("").asBool());
        BEAST_EXPECT(Value("false").asBool());
    }

    void
    run() override
    {
        testAsBool();
    }
};

BEAST_DEFINE_TESTSUITE(JsonValue, json, xrpl);

}  // namespace xrpl
