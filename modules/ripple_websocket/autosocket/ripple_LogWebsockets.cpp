//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// VFALCO NOTE this looks like some facility for giving websocket
//         a way to produce logging output.
//
namespace websocketpp
{
namespace log
{

#if RIPPLE_USE_NAMESPACE
using namespace ripple;

LogPartition websocketPartition ("WebSocket");

void websocketLog (websocketpp::log::alevel::value v, const std::string& entry)
{
    using namespace ripple;

    if ((v == websocketpp::log::alevel::DEVEL) || (v == websocketpp::log::alevel::DEBUG_CLOSE))
    {
        if (websocketPartition.doLog (lsTRACE))
            Log (lsDEBUG, websocketPartition) << entry;
    }
    else if (websocketPartition.doLog (lsDEBUG))
        Log (lsDEBUG, websocketPartition) << entry;
}

void websocketLog (websocketpp::log::elevel::value v, const std::string& entry)
{
    using namespace ripple;

    LogSeverity s = lsDEBUG;

    if ((v & websocketpp::log::elevel::INFO) != 0)
        s = lsINFO;
    else if ((v & websocketpp::log::elevel::FATAL) != 0)
        s = lsFATAL;
    else if ((v & websocketpp::log::elevel::RERROR) != 0)
        s = lsERROR;
    else if ((v & websocketpp::log::elevel::WARN) != 0)
        s = lsWARNING;

    if (websocketPartition.doLog (s))
        Log (s, websocketPartition) << entry;
}
#else
LogPartition websocketPartition ("WebSocket");

void websocketLog (websocketpp::log::alevel::value v, const std::string& entry)
{
    if ((v == websocketpp::log::alevel::DEVEL) || (v == websocketpp::log::alevel::DEBUG_CLOSE))
    {
        if (websocketPartition.doLog (lsTRACE))
            Log (lsDEBUG, websocketPartition) << entry;
    }
    else if (websocketPartition.doLog (lsDEBUG))
        Log (lsDEBUG, websocketPartition) << entry;
}

void websocketLog (websocketpp::log::elevel::value v, const std::string& entry)
{
    LogSeverity s = lsDEBUG;

    if ((v & websocketpp::log::elevel::INFO) != 0)
        s = lsINFO;
    else if ((v & websocketpp::log::elevel::FATAL) != 0)
        s = lsFATAL;
    else if ((v & websocketpp::log::elevel::RERROR) != 0)
        s = lsERROR;
    else if ((v & websocketpp::log::elevel::WARN) != 0)
        s = lsWARNING;

    if (websocketPartition.doLog (s))
        Log (s, websocketPartition) << entry;
}
#endif

}
}

// vim:ts=4
