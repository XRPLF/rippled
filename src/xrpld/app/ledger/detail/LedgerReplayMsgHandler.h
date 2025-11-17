#ifndef XRPL_APP_LEDGER_LEDGERREPLAYMSGHANDLER_H_INCLUDED
#define XRPL_APP_LEDGER_LEDGERREPLAYMSGHANDLER_H_INCLUDED

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/messages.h>

namespace ripple {
class Application;
class LedgerReplayer;

class LedgerReplayMsgHandler final
{
public:
    LedgerReplayMsgHandler(Application& app, LedgerReplayer& replayer);
    ~LedgerReplayMsgHandler() = default;

    /**
     * Process TMProofPathRequest and return TMProofPathResponse
     * @note check has_error() and error() of the response for error
     */
    protocol::TMProofPathResponse
    processProofPathRequest(
        std::shared_ptr<protocol::TMProofPathRequest> const& msg);

    /**
     * Process TMProofPathResponse
     * @return false if the response message has bad format or bad data;
     *         true otherwise
     */
    bool
    processProofPathResponse(
        std::shared_ptr<protocol::TMProofPathResponse> const& msg);

    /**
     * Process TMReplayDeltaRequest and return TMReplayDeltaResponse
     * @note check has_error() and error() of the response for error
     */
    protocol::TMReplayDeltaResponse
    processReplayDeltaRequest(
        std::shared_ptr<protocol::TMReplayDeltaRequest> const& msg);

    /**
     * Process TMReplayDeltaResponse
     * @return false if the response message has bad format or bad data;
     *         true otherwise
     */
    bool
    processReplayDeltaResponse(
        std::shared_ptr<protocol::TMReplayDeltaResponse> const& msg);

private:
    Application& app_;
    LedgerReplayer& replayer_;
    beast::Journal journal_;
};

}  // namespace ripple

#endif
