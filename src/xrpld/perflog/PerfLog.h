//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_BASICS_PERFLOG_H
#define RIPPLE_BASICS_PERFLOG_H

#include <xrpld/core/Config.h>
#include <xrpld/core/JobTypes.h>

#include <xrpl/json/json_value.h>

#include <boost/filesystem.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace beast {
class Journal;
}

namespace ripple {
class Application;
namespace perf {

/**
 * Singleton class that maintains performance counters and optionally
 * writes Json-formatted data to a distinct log. It should exist prior
 * to other objects launched by Application to make it accessible for
 * performance logging.
 */

class PerfLog
{
public:
    using steady_clock = std::chrono::steady_clock;
    using system_clock = std::chrono::system_clock;
    using steady_time_point = std::chrono::time_point<steady_clock>;
    using system_time_point = std::chrono::time_point<system_clock>;
    using seconds = std::chrono::seconds;
    using milliseconds = std::chrono::milliseconds;
    using microseconds = std::chrono::microseconds;

    /**
     * Configuration from [perf] section of rippled.cfg.
     */
    struct Setup
    {
        boost::filesystem::path perfLog;
        // log_interval is in milliseconds to support faster testing.
        milliseconds logInterval{seconds(1)};
    };

    struct Peer
    {
        struct PeerMsg
        {
            ::Json::StaticString label;
            // Atomics are faster than mutexes for modifying a pair integers,
            // and it's not strictly necessary that they are read or modified in
            // conjunction atomically.
            std::atomic<std::uint64_t> sent{0};
            std::atomic<std::uint64_t> sentBytes{0};
            std::atomic<std::uint64_t> received{0};
            std::atomic<std::uint64_t> receivedBytes{0};

            PeerMsg(char const* l) : label(l)
            {
                sent.store(0);
                sentBytes.store(0);
                received.store(0);
                receivedBytes.store(0);
            }

            PeerMsg(PeerMsg const& other) : label(other.label)
            {
                sent.store(other.sent.load());
                sentBytes.store(other.sentBytes.load());
                received.store(other.received.load());
                receivedBytes.store(other.receivedBytes.load());
            }

            Json::Value
            to_json() const
            {
                Json::Value ret(Json::objectValue);
                ret[Json::StaticString("sent")] = std::to_string(sent.load());
                ret[Json::StaticString("sent_bytes")] =
                    std::to_string(sentBytes.load());
                ret[Json::StaticString("received")] =
                    std::to_string(received.load());
                ret[Json::StaticString("received_bytes")] =
                    std::to_string(receivedBytes.load());
                return ret;
            }
        };

        struct PeerMsgNonAtomic
        {
            ::Json::StaticString label;
            std::uint64_t sent{0};
            std::uint64_t sentBytes{0};
            std::uint64_t received{0};
            std::uint64_t receivedBytes{0};

            PeerMsgNonAtomic(char const* l) : label(l)
            {
            }

            Json::Value
            to_json() const
            {
                Json::Value ret(Json::objectValue);
                ret[Json::StaticString("sent")] = std::to_string(sent);
                ret[Json::StaticString("sent_bytes")] =
                    std::to_string(sentBytes);
                ret[Json::StaticString("received")] = std::to_string(received);
                ret[Json::StaticString("received_bytes")] =
                    std::to_string(receivedBytes);
                return ret;
            }
        };

        struct Send
        {
            std::atomic<std::uint64_t> sent{0};
            std::atomic<std::uint64_t> sentBytes{0};
            std::atomic<std::uint64_t> sendFailedClosed{0};
            std::atomic<std::uint64_t> sendFailedAborted{0};
            std::atomic<std::uint64_t> sendFailedOther{0};
            std::atomic<std::uint64_t> sendQueueFailedGracefulClose{0};
            std::atomic<std::uint64_t> sendQueueFailedDetaching{0};
            std::atomic<std::uint64_t> sendQueueFailedSquelch{0};

            Json::Value
            to_json() const
            {
                Json::Value ret(Json::objectValue);
                ret[Json::StaticString("sent")] = std::to_string(sent.load());
                ret[Json::StaticString("sent_bytes")] =
                    std::to_string(sentBytes.load());
                ret[Json::StaticString("send_failed_closed")] =
                    std::to_string(sendFailedClosed.load());
                ret[Json::StaticString("send_failed_aborted")] =
                    std::to_string(sendFailedAborted.load());
                ret[Json::StaticString("send_failed_other")] =
                    std::to_string(sendFailedOther.load());
                ret[Json::StaticString("send_queue_failed_graceful_close")] =
                    std::to_string(sendQueueFailedGracefulClose.load());
                ret[Json::StaticString("send_queue_failed_detaching")] =
                    std::to_string(sendQueueFailedDetaching.load());
                ret[Json::StaticString("send_queue_failed_squelch")] =
                    std::to_string(sendQueueFailedSquelch.load());
                return ret;
            }
        };

        struct Receive
        {
            std::atomic<std::uint64_t> receiveFailedZeroSize{0};
            std::atomic<std::uint64_t> receiveFailedHeader{0};
            std::atomic<std::uint64_t> receiveFailedTooBig{0};
            std::atomic<std::uint64_t> receiveFailedCompressed{0};
            std::atomic<std::uint64_t> receivePackets{0};

            Json::Value
            to_json() const
            {
                Json::Value ret(Json::objectValue);
                ret[Json::StaticString("receive_failed_zero_size")] =
                    std::to_string(receiveFailedZeroSize.load());
                ret[Json::StaticString("receive_failed_header")] =
                    std::to_string(receiveFailedHeader.load());
                ret[Json::StaticString("receive_failed_too_big")] =
                    std::to_string(receiveFailedTooBig.load());
                ret[Json::StaticString("receive_failed_compressed")] =
                    std::to_string(receiveFailedCompressed.load());
                ret[Json::StaticString("receive_packets")] =
                    std::to_string(receivePackets.load());
                return ret;
            }
        };

        struct Connection
        {
            std::atomic<std::uint64_t> totalInboundAttempts{0};
            std::atomic<std::uint64_t> totalOutboundAttempts{0};
            std::atomic<std::uint64_t> totalInboundConnects{0};
            std::atomic<std::uint64_t> totalOutboundConnects{0};
            std::atomic<std::uint64_t> totalInboundDisconnects{0};
            std::atomic<std::uint64_t> totalOutboundDisconnects{0};
            std::atomic<std::uint64_t> disconnectInboundResources{0};
            std::atomic<std::uint64_t> disconnectOutboundResources{0};
            std::atomic<std::uint64_t> outboundConnectFailTimeouts{0};
            std::atomic<std::uint64_t> outboundConnectFailOnConnectError{0};
            std::atomic<std::uint64_t> outboundConnectFailOnHandshakeError{0};
            std::atomic<std::uint64_t> outboundConnectFailOnHandshakeDuplicate{
                0};
            std::atomic<std::uint64_t> outboundConnectFailOnWriteError{0};
            std::atomic<std::uint64_t> outboundConnectFailOnReadError{0};
            std::atomic<std::uint64_t> outboundConnectFailOnShutdownError{0};
            std::atomic<std::uint64_t> outboundConnectFailProtocol{0};
            std::atomic<std::uint64_t> outboundConnectFailSlotsFull{0};
            std::atomic<std::uint64_t> outboundConnectFailOnHandshakeFailure{0};
            std::atomic<std::uint64_t> outboundConnectCloseStop{0};
            std::atomic<std::uint64_t> outboundConnectCloseOnTimer{0};
            std::atomic<std::uint64_t> outboundConnectCloseOnHandshake{0};
            std::atomic<std::uint64_t> outboundConnectCloseOnShutdownNoError{0};
            std::atomic<std::uint64_t> outboundConnectCloseOnShutdown{0};
            std::atomic<std::uint64_t> outboundConnectCloseUpgrade{0};
            std::atomic<std::uint64_t> outboundConnectCloseShared{0};

            Json::Value
            to_json() const
            {
                Json::Value ret(Json::objectValue);
                ret[Json::StaticString("total_inbound_attempts")] =
                    std::to_string(totalInboundAttempts.load());
                ret[Json::StaticString("total_outbound_attempts")] =
                    std::to_string(totalOutboundAttempts.load());
                ret[Json::StaticString("total_inbound_connects")] =
                    std::to_string(totalInboundConnects.load());
                ret[Json::StaticString("total_outbound_connects")] =
                    std::to_string(totalOutboundConnects.load());
                ret[Json::StaticString("total_inbound_disconnects")] =
                    std::to_string(totalInboundDisconnects.load());
                ret[Json::StaticString("total_outbound_disconnects")] =
                    std::to_string(totalOutboundDisconnects.load());
                ret[Json::StaticString("disconnect_inbound_resources")] =
                    std::to_string(disconnectInboundResources.load());
                ret[Json::StaticString("disconnect_outbound_resources")] =
                    std::to_string(disconnectOutboundResources.load());
                ret[Json::StaticString("outbound_connect_fail_timeouts")] =
                    std::to_string(outboundConnectFailTimeouts.load());
                ret[Json::StaticString(
                    "outbound_connect_fail_on_connect_error")] =
                    std::to_string(outboundConnectFailOnConnectError.load());
                ret[Json::StaticString(
                    "outbound_connect_fail_on_handshake_error")] =
                    std::to_string(outboundConnectFailOnHandshakeError.load());
                ret[Json::StaticString(
                    "outbound_connect_fail_on_handshake_duplicate")] =
                    std::to_string(
                        outboundConnectFailOnHandshakeDuplicate.load());
                ret[Json::StaticString(
                    "outbound_connect_fail_on_write_error")] =
                    std::to_string(outboundConnectFailOnWriteError.load());
                ret[Json::StaticString("outbound_connect_fail_on_read_error")] =
                    std::to_string(outboundConnectFailOnReadError.load());
                ret[Json::StaticString(
                    "outbound_connect_fail_on_shutdown_error")] =
                    std::to_string(outboundConnectFailOnShutdownError.load());
                ret[Json::StaticString("outbound_connect_fail_protocol")] =
                    std::to_string(outboundConnectFailProtocol.load());
                ret[Json::StaticString("outbound_connect_fail_slots_full")] =
                    std::to_string(outboundConnectFailSlotsFull.load());
                ret[Json::StaticString(
                    "outbound_connect_fail_on_handshake_failure")] =
                    std::to_string(
                        outboundConnectFailOnHandshakeFailure.load());
                ret[Json::StaticString("outbound_connect_close_stop")] =
                    std::to_string(outboundConnectCloseStop.load());
                ret[Json::StaticString("outbound_connect_close_on_timer")] =
                    std::to_string(outboundConnectCloseOnTimer.load());
                ret[Json::StaticString("outbound_connect_close_on_handshake")] =
                    std::to_string(outboundConnectCloseOnHandshake.load());
                ret[Json::StaticString(
                    "outbound_connect_close_on_shutdown_no_error")] =
                    std::to_string(
                        outboundConnectCloseOnShutdownNoError.load());
                ret[Json::StaticString("outbound_connect_close_on_shutdown")] =
                    std::to_string(outboundConnectCloseOnShutdown.load());
                ret[Json::StaticString("outbound_connect_close_upgrade")] =
                    std::to_string(outboundConnectCloseUpgrade.load());
                ret[Json::StaticString("outbound_connect_close_shared")] =
                    std::to_string(outboundConnectCloseShared.load());

                return ret;
            }
        };

        std::unordered_map<int, PeerMsg> msgs;
        Send send;
        Receive receive;
        Connection connection;

        Peer()
        {
            // from ripple.proto
            msgs.insert({2, "mtMANIFESTS"});
            msgs.insert({3, "mtPING"});
            msgs.insert({5, "mtCLUSTER"});
            msgs.insert({15, "mtENDPOINTS"});
            msgs.insert({30, "mtTRANSACTION"});
            msgs.insert({31, "mtGET_LEDGER"});
            msgs.insert({32, "mtLEDGER_DATA"});
            msgs.insert({33, "mtPROPOSE_LEDGER"});
            msgs.insert({34, "mtSTATUS_CHANGE"});
            msgs.insert({35, "mtHAVE_SET"});
            msgs.insert({41, "mtVALIDATION"});
            msgs.insert({42, "mtGET_OBJECTS"});
            msgs.insert({54, "mtVALIDATORLIST"});
            msgs.insert({55, "mtSQUELCH"});
            msgs.insert({56, "mtVALIDATORLISTCOLLECTION"});
            msgs.insert({57, "mtPROOF_PATH_REQ"});
            msgs.insert({58, "mtPROOF_PATH_RESPONSE"});
            msgs.insert({59, "mtREPLAY_DELTA_REQ"});
            msgs.insert({60, "mtREPLAY_DELTA_RESPONSE"});
            msgs.insert({63, "mtHAVE_TRANSACTIONS"});
            msgs.insert({64, "mtTRANSACTIONS"});
        }

        void
        queuedPeerMessage(
            int const type,
            std::size_t const numBytes,
            beast::Journal const& j)
        {
            auto found = msgs.find(type);
            if (found != msgs.end())
            {
                ++found->second.sent;
                found->second.sentBytes += numBytes;
            }
            else
            {
                auto unknown = msgs.insert({-1, "UNKNOWN"});
                ++unknown.first->second.sent;
                unknown.first->second.sentBytes += numBytes;
                JLOG(j.error()) << "queued unknown peer message type " << type;
            }
        }

        void
        receivedPeerMessage(
            int const type,
            std::size_t const numBytes,
            beast::Journal const& j)
        {
            auto found = msgs.find(type);
            if (found != msgs.end())
            {
                ++found->second.received;
                found->second.receivedBytes += numBytes;
            }
            else
            {
                auto unknown = msgs.insert({-1, "UNKNOWN"});
                ++unknown.first->second.received;
                unknown.first->second.receivedBytes += numBytes;
                JLOG(j.error()) << "queued unknown peer message type " << type;
            }
        }

        Json::Value
        to_json() const
        {
            Json::Value ret(Json::objectValue);
            Json::Value byMessage(Json::objectValue);
            PeerMsgNonAtomic total("total");
            for (auto const& [type, stats] : msgs)
            {
                if (stats.sent || stats.sentBytes || stats.received ||
                    stats.receivedBytes)
                {
                    byMessage[stats.label] = stats.to_json();
                    total.sent += stats.sent;
                    total.sentBytes += stats.sentBytes;
                    total.received += stats.received;
                    total.receivedBytes += stats.receivedBytes;
                }
            }
            byMessage[total.label] = total.to_json();
            ret[Json::StaticString("by_message")] = byMessage;
            ret[Json::StaticString("send")] = send.to_json();
            ret[Json::StaticString("receive")] = receive.to_json();
            ret[Json::StaticString("connection")] = connection.to_json();
            return ret;
        }
    };

    virtual ~PerfLog() = default;

    virtual void
    start()
    {
    }

    virtual void
    stop()
    {
    }

    /**
     * Log start of RPC call.
     *
     * @param method RPC command
     * @param requestId Unique identifier to track command
     */
    virtual void
    rpcStart(std::string const& method, std::uint64_t requestId) = 0;

    /**
     * Log successful finish of RPC call
     *
     * @param method RPC command
     * @param requestId Unique identifier to track command
     */
    virtual void
    rpcFinish(std::string const& method, std::uint64_t requestId) = 0;

    /**
     * Log errored RPC call
     *
     * @param method RPC command
     * @param requestId Unique identifier to track command
     */
    virtual void
    rpcError(std::string const& method, std::uint64_t requestId) = 0;

    /**
     * Log queued job
     *
     * @param type Job type
     */
    virtual void
    jobQueue(JobType const type) = 0;

    /**
     * Log job executing
     *
     * @param type Job type
     * @param dur Duration enqueued in microseconds
     * @param startTime Time that execution began
     * @param instance JobQueue worker thread instance
     */
    virtual void
    jobStart(
        JobType const type,
        microseconds dur,
        steady_time_point startTime,
        int instance) = 0;

    /**
     * Log job finishing
     *
     * @param type Job type
     * @param dur Duration running in microseconds
     * @param instance Jobqueue worker thread instance
     */
    virtual void
    jobFinish(JobType const type, microseconds dur, int instance) = 0;

    /**
     * Render performance counters in Json
     *
     * @return Counters Json object
     */
    virtual Json::Value
    countersJson() const = 0;

    /**
     * Render currently executing jobs and RPC calls and durations in Json
     *
     * @return Current executing jobs and RPC calls and durations
     */
    virtual Json::Value
    currentJson() const = 0;

    /**
     * Ensure enough room to store each currently executing job
     *
     * @param resize Number of JobQueue worker threads
     */
    virtual void
    resizeJobs(int const resize) = 0;

    /**
     * Rotate perf log file
     */
    virtual void
    rotate() = 0;

    virtual Peer&
    getPeerCounters() = 0;
};

PerfLog::Setup
setup_PerfLog(Section const& section, boost::filesystem::path const& configDir);

std::unique_ptr<PerfLog>
make_PerfLog(
    PerfLog::Setup const& setup,
    Application& app,
    beast::Journal journal,
    std::function<void()>&& signalStop);

template <typename Func, class Rep, class Period>
auto
measureDurationAndLog(
    Func&& func,
    std::string const& actionDescription,
    std::chrono::duration<Rep, Period> maxDelay,
    beast::Journal const& journal)
{
    auto start_time = std::chrono::high_resolution_clock::now();

    auto result = func();

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (duration > maxDelay)
    {
        JLOG(journal.warn())
            << actionDescription << " took " << duration.count() << " ms";
    }

    return result;
}

}  // namespace perf
}  // namespace ripple

#endif  // RIPPLE_BASICS_PERFLOG_H
