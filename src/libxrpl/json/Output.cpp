#include <xrpl/json/Output.h>

#include <xrpl/json/Writer.h>
#include <xrpl/json/json_value.h>

#include <string>

namespace json {

namespace {

void
outputJson(json::Value const& value, Writer& writer)
{
    switch (value.type())
    {
        case json::NullValue: {
            writer.output(nullptr);
            break;
        }

        case json::IntValue: {
            writer.output(value.asInt());
            break;
        }

        case json::UintValue: {
            writer.output(value.asUInt());
            break;
        }

        case json::RealValue: {
            writer.output(value.asDouble());
            break;
        }

        case json::StringValue: {
            writer.output(value.asString());
            break;
        }

        case json::BooleanValue: {
            writer.output(value.asBool());
            break;
        }

        case json::ArrayValue: {
            writer.startRoot(Writer::CollectionType::Array);
            for (auto const& i : value)
            {
                writer.rawAppend();
                outputJson(i, writer);
            }
            writer.finish();
            break;
        }

        case json::ObjectValue: {
            writer.startRoot(Writer::CollectionType::Object);
            auto members = value.getMemberNames();
            for (auto const& tag : members)
            {
                writer.rawSet(tag);
                outputJson(value[tag], writer);
            }
            writer.finish();
            break;
        }
    }  // switch
}

}  // namespace

void
outputJson(json::Value const& value, Output const& out)
{
    Writer writer(out);
    outputJson(value, writer);
}

std::string
jsonAsString(json::Value const& value)
{
    std::string s;
    Writer writer(stringOutput(s));
    outputJson(value, writer);
    return s;
}

}  // namespace json
