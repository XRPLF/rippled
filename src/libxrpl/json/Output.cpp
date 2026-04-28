#include <xrpl/json/Output.h>

#include <xrpl/json/Writer.h>
#include <xrpl/json/json_value.h>

#include <string>

namespace Json {

namespace {

void
outputJson(Json::Value const& value, Writer& writer)
{
    switch (value.type())
    {
        case Json::NullValue: {
            writer.output(nullptr);
            break;
        }

        case Json::IntValue: {
            writer.output(value.asInt());
            break;
        }

        case Json::UintValue: {
            writer.output(value.asUInt());
            break;
        }

        case Json::RealValue: {
            writer.output(value.asDouble());
            break;
        }

        case Json::StringValue: {
            writer.output(value.asString());
            break;
        }

        case Json::BooleanValue: {
            writer.output(value.asBool());
            break;
        }

        case Json::ArrayValue: {
            writer.startRoot(Writer::Array);
            for (auto const& i : value)
            {
                writer.rawAppend();
                outputJson(i, writer);
            }
            writer.finish();
            break;
        }

        case Json::ObjectValue: {
            writer.startRoot(Writer::Object);
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
outputJson(Json::Value const& value, Output const& out)
{
    Writer writer(out);
    outputJson(value, writer);
}

std::string
jsonAsString(Json::Value const& value)
{
    std::string s;
    Writer writer(stringOutput(s));
    outputJson(value, writer);
    return s;
}

}  // namespace Json
