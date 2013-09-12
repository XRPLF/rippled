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
class CountedObject : LeakChecked <CountedObject <Object> >
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
            return Object::getCountedObjectName ();
        }

        void checkPureVirtual () const { }
    };

private:
    static Counter& getCounter () noexcept
    {
        // VFALCO TODO Research the thread safety of static initializers
        //             on all supported platforms
        return StaticObject <Counter>::get();
    }
};

#endif
