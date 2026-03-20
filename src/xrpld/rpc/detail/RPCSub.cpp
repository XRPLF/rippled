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
        boost::asio::io_context& ioContext,
        JobQueue& jobQueue,
        std::string const& strUrl,
        std::string const& strUsername,
        std::string const& strPassword,
        Logs& logs)
        : RPCSub(source)
        , io_context_(ioContext)
        , jobQueue_(jobQueue)
        , url_(strUrl)
        , username_(strUsername)
        , password_(strPassword)
        , j_(logs.journal("RPCSub"))
        , logs_(logs)
    {
        parsedURL pUrl;

        if (!parseUrl(pUrl, strUrl))
        {
            Throw<std::runtime_error>("Failed to parse url.");
        }
        else if (pUrl.scheme == "https")
        {
            ssl_ = true;
        }
        else if (pUrl.scheme != "http")
        {
            Throw<std::runtime_error>("Only http and https is supported.");
        }

        seq_ = 1;

        ip_ = pUrl.domain;
        if (!pUrl.port)
        {
            port_ = ssl_ ? 443 : 80;
        }
        else
        {
            port_ = *pUrl.port;
        }
        path_ = pUrl.path;

        JLOG(j_.info()) << "RPCCall::fromNetwork sub: ip=" << ip_ << " port=" << port_
                        << " ssl= " << (ssl_ ? "yes" : "no") << " path='" << path_ << "'";
    }

    ~RPCSubImp() = default;

    void
    send(Json::Value const& jvObj, bool broadcast) override
    {
        std::lock_guard sl(lock_);

        auto jm = broadcast ? j_.debug() : j_.info();
        JLOG(jm) << "RPCCall::fromNetwork push: " << jvObj;

        deque_.push_back(std::make_pair(seq_++, jvObj));

        if (!sending_)
        {
            // Start a sending thread.
            JLOG(j_.info()) << "RPCCall::fromNetwork start";

            sending_ =
                jobQueue_.addJob(jtCLIENT_SUBSCRIBE, "RPCSubSendThr", [this]() { sendThread(); });
        }
    }

    void
    setUsername(std::string const& strUsername) override
    {
        std::lock_guard sl(lock_);

        username_ = strUsername;
    }

    void
    setPassword(std::string const& strPassword) override
    {
        std::lock_guard sl(lock_);

        password_ = strPassword;
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
                std::lock_guard sl(lock_);

                if (deque_.empty())
                {
                    sending_ = false;
                    bSend = false;
                }
                else
                {
                    auto const [seq, env] = deque_.front();

                    deque_.pop_front();

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
                    JLOG(j_.info()) << "RPCCall::fromNetwork: " << ip_;

                    RPCCall::fromNetwork(
                        io_context_,
                        ip_,
                        port_,
                        username_,
                        password_,
                        path_,
                        "event",
                        jvEvent,
                        ssl_,
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
    boost::asio::io_context& io_context_;
    JobQueue& jobQueue_;

    std::string url_;
    std::string ip_;
    std::uint16_t port_;
    bool ssl_{false};
    std::string username_;
    std::string password_;
    std::string path_;

    int seq_;  // Next id to allocate.

    bool sending_{false};  // Sending thread is active.

    std::deque<std::pair<int, Json::Value>> deque_;

    beast::Journal const j_;
    Logs& logs_;
};

//------------------------------------------------------------------------------

RPCSub::RPCSub(InfoSub::Source& source) : InfoSub(source, Consumer())
{
}

std::shared_ptr<RPCSub>
makeRPCSub(
    InfoSub::Source& source,
    boost::asio::io_context& ioContext,
    JobQueue& jobQueue,
    std::string const& strUrl,
    std::string const& strUsername,
    std::string const& strPassword,
    Logs& logs)
{
    return std::make_shared<RPCSubImp>(
        std::ref(source),
        std::ref(ioContext),
        std::ref(jobQueue),
        strUrl,
        strUsername,
        strPassword,
        logs);
}

}  // namespace xrpl
