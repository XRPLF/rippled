#pragma once

#include <xrpl/beast/utility/Journal.h>

#include <xrpl.pb.h>

#include <memory>

namespace xrpl {
class Application;
class LedgerReplayer;

/**
 * Outcome of processing an incoming ledger-replay response.
 */
enum class ReplayMsgStatus {
    Ok,         ///< Accepted.
    BadData,    ///< Peer reported has_error() (legitimate "cannot fulfill" signal).
    Malformed,  ///< Protocol-level violation; no honest peer would produce this.
};

class LedgerReplayMsgHandler final
{
public:
    LedgerReplayMsgHandler(Application& app, LedgerReplayer& replayer);
    ~LedgerReplayMsgHandler() = default;

    /**
     * Process TMProofPathRequest and return TMProofPathResponse
     * @note check has_error() and error() of the response for error
     * @return TMProofPathResponse with the proof path, or with error() set if
     *         the request cannot be fulfilled
     */
    protocol::TMProofPathResponse
    processProofPathRequest(std::shared_ptr<protocol::TMProofPathRequest> const& msg);

    /**
     * Process TMProofPathResponse
     */
    ReplayMsgStatus
    processProofPathResponse(std::shared_ptr<protocol::TMProofPathResponse> const& msg);

    /**
     * Process TMReplayDeltaRequest and return TMReplayDeltaResponse
     * @note check has_error() and error() of the response for error
     * @return TMReplayDeltaResponse with the ledger delta, or with error() set
     *         if the request cannot be fulfilled
     */
    protocol::TMReplayDeltaResponse
    processReplayDeltaRequest(std::shared_ptr<protocol::TMReplayDeltaRequest> const& msg);

    /**
     * Process TMReplayDeltaResponse
     */
    ReplayMsgStatus
    processReplayDeltaResponse(std::shared_ptr<protocol::TMReplayDeltaResponse> const& msg);

private:
    Application& app_;
    LedgerReplayer& replayer_;
    beast::Journal journal_;
};

}  // namespace xrpl
