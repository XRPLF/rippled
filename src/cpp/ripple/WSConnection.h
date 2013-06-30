//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================


#ifndef RIPPLE_WSCONNECTION_H
#define RIPPLE_WSCONNECTION_H

// This is for logging
struct WSConnectionLog;

template <typename endpoint_type>
class WSServerHandler;
//
// Storage for connection specific info
// - Subscriptions
//
template <typename endpoint_type>
class WSConnection
    : public InfoSub
    , public boost::enable_shared_from_this< WSConnection<endpoint_type> >
    , public CountedObject <WSConnection <endpoint_type> >
{
public:
    static char const* getCountedObjectName () { return "WSConnection"; }

    typedef typename endpoint_type::connection_type connection;
    typedef typename boost::shared_ptr<connection> connection_ptr;
    typedef typename boost::weak_ptr<connection> weak_connection_ptr;
    typedef typename endpoint_type::handler::message_ptr message_ptr;

public:
    //  WSConnection()
    //      : mHandler((WSServerHandler<websocketpp::WSDOOR_SERVER>*)(NULL)),
    //          mConnection(connection_ptr()) { ; }

    WSConnection (WSServerHandler<endpoint_type>* wshpHandler, const connection_ptr& cpConnection)
        : mHandler (wshpHandler), mConnection (cpConnection), mNetwork (getApp().getOPs ()),
          mRemoteIP (cpConnection->get_socket ().lowest_layer ().remote_endpoint ().address ().to_string ()),
          mLoadSource (mRemoteIP), mPingTimer (cpConnection->get_io_service ()), mPinged (false),
          mRcvQueueRunning (false), mDead (false)
    {
        WriteLog (lsDEBUG, WSConnectionLog) << "Websocket connection from " << mRemoteIP;
        setPingTimer ();
    }

    void preDestroy ()
    {
        // sever connection
        mPingTimer.cancel ();
        mConnection.reset ();

        boost::recursive_mutex::scoped_lock sl (mRcvQueueLock);
        mDead = true;
    }

    virtual ~WSConnection ()
    {
        ;
    }

    static void destroy (boost::shared_ptr< WSConnection<endpoint_type> >)
    {
        // Just discards the reference
    }

    // Implement overridden functions from base class:
    void send (const Json::Value& jvObj, bool broadcast)
    {
        connection_ptr ptr = mConnection.lock ();

        if (ptr)
            mHandler->send (ptr, jvObj, broadcast);
    }

    void send (const Json::Value& jvObj, const std::string& sObj, bool broadcast)
    {
        connection_ptr ptr = mConnection.lock ();

        if (ptr)
            mHandler->send (ptr, sObj, broadcast);
    }

    // Utilities
    Json::Value invokeCommand (Json::Value& jvRequest)
    {
        if (getApp().getLoadManager ().shouldCutoff (mLoadSource))
        {
            // VFALCO TODO This must be implemented before open sourcing

#if SHOULD_DISCONNECT
            // FIXME: Must dispatch to strand
            connection_ptr ptr = mConnection.lock ();

            if (ptr)
                ptr->close (websocketpp::close::status::PROTOCOL_ERROR, "overload");

            return rpcError (rpcSLOW_DOWN);
#endif
        }

        // Requests without "command" are invalid.
        //
        if (!jvRequest.isMember ("command"))
        {
            Json::Value jvResult (Json::objectValue);

            jvResult["type"]    = "response";
            jvResult["status"]  = "error";
            jvResult["error"]   = "missingCommand";
            jvResult["request"] = jvRequest;

            if (jvRequest.isMember ("id"))
            {
                jvResult["id"]  = jvRequest["id"];
            }

            getApp().getLoadManager ().applyLoadCharge (mLoadSource, LT_RPCInvalid);

            return jvResult;
        }

        LoadType loadType = LT_RPCReference;
        RPCHandler  mRPCHandler (&mNetwork, boost::dynamic_pointer_cast<InfoSub> (this->shared_from_this ()));
        Json::Value jvResult (Json::objectValue);

        int iRole   = mHandler->getPublic ()
                      ? RPCHandler::GUEST     // Don't check on the public interface.
                      : iAdminGet (jvRequest, mRemoteIP);

        if (RPCHandler::FORBID == iRole)
        {
            jvResult["result"]  = rpcError (rpcFORBIDDEN);
        }
        else
        {
            jvResult["result"] = mRPCHandler.doCommand (jvRequest, iRole, &loadType);
        }

        // Debit/credit the load and see if we should include a warning.
        //
        if (getApp().getLoadManager ().applyLoadCharge (mLoadSource, loadType) &&
            getApp().getLoadManager ().shouldWarn (mLoadSource))
        {
            jvResult["warning"] = "load";
        }

        // Currently we will simply unwrap errors returned by the RPC
        // API, in the future maybe we can make the responses
        // consistent.
        //
        // Regularize result. This is duplicate code.
        if (jvResult["result"].isMember ("error"))
        {
            jvResult            = jvResult["result"];
            jvResult["status"]  = "error";
            jvResult["request"] = jvRequest;

        }
        else
        {
            jvResult["status"]  = "success";
        }

        if (jvRequest.isMember ("id"))
        {
            jvResult["id"]      = jvRequest["id"];
        }

        jvResult["type"]        = "response";

        return jvResult;
    }

    bool onPingTimer (std::string&)
    {
#ifdef DISCONNECT_ON_WEBSOCKET_PING_TIMEOUTS

        if (mPinged)
            return true; // causes connection to close

#endif
        mPinged = true;
        setPingTimer ();
        return false; // causes ping to be sent
    }

    void onPong (const std::string&)
    {
        mPinged = false;
    }

    static void pingTimer (weak_connection_ptr c, WSServerHandler<endpoint_type>* h, const boost::system::error_code& e)
    {
        if (e)
            return;

        connection_ptr ptr = c.lock ();

        if (ptr)
            h->pingTimer (ptr);
    }

    void setPingTimer ()
    {
        connection_ptr ptr = mConnection.lock ();

        if (ptr)
        {
            mPingTimer.expires_from_now (boost::posix_time::seconds (theConfig.WEBSOCKET_PING_FREQ));
            mPingTimer.async_wait (ptr->get_strand ().wrap (boost::bind (
                                       &WSConnection<endpoint_type>::pingTimer, mConnection, mHandler, boost::asio::placeholders::error)));
        }
    }

    void rcvMessage (message_ptr msg, bool& msgRejected, bool& runQueue)
    {
        boost::recursive_mutex::scoped_lock sl (mRcvQueueLock);

        if (mDead)
        {
            msgRejected = false;
            runQueue = false;
            return;
        }

        if (mDead || (mRcvQueue.size () >= 1000))
        {
            msgRejected = !mDead;
            runQueue = false;
        }
        else
        {
            msgRejected = false;
            mRcvQueue.push (msg);

            if (mRcvQueueRunning)
                runQueue = false;
            else
            {
                runQueue = true;
                mRcvQueueRunning = true;
            }
        }
    }

    message_ptr getMessage ()
    {
        boost::recursive_mutex::scoped_lock sl (mRcvQueueLock);

        if (mDead || mRcvQueue.empty ())
        {
            mRcvQueueRunning = false;
            return message_ptr ();
        }

        message_ptr m = mRcvQueue.front ();
        mRcvQueue.pop ();
        return m;
    }

private:
    typedef void (WSConnection::*doFuncPtr) (Json::Value& jvResult, Json::Value& jvRequest);

    WSServerHandler<endpoint_type>*     mHandler;
    weak_connection_ptr                 mConnection;
    NetworkOPs&                         mNetwork;
    std::string                         mRemoteIP;
    LoadSource                          mLoadSource;

    boost::asio::deadline_timer         mPingTimer;
    bool                                mPinged;

    boost::recursive_mutex              mRcvQueueLock;
    std::queue<message_ptr>             mRcvQueue;
    bool                                mRcvQueueRunning;
    bool                                mDead;
};

#endif

// vim:ts=4
