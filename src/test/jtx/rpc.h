#pragma once

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/TER.h>

#include <optional>
#include <string>
#include <utility>

namespace xrpl::test::jtx {

/** Set the expected result code for a JTx
    The test will fail if the code doesn't match.
*/
class Rpc
{
private:
    std::optional<ErrorCodeI> code_;
    std::optional<std::string> errorMessage_;
    std::optional<std::string> error_;
    std::optional<std::string> errorException_;

public:
    /// If there's an error code, we expect an error message
    explicit Rpc(ErrorCodeI code, std::optional<std::string> m = {})
        : code_(code), errorMessage_(std::move(m))
    {
    }

    ///  If there is not a code, we expect an exception message
    explicit Rpc(std::string error, std::optional<std::string> exceptionMessage = {})
        : error_(error), errorException_(std::move(exceptionMessage))
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        // The RPC request should fail. RPC errors result in telENV_RPC_FAILED.
        jt.ter = telENV_RPC_FAILED;
        if (code_)
        {
            auto const& errorInfo = RPC::getErrorInfo(*code_);
            // When an RPC request returns an error code ('error_code'), it
            // always includes an error message ('error_message'), and sometimes
            // includes an error token ('error'). If it does, the error token is
            // always obtained from the lookup into the ErrorInfo lookup table.
            //
            // Take advantage of that fact to populate jt.rpcException. The
            // check will be aware of whether the rpcException can be safely
            // ignored.
            jt.rpcCode = {*code_, errorMessage_ ? *errorMessage_ : errorInfo.message.cStr()};
            jt.rpcException = {errorInfo.token.cStr(), std::nullopt};
        }
        if (error_)
            jt.rpcException = {*error_, errorException_};
    }
};

}  // namespace xrpl::test::jtx
