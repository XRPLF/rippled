#pragma once

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/TER.h>

#include <exception>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl::RPC {

/** Status represents the results of an operation that might fail.

    It wraps the legacy codes TER and error_code_i, providing both a uniform
    interface and a way to attach additional information to existing status
    returns.

    A Status can also be used to fill a json::Value with a JSON-RPC 2.0
    error response:  see http://www.jsonrpc.org/specification#error_object
 */
struct Status : public std::exception
{
public:
    enum class Type { None, TER, ErrorCodeI };
    using Code = int;
    using Strings = std::vector<std::string>;

    static constexpr Code kOK = 0;

    Status() = default;

    // The enable_if allows only integers (not enums).  Prevents enum narrowing.
    template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    Status(T code, Strings d = {}) : code_(code), messages_(std::move(d))
    {
    }

    Status(TER ter, Strings d = {})
        : type_(Type::TER), code_(TERtoInt(ter)), messages_(std::move(d))
    {
    }

    Status(ErrorCodeI e, Strings d = {})
        : type_(Type::ErrorCodeI), code_(e), messages_(std::move(d))
    {
    }

    Status(ErrorCodeI e, std::string const& s) : type_(Type::ErrorCodeI), code_(e), messages_({s})
    {
    }

    /* Returns a representation of the integer status Code as a string.
       If the Status is OK, the result is an empty string.
    */
    [[nodiscard]] std::string
    codeString() const;

    /** Returns true if the Status is *not* OK. */
    operator bool() const
    {
        return code_ != kOK;
    }

    /** Returns true if the Status is OK. */
    bool
    operator!() const
    {
        return !bool(*this);
    }

    /** Returns the Status as a TER.
        This may only be called if type() == Type::TER. */
    [[nodiscard]] TER
    toTER() const
    {
        XRPL_ASSERT(type_ == Type::TER, "xrpl::RPC::Status::toTER : type is TER");
        return TER::fromInt(code_);
    }

    /** Returns the Status as an error_code_i.
        This may only be called if type() == Type::ErrorCodeI. */
    [[nodiscard]] ErrorCodeI
    toErrorCode() const
    {
        XRPL_ASSERT(type_ == Type::ErrorCodeI, "xrpl::RPC::Status::toTER : type is error code");
        return ErrorCodeI(code_);
    }

    /** Apply the Status to a JsonObject
     */
    void
    inject(json::Value& object) const
    {
        if (auto ec = toErrorCode())
        {
            if (messages_.empty())
            {
                injectError(ec, object);
            }
            else
            {
                injectError(ec, message(), object);
            }
        }
    }

    [[nodiscard]] Strings const&
    messages() const
    {
        return messages_;
    }

    /** Return the first message, if any. */
    [[nodiscard]] std::string
    message() const;

    [[nodiscard]] Type
    type() const
    {
        return type_;
    }

    [[nodiscard]] std::string
    toString() const;

    /** Fill a json::Value with an RPC 2.0 response.
        If the Status is OK, fillJson has no effect.
        Not currently used. */
    void
    fillJson(json::Value&);

private:
    Type type_ = Type::None;
    Code code_ = kOK;
    Strings messages_;
};

}  // namespace xrpl::RPC
