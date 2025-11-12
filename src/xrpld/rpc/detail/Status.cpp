#include <xrpld/rpc/Status.h>

#include <sstream>

namespace ripple {
namespace RPC {

std::string
Status::codeString() const
{
    if (!*this)
        return "";

    if (type_ == Type::none)
        return std::to_string(code_);

    if (type_ == Status::Type::TER)
    {
        std::string s1, s2;

        [[maybe_unused]] auto const success = transResultInfo(toTER(), s1, s2);
        XRPL_ASSERT(success, "ripple::RPC::codeString : valid TER result");

        return s1 + ": " + s2;
    }

    if (type_ == Status::Type::error_code_i)
    {
        auto info = get_error_info(toErrorCode());
        std::ostringstream sStr;
        sStr << info.token.c_str() << ": " << info.message.c_str();
        return sStr.str();
    }

    // LCOV_EXCL_START
    UNREACHABLE("ripple::RPC::codeString : invalid type");
    return "";
    // LCOV_EXCL_STOP
}

void
Status::fillJson(Json::Value& value)
{
    if (!*this)
        return;

    auto& error = value[jss::error];
    error[jss::code] = code_;
    error[jss::message] = codeString();

    // Are there any more messages?
    if (!messages_.empty())
    {
        auto& messages = error[jss::data];
        for (auto& i : messages_)
            messages.append(i);
    }
}

std::string
Status::message() const
{
    std::string result;
    for (auto& m : messages_)
    {
        if (!result.empty())
            result += '/';
        result += m;
    }

    return result;
}

std::string
Status::toString() const
{
    if (*this)
        return codeString() + ":" + message();
    return "";
}

}  // namespace RPC
}  // namespace ripple
