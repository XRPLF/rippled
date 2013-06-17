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

class PerformedAtExit::Performer
{
public:
    typedef Static::Storage <LockFreeStack <PerformedAtExit>, PerformedAtExit> StackType;

private:
    ~Performer ()
    {
        PerformedAtExit* object = s_list->pop_front ();

        while (object != nullptr)
        {
            object->performAtExit ();

            object = s_list->pop_front ();
        }

        LeakCheckedBase::detectAllLeaks ();
    }

public:
    static void push_front (PerformedAtExit* object)
    {
        s_list->push_front (object);
    }

private:
    friend class PerformedAtExit;

    static StackType s_list;

    static Performer s_performer;
};

PerformedAtExit::Performer PerformedAtExit::Performer::s_performer;
PerformedAtExit::Performer::StackType PerformedAtExit::Performer::s_list;

PerformedAtExit::PerformedAtExit ()
{
#if BEAST_IOS
    // TODO: PerformedAtExit::Performer::push_front crashes on iOS if s_storage is not accessed before used
    char* hack = PerformedAtExit::Performer::s_list.s_storage;
#endif

    Performer::push_front (this);
}

