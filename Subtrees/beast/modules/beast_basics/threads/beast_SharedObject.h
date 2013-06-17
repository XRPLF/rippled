/*============================================================================*/
/*
  VFLib: https://github.com/vinniefalco/VFLib

  Copyright (C) 2008 by Vinnie Falco <vinnie.falco@gmail.com>

  This library contains portions of other open source products covered by
  separate licenses. Please see the corresponding source files for specific
  terms.

  VFLib is provided under the terms of The MIT License (MIT):

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.
*/
/*============================================================================*/

#ifndef BEAST_SHAREDOBJECT_BEASTHEADER
#define BEAST_SHAREDOBJECT_BEASTHEADER

//==============================================================================
/**
    A reference counted object with overridable destroy behavior.

    This is a reference counted object compatible with SharedObjectPtr or
    ReferenceCountedObjectPtr. When the last reference is removed, an
    overridable virtual function is called to destroy the object. The default
    behavior simply calls operator delete. Overrides can perform more complex
    dispose actions, typically to destroy the object on a separate thread.

    @ingroup beast_concurrent
*/
class SharedObject : Uncopyable
{
public:
    /** Abstract SharedObject scope.

        The scope is invoked to destroy the object.
    */
    class Scope
    {
    public:
        virtual ~Scope () { }

        virtual void destroySharedObject (SharedObject* const object) = 0;
    };

public:
    /** Separate thread for a SharedObject scope.

        This Scope deletes the shared object on a separate provided thread.
    */
    class ThreadedScope
        : public Scope
        , private ThreadWithCallQueue::EntryPoints
    {
    public:
        /** Create a ThreadedScope.

            @param name The name of the provided thread, for diagnostics.
        */
        explicit ThreadedScope (char const* name);

        void destroySharedObject (SharedObject* const object);

        /** Delete a dynamic object asynchronously.

            This convenient template will delete a dynamically allocated
            object on the provided thread.
        */
        template <class Object>
        void deleteAsync (Object* const object)
        {
            // If an object being deleted recursively triggers async deletes,
            // it is possible that the call queue has already been closed.
            // We detect this condition by checking the associated thread and
            // doing the delete directly.
            //
            if (m_thread.isAssociatedWithCurrentThread ())
                delete object;
            else
                m_thread.callf (Delete <Object> (object));
        }

    private:
        // Simple functor to delete an object.
        //
        template <class Object>
        struct Delete
        {
            Delete (Object* const object) : m_object (object)
            {
            }

            void operator () ()
            {
                delete m_object;
            }

        private:
            Delete& operator= (Delete const&);

            Object* const m_object;
        };

    private:
        ThreadWithCallQueue m_thread;
    };

protected:
    /** Construct a SharedObject.

        The constructor is protected to require subclassing.
    */
    SharedObject () { }

    virtual ~SharedObject () { }

    /** Delete the object.

        The default behavior calls operator delete.
    */
    virtual void destroySharedObject ();

public:
    /** Increment the reference count.

        It should not be necessary to call this function directly. Use one of
        the RAII containers that manages the reference count to hold the
        object instead.
    */
    inline void incReferenceCount () noexcept
    {
        m_refs.addref ();
    }

    /** Decrement the reference count.

        It should not be necessary to call this function directly. Use one of
        the RAII containers that manages the reference count to hold the
        object instead.
    */
    inline void decReferenceCount () noexcept
    {
        if (m_refs.release ())
            destroySharedObject ();
    }

private:
    AtomicCounter m_refs;
};

//------------------------------------------------------------------------------

/** RAII container for SharedObject.

    This container is used to hold a pointer to a SharedObject and manage the
    reference counts for you.
*/
template <class Object>
class SharedObjectPtr
{
public:
    typedef Object ReferencedType;

    inline SharedObjectPtr () noexcept
:
    m_object (nullptr)
    {
    }

    inline SharedObjectPtr (Object* const refCountedObject) noexcept
:
    m_object (refCountedObject)
    {
        if (refCountedObject != nullptr)
            refCountedObject->incReferenceCount ();
    }

    inline SharedObjectPtr (const SharedObjectPtr& other) noexcept
:
    m_object (other.m_object)
    {
        if (m_object != nullptr)
            m_object->incReferenceCount ();
    }

#if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    inline SharedObjectPtr (SharedObjectPtr&& other) noexcept
:
    m_object (other.m_object)
    {
        other.m_object = nullptr;
    }
#endif

    template <class DerivedClass>
    inline SharedObjectPtr (const SharedObjectPtr <DerivedClass>& other) noexcept
:
    m_object (static_cast <Object*> (other.get ()))
    {
        if (m_object != nullptr)
            m_object->incReferenceCount ();
    }

    SharedObjectPtr& operator= (const SharedObjectPtr& other)
    {
        return operator= (other.m_object);
    }

    template <class DerivedClass>
    SharedObjectPtr& operator= (const SharedObjectPtr <DerivedClass>& other)
    {
        return operator= (static_cast <Object*> (other.get ()));
    }

#if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    SharedObjectPtr& operator= (SharedObjectPtr && other)
    {
        std::swap (m_object, other.m_object);
        return *this;
    }
#endif

    SharedObjectPtr& operator= (Object* const newObject)
    {
        if (m_object != newObject)
        {
            if (newObject != nullptr)
                newObject->incReferenceCount ();

            Object* const oldObject = m_object;
            m_object = newObject;

            if (oldObject != nullptr)
                oldObject->decReferenceCount ();
        }

        return *this;
    }

    inline ~SharedObjectPtr ()
    {
        if (m_object != nullptr)
            m_object->decReferenceCount ();
    }

    inline operator Object* () const noexcept
    {
        return m_object;
    }

    inline Object* operator-> () const noexcept
    {
        return m_object;
    }

    inline Object* get () const noexcept
    {
        return m_object;
    }

    inline Object* getObject () const noexcept
    {
        return m_object;
    }

private:
    Object* m_object;
};

template <class Object>
bool operator== (const SharedObjectPtr <Object>& object1, Object* const object2) noexcept
{
    return object1.get () == object2;
}

template <class Object>
bool operator== (const SharedObjectPtr <Object>& object1, const SharedObjectPtr <Object>& object2) noexcept
{
    return object1.get () == object2.get ();
}

template <class Object>
bool operator== (Object* object1, SharedObjectPtr <Object>& object2) noexcept
{
    return object1 == object2.get ();
}

template <class Object>
bool operator!= (const SharedObjectPtr <Object>& object1, const Object* object2) noexcept
{
    return object1.get () != object2;
}

template <class Object>
bool operator!= (const SharedObjectPtr <Object>& object1, SharedObjectPtr <Object>& object2) noexcept
{
    return object1.get () != object2.get ();
}

template <class Object>
bool operator!= (Object* object1, SharedObjectPtr <Object>& object2) noexcept
{
    return object1 != object2.get ();
}

#endif
