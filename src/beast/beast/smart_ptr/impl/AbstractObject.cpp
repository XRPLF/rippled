//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include "../AbstractObject.h"

namespace beast {
namespace abstract {

class AbstractObjectTests : public UnitTest
{
public:
    class Object : public Interfaces <Object>
    {
    public:
        explicit Object (UnitTest& test)
            : m_test (test)
        {
        }

        UnitTest& test ()
        {
            return m_test;
        }

    private:
        UnitTest& m_test;
    };

    //--------------------------------------------------------------------------

    class Interface1 : public Interface <Interface1>
    {
    public:
        Interface1 ()
        {
        }
    };

    class Callback1 : public Factory <Object>::Callback
    {
    public:
        explicit Callback1 (Factory <Object>& factory)
            : Factory <Object>::Callback (factory)
        {
        }

        void create_interfaces (Object& object)
        {
            object.add_interface (new Interface1);
        }
    };

    //--------------------------------------------------------------------------

    class Interface2 : public Interface <Interface2>
    {
    public:
        Interface2 ()
        {
        }
    };

    class Callback2 : public Factory <Object>::Callback
    {
    public:
        explicit Callback2 (Factory <Object>& factory)
            : Factory <Object>::Callback (factory)
        {
        }

        void create_interfaces (Object& object)
        {
            object.add_interface (new Interface2);
        }
    };

    //--------------------------------------------------------------------------

    void runTest ()
    {
        beginTestCase ("create");

        Factory <Object> factory;
        Callback1 callback1 (factory);
        Callback2 callback2 (factory);

        Object object (*this);
        factory.create_interfaces (object);

        // find existing interfaces
        expect (object.find_interface <Interface1> () != nullptr);
        expect (object.find_interface <Interface2> () != nullptr);
         
        // add duplicate interface
        try
        {
            object.add_interface (new Interface1);
            fail ("uncaught exeption");
        }
        catch (std::invalid_argument const&)
        {
            pass ();
        }

        // request missing interface
        try
        {
            struct MissingInterface { };
            object.get_interface <MissingInterface> ();
            fail ("uncaught exeption");
        }
        catch (std::bad_cast const&)
        {
            pass ();
        }
    }

    AbstractObjectTests () : UnitTest (
        "AbstractObject", "beast", runManual)
    {
    }
};

static AbstractObjectTests abstractObjectTests;

}
}
