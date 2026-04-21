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
        case Json::nullValue: {
            writer.output(nullptr);
            break;
        }

        case Json::intValue: {
            writer.output(value.asInt());
            break;
        }

        case Json::uintValue: {
            writer.output(value.asUInt());
            break;
        }

        case Json::realValue: {
            writer.output(value.asDouble());
            break;
        }

        case Json::stringValue: {
            writer.output(value.asString());
            break;
        }

        case Json::booleanValue: {
            writer.output(value.asBool());
            break;
        }

        case Json::arrayValue: {
            writer.startRoot(Writer::array);
            for (auto const& i : value)
            {
                writer.rawAppend();
                outputJson(i, writer);
            }
            writer.finish();
            break;
        }

        case Json::objectValue: {
            writer.startRoot(Writer::object);
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
