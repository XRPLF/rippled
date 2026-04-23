#include <xrpl/json/json_writer.h>
#include <xrpl/json/to_string.h>

#include <string>

namespace Json {

std::string
to_string(Value const& value)
{
    return FastWriter().write(value);
}

std::string
pretty(Value const& value)
{
    return StyledWriter().write(value);
}

}  // namespace Json
