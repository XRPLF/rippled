#ifndef RIPPLE_LOADEVENT_H
#define RIPPLE_LOADEVENT_H

class LoadMonitor;

// VFALCO NOTE What is the difference between a LoadEvent and a LoadMonitor?
// VFALCO TODO Rename LoadEvent to LoadMonitor::Event
//
//        This looks like a scoped elapsed time measuring class
//
class LoadEvent
{
public:
    // VFALCO NOTE Why are these shared pointers? Wouldn't there be a
    //             piece of lifetime-managed calling code that can simply own
    //             the object?
    //
    //             Why both kinds of containers?
    //
    typedef boost::shared_ptr <LoadEvent> pointer;
    typedef UPTR_T <LoadEvent>            autoptr;

public:
    // VFALCO TODO remove the dependency on LoadMonitor. Is that possible?
    LoadEvent (LoadMonitor& monitor,
               const std::string& name,
               bool shouldStart);

    ~LoadEvent ();

    // VFALCO TODO rename this to setName () or setLabel ()
    void reName (const std::string& name);

    // okay to call if already started
    void start ();

    void stop ();

private:
    LoadMonitor&                mMonitor;
    bool                        mRunning;
    std::string                 mName;
    boost::posix_time::ptime    mStartTime;
};

#endif
