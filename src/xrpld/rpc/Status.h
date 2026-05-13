/** @file
 *  Unified error result type for the XRPL RPC layer.
 *
 *  Provides `Status`, an adapter over the two legacy error spaces — `TER`
 *  (transaction engine results) and `error_code_i` (RPC protocol errors) —
 *  plus a raw integer fallback, so handler code can propagate and report
 *  errors through a single type regardless of origin.
 */

#pragma once

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/TER.h>

namespace xrpl::RPC {

/** Unified error result type for RPC handlers.
 *
 *  Wraps either a `TER` (transaction engine result), an `error_code_i` (RPC
 *  protocol error), or a raw integer, together with an optional list of
 *  diagnostic message strings. The discriminant `type_` tag ensures each code
 *  space is converted back correctly; calling `toTER()` or `toErrorCode()` on
 *  the wrong type triggers an assertion.
 *
 *  The success sentinel is `kOK == 0`, which is coherent because both
 *  `tesSUCCESS` and `rpcSUCCESS` are also zero. `operator bool()` therefore
 *  reads as "true if something went wrong" and works uniformly across all
 *  three code spaces.
 *
 *  Inherits `std::exception` to support throw/catch flows; the dominant usage
 *  pattern in practice is value-based propagation (e.g., as a return value or
 *  inside `std::variant<Result, RPC::Status>`).
 *
 *  @see ErrorCodes.h for `error_code_i` and `inject_error()`
 *  @see TER.h for the transaction engine result type
 */
struct Status : public std::exception
{
public:
    /** Discriminant tag identifying which error code space this Status holds. */
    enum class Type {
        None,       /**< Raw integer or default-constructed (no semantic type). */
        TER,        /**< Transaction engine result (`TER`). */
        ErrorCodeI  /**< RPC protocol error (`error_code_i`). */
    };

    /** Underlying integer storage type for the error code. */
    using Code = int;

    /** List of freeform diagnostic message strings attached to the status. */
    using Strings = std::vector<std::string>;

    /** Success sentinel; equals zero, consistent with `tesSUCCESS` and `rpcSUCCESS`. */
    static constexpr Code kOK = 0;

    /** Constructs an OK (success) status with `Type::None` and `code_ == kOK`. */
    Status() = default;

    /** Constructs a raw-integer status.
     *
     *  The `enable_if` guard restricts this constructor to genuine integral
     *  types and excludes enums. This prevents `TER` or `error_code_i` enum
     *  values from silently binding here and leaving `type_` as `Type::None`,
     *  which would cause `inject()` and `toErrorCode()` to misbehave.
     *
     *  @tparam T An integral (non-enum) type.
     *  @param code Raw integer status code.
     *  @param d    Optional diagnostic messages attached to this status.
     */
    template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    Status(T code, Strings d = {}) : code_(code), messages_(std::move(d))
    {
    }

    /** Constructs a status from a `TER` transaction engine result.
     *
     *  @param ter The transaction engine result; stored as `TERtoInt(ter)`.
     *  @param d   Optional diagnostic messages attached to this status.
     */
    Status(TER ter, Strings d = {})
        : type_(Type::TER), code_(TERtoInt(ter)), messages_(std::move(d))
    {
    }

    /** Constructs a status from an `error_code_i` RPC error code.
     *
     *  @param e The RPC error code.
     *  @param d Optional diagnostic messages attached to this status.
     */
    Status(ErrorCodeI e, Strings d = {})
        : type_(Type::ErrorCodeI), code_(e), messages_(std::move(d))
    {
    }

    /** Constructs a status from an `error_code_i` with a single diagnostic message.
     *
     *  Convenience overload for the common single-message case, e.g.
     *  `RPC::Status{rpcINVALID_PARAMS, "ledgerHashMalformed"}`.
     *
     *  @param e The RPC error code.
     *  @param s A single diagnostic message string.
     */
    Status(ErrorCodeI e, std::string const& s) : type_(Type::ErrorCodeI), code_(e), messages_({s})
    {
    }

    /** Returns a human-readable representation of the error code.
     *
     *  For `Type::TER`, returns the TER token and description (e.g.
     *  `"temBAD_AMOUNT: Malformed: Bad amount."`). For `Type::ErrorCodeI`,
     *  returns the error token and message from the `ErrorInfo` table (e.g.
     *  `"badSyntax: Syntax error."`). For `Type::None`, returns the raw
     *  integer as a decimal string. Returns an empty string when OK.
     *
     *  @return Human-readable code string, or empty string if `code_ == kOK`.
     */
    [[nodiscard]] std::string
    codeString() const;

    /** Returns `true` if the status represents an error (i.e. `code_ != kOK`). */
    operator bool() const
    {
        return code_ != kOK;
    }

    /** Returns `true` if the status is OK (i.e. `code_ == kOK`). */
    bool
    operator!() const
    {
        return !bool(*this);
    }

    /** Recovers the stored code as a `TER`.
     *
     *  @pre `type() == Type::TER`; asserts otherwise.
     *  @return The original `TER` value reconstructed via `TER::fromInt`.
     */
    [[nodiscard]] TER
    toTER() const
    {
        XRPL_ASSERT(type_ == Type::TER, "xrpl::RPC::Status::toTER : type is TER");
        return TER::fromInt(code_);
    }

    /** Recovers the stored code as an `error_code_i`.
     *
     *  @pre `type() == Type::ErrorCodeI`; asserts otherwise.
     *  @return The original `error_code_i` value.
     */
    [[nodiscard]] ErrorCodeI
    toErrorCode() const
    {
        XRPL_ASSERT(type_ == Type::ErrorCodeI, "xrpl::RPC::Status::toTER : type is error code");
        return ErrorCodeI(code_);
    }

    /** Writes the error into a JSON response object.
     *
     *  Delegates to `injectError()` from `ErrorCodes.h`, which populates
     *  `object` with `jss::error` (token), an error code integer, and a
     *  message from the `ErrorInfo` table. If this `Status` carries attached
     *  messages, the first one is forwarded as a supplemental message string,
     *  overriding the default `ErrorInfo` message. Has no effect when OK.
     *
     *  @param object The JSON object to populate with error fields.
     *  @note Only `Type::ErrorCodeI` statuses carry through to `injectError()`.
     *      A `TER`-typed or raw-integer status will silently produce no output
     *      because `toErrorCode()` is called unconditionally; callers that
     *      hold a `TER` status should convert or check `type()` before calling.
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

    /** Returns all diagnostic messages attached to this status. */
    [[nodiscard]] Strings const&
    messages() const
    {
        return messages_;
    }

    /** Returns the attached diagnostic messages concatenated with `'/'` separators.
     *
     *  Returns an empty string if no messages are attached.
     *
     *  @return All messages joined by `'/'`, or empty string if none.
     */
    [[nodiscard]] std::string
    message() const;

    /** Returns the discriminant tag identifying the error code space. */
    [[nodiscard]] Type
    type() const
    {
        return type_;
    }

    /** Returns a combined string of `codeString()` and `message()`, or empty if OK. */
    [[nodiscard]] std::string
    toString() const;

    /** Writes a JSON-RPC 2.0 error object into `value`.
     *
     *  Populates `value["error"]` with `code`, `message`, and (if present)
     *  `data` fields per the JSON-RPC 2.0 spec at
     *  http://www.jsonrpc.org/specification#error_object. Has no effect when OK.
     *
     *  @note Not currently used by the production RPC dispatch pipeline; the
     *      active bridge to JSON responses is `inject()`. Present to document
     *      intent for future JSON-RPC 2.0 compliance.
     *  @param value The JSON object to populate.
     */
    void
    fillJson(json::Value&);

private:
    Type type_ = Type::None;
    Code code_ = kOK;
    Strings messages_;
};

}  // namespace xrpl::RPC
