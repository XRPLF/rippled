#include <xrpld/rpc/RPCSub.h>

#include <xrpld/rpc/RPCCall.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/contract.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>  // IWYU pragma: keep
#include <xrpl/server/InfoSub.h>

#include <boost/asio/io_context.hpp>

#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

// Subscription object for JSON-RPC
class RPCSubImp : public RPCSub
{
public:
    RPCSubImp(
        InfoSub::Source& source,
        boost::asio::io_context& io_context,
        JobQueue& jobQueue,
        std::string const& strUrl,
        std::string strUsername,
        std::string strPassword,
        ServiceRegistry& registry)
        : RPCSub(source)
        , m_io_context(io_context)
        , m_jobQueue(jobQueue)
        , mUrl(strUrl)
        , mUsername(std::move(strUsername))
        , mPassword(std::move(strPassword))
        , j_(registry.getJournal("RPCSub"))
        , logs_(registry.getLogs())
    {
        parsedURL pUrl;

        if (!parseUrl(pUrl, strUrl))
        {
            Throw<std::runtime_error>("Failed to parse url.");
        }
        else if (pUrl.scheme == "https")
        {
            mSSL = true;
        }
        else if (pUrl.scheme != "http")
        {
            Throw<std::runtime_error>("Only http and https is supported.");
        }

        mSeq = 1;

        mIp = pUrl.domain;
        if (!pUrl.port)
        {
            mPort = mSSL ? 443 : 80;
        }
        else
        {
            mPort = *pUrl.port;
        }
        mPath = pUrl.path;

        JLOG(j_.info()) << "RPCCall::fromNetwork sub: ip=" << mIp << " port=" << mPort
                        << " ssl= " << (mSSL ? "yes" : "no") << " path='" << mPath << "'";
    }

    ~RPCSubImp() override = default;

    void
    send(Json::Value const& jvObj, bool broadcast) override
    {
        std::scoped_lock const sl(mLock);

        auto jm = broadcast ? j_.debug() : j_.info();
        JLOG(jm) << "RPCCall::fromNetwork push: " << jvObj;

        mDeque.emplace_back(mSeq++, jvObj);

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
        std::scoped_lock const sl(mLock);

        mUsername = strUsername;
    }

    void
    setPassword(std::string const& strPassword) override
    {
        std::scoped_lock const sl(mLock);

        mPassword = strPassword;
    }

private:
    // XXX Could probably create a bunch of send jobs in a single get of the
    // lock.
    void
    sendThread()
    {
        Json::Value jvEvent;
        bool bSend = false;

        do
        {
            {
                // Obtain the lock to manipulate the queue and change sending.
                std::scoped_lock const sl(mLock);

                if (mDeque.empty())
                {
                    mSending = false;
                    bSend = false;
                }
                else
                {
                    auto const [seq, env] = mDeque.front();

                    mDeque.pop_front();

                    jvEvent = env;
                    jvEvent["seq"] = seq;

                    bSend = true;
                }
            }

            // Send outside of the lock.
            if (bSend)
            {
                // XXX Might not need this in a try.
                try
                {
                    JLOG(j_.info()) << "RPCCall::fromNetwork: " << mIp;

                    RPCCall::fromNetwork(
                        m_io_context,
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
                }
                catch (std::exception const& e)
                {
                    JLOG(j_.info()) << "RPCCall::fromNetwork exception: " << e.what();
                }
            }
        } while (bSend);
    }

private:
    boost::asio::io_context& m_io_context;
    JobQueue& m_jobQueue;

    std::string mUrl;
    std::string mIp;
    std::uint16_t mPort;
    bool mSSL{false};
    std::string mUsername;
    std::string mPassword;
    std::string mPath;

    int mSeq;  // Next id to allocate.

    bool mSending{false};  // Sending thread is active.

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
    boost::asio::io_context& io_context,
    JobQueue& jobQueue,
    std::string const& strUrl,
    std::string const& strUsername,
    std::string const& strPassword,
    ServiceRegistry& registry)
{
    return std::make_shared<RPCSubImp>(
        std::ref(source),
        std::ref(io_context),
        std::ref(jobQueue),
        strUrl,
        strUsername,
        strPassword,
        registry);
}

}  // namespace xrpl
