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

#ifndef BEAST_RESULT_H_INCLUDED
#define BEAST_RESULT_H_INCLUDED

namespace beast
{

/** Represents the 'success' or 'failure' of an operation, and holds an associated
    error message to describe the error when there's a failure.

    E.g.
    @code
    Result myOperation()
    {
        if (doSomeKindOfFoobar())
            return Result::ok();
        else
            return Result::fail ("foobar didn't work!");
    }

    const Result result (myOperation());

    if (result.wasOk())
    {
        ...it's all good...
    }
    else
    {
        warnUserAboutFailure ("The foobar operation failed! Error message was: "
                                + result.getErrorMessage());
    }
    @endcode
*/
class  Result
{
public:
    //==============================================================================
    /** Creates and returns a 'successful' result. */
    static Result ok() noexcept { return Result(); }

    /** Creates a 'failure' result.
        If you pass a blank error message in here, a default "Unknown Error" message
        will be used instead.
    */
    static Result fail (const String& errorMessage) noexcept;

    //==============================================================================
    /** Returns true if this result indicates a success. */
    bool wasOk() const noexcept;

    /** Returns true if this result indicates a failure.
        You can use getErrorMessage() to retrieve the error message associated
        with the failure.
    */
    bool failed() const noexcept;

    /** Returns true if this result indicates a success.
        This is equivalent to calling wasOk().
    */
    operator bool() const noexcept;

    /** Returns true if this result indicates a failure.
        This is equivalent to calling failed().
    */
    bool operator!() const noexcept;

    /** Returns the error message that was set when this result was created.
        For a successful result, this will be an empty string;
    */
    const String& getErrorMessage() const noexcept;

    //==============================================================================
    Result (const Result&);
    Result& operator= (const Result&);

   #if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    Result (Result&&) noexcept;
    Result& operator= (Result&&) noexcept;
   #endif

    bool operator== (const Result& other) const noexcept;
    bool operator!= (const Result& other) const noexcept;

private:
    String errorMessage;

    // The default constructor is not for public use!
    // Instead, use Result::ok() or Result::fail()
    Result() noexcept;
    explicit Result (const String&) noexcept;

    // These casts are private to prevent people trying to use the Result object in numeric contexts
    operator int() const;
    operator void*() const;
};

} // beast

#endif

