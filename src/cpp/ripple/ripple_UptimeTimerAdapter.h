#ifndef RIPPLE_UPTIMETIMERADAPTER_H
#define RIPPLE_UPTIMETIMERADAPTER_H

/** Adapter providing uptime measurements for template classes.
*/
struct UptimeTimerAdapter
{
	inline static int getElapsedSeconds ()
	{
		return UptimeTimer::getInstance().getElapsedSeconds ();
	}
};

#endif
