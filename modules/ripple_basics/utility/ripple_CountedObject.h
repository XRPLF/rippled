//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_COUNTEDOBJECT_RIPPLEHEADER
#define RIPPLE_COUNTEDOBJECT_RIPPLEHEADER

//------------------------------------------------------------------------------

/** Manages all counted object types.
*/
class CountedObjects
{
public:
    static CountedObjects& getInstance ();

    typedef std::pair <std::string, int> Entry;
    typedef std::vector <Entry> List;

    List getCounts (int minimumThreshold) const;

public:
    /** Implementation for @ref CountedObject.

        @internal
    */
    class CounterBase
    {
    public:
        CounterBase ();

        virtual ~CounterBase ();

        inline int increment () noexcept
        {
            return ++m_count;
        }

        inline int decrement () noexcept
        {
            return --m_count;
        }

        inline int getCount () const noexcept
        {
            return m_count.get ();
        }

        inline CounterBase* getNext () const noexcept
        {
            return m_next;
        }

        virtual char const* getName () const = 0;

    private:
        virtual void checkPureVirtual () const = 0;

    protected:
        beast::Atomic <int> m_count;
        CounterBase* m_next;
    };

private:
    CountedObjects ();

    ~CountedObjects ();

private:
    beast::Atomic <int> m_count;
    beast::Atomic <CounterBase*> m_head;
};

//------------------------------------------------------------------------------

/** Tracks the number of instances of an object.

    Derived classes have their instances counted automatically. This is used
    for reporting purposes.

    @ingroup ripple_basics
*/
template <class Object>
class CountedObject
{
public:
    CountedObject ()
    {
        getCounter ().increment ();
    }

    CountedObject (CountedObject const&)
    {
        getCounter ().increment ();
    }

    ~CountedObject ()
    {
        getCounter ().decrement ();
    }

private:
    class Counter : public CountedObjects::CounterBase
    {
    public:
        Counter () noexcept { }

        char const* getName () const noexcept
        {
            return getClassName ();
        }

        void checkPureVirtual () const { }
    };

private:
    /* Due to a bug in Visual Studio 10 and earlier, the string returned by
       typeid().name() will appear to leak on exit. Therefore, we should
       only call this function when there's an actual leak, or else there
       will be spurious leak notices at exit.
    */
    static char const* getClassName () noexcept
    {
        return typeid (Object).name ();
    }

    static Counter& getCounter () noexcept
    {
        // VFALCO TODO Research the thread safety of static initializers
        //             on all supported platforms
        static Counter counter;
        return counter;

        /*
        static Counter* volatile s_instance;
        static beast::Static::Initializer s_initializer;
        if (s_initializer.begin ())
        {
            static char s_storage [sizeof (Counter)];
            s_instance = new (s_storage) Counter;
            s_initializer.end ();
        }
        return *s_instance;
        */
    }
};

#endif
