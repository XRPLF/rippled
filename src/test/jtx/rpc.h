#ifndef XRPL_TEST_JTX_RPC_H_INCLUDED
#define XRPL_TEST_JTX_RPC_H_INCLUDED

#include <test/jtx/Env.h>

#include <tuple>

namespace ripple {
namespace test {
namespace jtx {

/** Set the expected result code for a JTx
    The test will fail if the code doesn't match.
*/
class rpc
{
private:
    std::optional<error_code_i> code_;
    std::optional<std::string> errorMessage_;
    std::optional<std::string> error_;
    std::optional<std::string> errorException_;

public:
    /// If there's an error code, we expect an error message
    explicit rpc(error_code_i code, std::optional<std::string> m = {})
        : code_(code), errorMessage_(m)
    {
    }

    ///  If there is not a code, we expect an exception message
    explicit rpc(
        std::string error,
        std::optional<std::string> exceptionMessage = {})
        : error_(error), errorException_(exceptionMessage)
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        // The RPC request should fail. RPC errors result in telENV_RPC_FAILED.
        jt.ter = telENV_RPC_FAILED;
        if (code_)
        {
            auto const& errorInfo = RPC::get_error_info(*code_);
            // When an RPC request returns an error code ('error_code'), it
            // always includes an error message ('error_message'), and sometimes
            // includes an error token ('error'). If it does, the error token is
            // always obtained from the lookup into the ErrorInfo lookup table.
            //
            // Take advantage of that fact to populate jt.rpcException. The
            // check will be aware of whether the rpcExcpetion can be safely
            // ignored.
            jt.rpcCode = {
                *code_,
                errorMessage_ ? *errorMessage_ : errorInfo.message.c_str()};
            jt.rpcException = {errorInfo.token.c_str(), std::nullopt};
        }
        if (error_)
            jt.rpcException = {*error_, errorException_};
    }
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
