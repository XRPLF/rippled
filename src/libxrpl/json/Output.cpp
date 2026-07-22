#include <xrpl/json/Output.h>

#include <xrpl/json/Writer.h>
#include <xrpl/json/json_value.h>

#include <string>

namespace json {

namespace {

void
outputJson(Value const& value, Writer& writer)
{
    switch (value.type())
    {
        case ValueType::Null: {
            writer.output(nullptr);
            break;
        }

        case ValueType::Int: {
            writer.output(value.asInt());
            break;
        }

        case ValueType::UInt: {
            writer.output(value.asUInt());
            break;
        }

        case ValueType::Real: {
            writer.output(value.asDouble());
            break;
        }

        case ValueType::String: {
            writer.output(value.asString());
            break;
        }

        case ValueType::Boolean: {
            writer.output(value.asBool());
            break;
        }

        case ValueType::Array: {
            writer.startRoot(Writer::CollectionType::Array);
            for (auto const& i : value)
            {
                writer.rawAppend();
                outputJson(i, writer);
            }
            writer.finish();
            break;
        }

        case ValueType::Object: {
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
outputJson(Value const& value, Output const& out)
{
    Writer writer(out);
    outputJson(value, writer);
}

std::string
jsonAsString(Value const& value)
{
    std::string s;
    Writer writer(stringOutput(s));
    outputJson(value, writer);
    return s;
}

}  // namespace json
