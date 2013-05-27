namespace websocketpp
{
	namespace log
	{
		LogPartition websocketPartition("WebSocket");

		void websocketLog(websocketpp::log::alevel::value v, const std::string& entry)
		{
			if ((v == websocketpp::log::alevel::DEVEL) || (v == websocketpp::log::alevel::DEBUG_CLOSE))
			{
				if (websocketPartition.doLog(lsTRACE))
					Log(lsDEBUG, websocketPartition) << entry;
			}
			else if (websocketPartition.doLog(lsDEBUG))
				Log(lsDEBUG, websocketPartition) << entry;
		}

		void websocketLog(websocketpp::log::elevel::value v, const std::string& entry)
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
			if (websocketPartition.doLog(s))
				Log(s, websocketPartition) << entry;
		}

	}
}

// vim:ts=4
