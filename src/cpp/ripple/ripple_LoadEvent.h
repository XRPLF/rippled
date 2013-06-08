#ifndef RIPPLE_LOADEVENT_H
#define RIPPLE_LOADEVENT_H

class LoadMonitor;

class LoadEvent
{
public:
	typedef boost::shared_ptr<LoadEvent>	pointer;
	typedef UPTR_T<LoadEvent>				autoptr;

public:
    // VFALCO TODO remove the dependency on LoadMonitor.
	LoadEvent (LoadMonitor& monitor,
               const std::string& name,
               bool shouldStart);

	~LoadEvent();

    // VFALCO TODO rename this to setName () or setLabel ()
	void reName (const std::string& name);

    // okay to call if already started
	void start();

	void stop();

private:
	LoadMonitor&				mMonitor;
	bool						mRunning;
	std::string					mName;
	boost::posix_time::ptime	mStartTime;
};

#endif
