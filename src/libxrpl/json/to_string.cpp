#include <xrpl/json/to_string.h>

#include <xrpl/json/json_writer.h>
#include <xrpl/basics/TraceLog.h>

#include <string>

namespace json {

std::string
to_string(Value const& value)
{
    TRACE_FUNC();
    return FastWriter().write(value);
}

std::string
pretty(Value const& value)
{
    TRACE_FUNC();
    return StyledWriter().write(value);
}

}  // namespace json
