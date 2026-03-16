#include <xrpld/rpc/RPCCall.h>
#include <xrpld/rpc/RPCSub.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/contract.h>
#include <xrpl/json/to_string.h>

#include <deque>

namespace xrpl {

// Subscription object for JSON-RPC
class RPCSubImp : public RPCSub
{
public:
    RPCSubImp(
        InfoSub::Source& source,
        JobQueue& jobQueue,
        std::string const& strUrl,
        std::string const& strUsername,
        std::string const& strPassword,
        Logs& logs)
        : RPCSub(source)
        , m_jobQueue(jobQueue)
        , mUrl(strUrl)
        , mSSL(false)
        , mUsername(strUsername)
        , mPassword(strPassword)
        , mSending(false)
        , j_(logs.journal("RPCSub"))
        , logs_(logs)
    {
        parsedURL pUrl;

        if (!parseUrl(pUrl, strUrl))
            Throw<std::runtime_error>("Failed to parse url.");
        else if (pUrl.scheme == "https")
            mSSL = true;
        else if (pUrl.scheme != "http")
            Throw<std::runtime_error>("Only http and https is supported.");

        mSeq = 1;

        mIp = pUrl.domain;
        mPort = (!pUrl.port) ? (mSSL ? 443 : 80) : *pUrl.port;
        mPath = pUrl.path;

        JLOG(j_.info()) << "RPCCall::fromNetwork sub: ip=" << mIp << " port=" << mPort
                        << " ssl= " << (mSSL ? "yes" : "no") << " path='" << mPath << "'";
    }

    ~RPCSubImp() = default;

    void
    send(Json::Value const& jvObj, bool broadcast) override
    {
        std::lock_guard sl(mLock);

        if (mDeque.size() >= maxQueueSize)
        {
            JLOG(j_.warn()) << "RPCCall::fromNetwork drop: queue full (" << mDeque.size()
                            << "), seq=" << mSeq << ", endpoint=" << mIp;
            ++mSeq;
            return;
        }

        auto jm = broadcast ? j_.debug() : j_.info();
        JLOG(jm) << "RPCCall::fromNetwork push: " << jvObj;

        mDeque.push_back(std::make_pair(mSeq++, jvObj));

        if (!mSending)
        {
            // Start a sending thread.
            JLOG(j_.info()) << "RPCCall::fromNetwork start";

            mSending =
                m_jobQueue.addJob(jtCLIENT_SUBSCRIBE, "RPCSubSendThr", [this]() { sendThread(); });
        }
    }

    void
    setUsername(std::string const& strUsername) override
    {
        std::lock_guard sl(mLock);

        mUsername = strUsername;
    }

    void
    setPassword(std::string const& strPassword) override
    {
        std::lock_guard sl(mLock);

        mPassword = strPassword;
    }

private:
    // Maximum concurrent HTTP deliveries per batch. Bounds file
    // descriptor usage while still allowing parallel delivery to
    // capable endpoints.
    static constexpr int maxInFlight = 32;

    // Maximum queued events before dropping. Consumers detect
    // gaps via the seq field.
    static constexpr std::size_t maxQueueSize = 16384;

    void
    sendThread()
    {
        try
        {
            for (;;)
            {
                boost::asio::io_context io_context;
                int dispatched = 0;

                {
                    std::lock_guard sl(mLock);

                    while (!mDeque.empty() && dispatched < maxInFlight)
                    {
                        auto const [seq, env] = mDeque.front();
                        mDeque.pop_front();

                        Json::Value jvEvent = env;
                        jvEvent["seq"] = seq;

                        RPCCall::fromNetwork(
                            io_context,
                            mIp,
                            mPort,
                            mUsername,
                            mPassword,
                            mPath,
                            "event",
                            jvEvent,
                            mSSL,
                            true,
                            logs_);
                        ++dispatched;
                    }

                    if (dispatched == 0)
                    {
                        // Reset under the lock to avoid a lost-wakeup race with send().
                        mSending = false;
                        return;
                    }
                }

                JLOG(j_.info()) << "RPCCall::fromNetwork: " << mIp << " dispatching " << dispatched
                                << " events";
                try
                {
                    io_context.run();
                }
                catch (std::exception const& e)
                {
                    JLOG(j_.warn()) << "io_context.run exception: " << e.what();
                    std::lock_guard sl(mLock);
                    mSending = false;
                    return;
                }
            }
        }
        catch (std::exception const& e)
        {
            JLOG(j_.warn()) << "sendThread exception: " << e.what();
            std::lock_guard sl(mLock);
            mSending = false;
        }
    }

private:
    JobQueue& m_jobQueue;

    std::string mUrl;
    std::string mIp;
    std::uint16_t mPort;
    bool mSSL;
    std::string mUsername;
    std::string mPassword;
    std::string mPath;

    int mSeq;  // Next id to allocate.

    bool mSending;  // Sending thread is active.

    std::deque<std::pair<int, Json::Value>> mDeque;

    beast::Journal const j_;
    Logs& logs_;
};

//------------------------------------------------------------------------------

RPCSub::RPCSub(InfoSub::Source& source) : InfoSub(source, Consumer())
{
}

std::shared_ptr<RPCSub>
make_RPCSub(
    InfoSub::Source& source,
    JobQueue& jobQueue,
    std::string const& strUrl,
    std::string const& strUsername,
    std::string const& strPassword,
    Logs& logs)
{
    return std::make_shared<RPCSubImp>(
        std::ref(source), std::ref(jobQueue), strUrl, strUsername, strPassword, logs);
}

}  // namespace xrpl
