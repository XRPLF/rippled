//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_CORE_MAIN_H_INCLUDED
#define BEAST_CORE_MAIN_H_INCLUDED

namespace beast
{

/** Represents a command line program's entry point
    To use this, derive your class from @ref Main and implement the
    function run ();
*/
class Main : public Uncopyable
{
public:
    Main ();

    virtual ~Main ();

    /** Run the program.

        You should call this from your @ref main function. Don't put
        anything else in there. Instead, do it in your class derived
        from Main. For example:

        @code

        struct MyProgram : Main
        {
            int run (int argc, char const* const* argv)
            {
                std::cout << "Hello, world!" << std::endl;
                return 0;
            }
        };

        int main (int argc, char const* const* argv)
        {
            MyProgram program;

            return program.runFromMain (argc, argv);
        }

        @endcode
    */
    int runFromMain (int argc, char const* const* argv);

    /** Retrieve the instance of the program. */
    static Main& getInstance ();

protected:
    /** Entry point for running the program.
        Subclasses provide the implementation.
    */
    virtual int run (int argc, char const* const* argv) = 0;

private:
    int runStartupUnitTests ();

private:
    static Main* s_instance;
};

}  // namespace beast

#endif

