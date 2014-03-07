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

#ifndef BEAST_DYNAMICOBJECT_H_INCLUDED
#define BEAST_DYNAMICOBJECT_H_INCLUDED

namespace beast
{

//==============================================================================
/**
    Represents a dynamically implemented object.

    This class is primarily intended for wrapping scripting language objects,
    but could be used for other purposes.

    An instance of a DynamicObject can be used to store named properties, and
    by subclassing hasMethod() and invokeMethod(), you can give your object
    methods.
*/
class BEAST_API DynamicObject
    : public SharedObject
    , LeakChecked <DynamicObject>
{
public:
    //==============================================================================
    DynamicObject();

    /** Destructor. */
    virtual ~DynamicObject();

    typedef SharedPtr<DynamicObject> Ptr;

    //==============================================================================
    /** Returns true if the object has a property with this name.
        Note that if the property is actually a method, this will return false.
    */
    virtual bool hasProperty (const Identifier& propertyName) const;

    /** Returns a named property.
        This returns a void if no such property exists.
    */
    virtual var getProperty (const Identifier& propertyName) const;

    /** Sets a named property. */
    virtual void setProperty (const Identifier& propertyName, const var& newValue);

    /** Removes a named property. */
    virtual void removeProperty (const Identifier& propertyName);

    //==============================================================================
    /** Checks whether this object has the specified method.

        The default implementation of this just checks whether there's a property
        with this name that's actually a method, but this can be overridden for
        building objects with dynamic invocation.
    */
    virtual bool hasMethod (const Identifier& methodName) const;

    /** Invokes a named method on this object.

        The default implementation looks up the named property, and if it's a method
        call, then it invokes it.

        This method is virtual to allow more dynamic invocation to used for objects
        where the methods may not already be set as properies.
    */
    virtual var invokeMethod (const Identifier& methodName,
                              const var* parameters,
                              int numParameters);

    /** Sets up a method.

        This is basically the same as calling setProperty (methodName, (var::MethodFunction) myFunction), but
        helps to avoid accidentally invoking the wrong type of var constructor. It also makes
        the code easier to read,

        The compiler will probably force you to use an explicit cast your method to a (var::MethodFunction), e.g.
        @code
        setMethod ("doSomething", (var::MethodFunction) &MyClass::doSomething);
        @endcode
    */
    void setMethod (const Identifier& methodName,
                    var::MethodFunction methodFunction);

    //==============================================================================
    /** Removes all properties and methods from the object. */
    void clear();

    /** Returns the NamedValueSet that holds the object's properties. */
    NamedValueSet& getProperties() noexcept     { return properties; }

private:
    //==============================================================================
    NamedValueSet properties;
};

}  // namespace beast

#endif   // BEAST_DYNAMICOBJECT_H_INCLUDED
