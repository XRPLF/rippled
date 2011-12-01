#ifndef __TIMINGSERVICE__
#define __TIMINGSERVICE__

#include <boost/asio.hpp>

/* responsible for keeping track of network time 
and kicking off the publishing process
*/

class TimingService
{
	boost::asio::deadline_timer* mLedgerTimer;
	boost::asio::deadline_timer* mPropTimer;
	boost::asio::deadline_timer* mValidTimer;

	void handleLedger();
	void handleProp();
	void handleValid();
public:
	TimingService();
	void start(boost::asio::io_service& ioService);

	static int getCurrentLedgerIndex();

};
#endif
