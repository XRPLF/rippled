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

#ifndef VF_MANUALCALLQUEUE_VFHEADER
#define VF_MANUALCALLQUEUE_VFHEADER

/*============================================================================*/
/**
    A CallQueue that requires periodic manual synchronization.

    To use this, declare an instance and then place calls into it as usual.
    Every so often, you must call synchronize() from the thread you want to
    associate with the queue. Typically this is done within an
    AudioIODeviceCallback:

    @code

    class AudioIODeviceCallbackWithCallQueue
      : public AudioIODeviceCallback
      , public CallQueue
    {
    public:
      AudioIODeviceCallbackWithCallQueue () : m_fifo ("Audio CallQueue")
      {
      }

      void audioDeviceIOCallback (const float** inputChannelData,
                                  int numInputChannels,
                                  float** outputChannelData,
                                  int numOutputChannels,
                                  int numSamples)
      {
        CallQueue::synchronize ();

        // do audio i/o
      }

      void signal () { } // No action required
      void reset () { }  // No action required
    };

    @endcode

    The close() function is provided for diagnostics. Call it as early as
    possible based on the exit or shutdown logic of your application. If calls
    are put into the queue after it is closed, it will generate an exception so
    you can track it down.

    @see CallQueue

    @ingroup vf_concurrent
*/
class ManualCallQueue : public CallQueue
{
public:
    /** Create a ManualCallQueue.

      @param name           A string used to help identify the associated
                              thread for debugging.
    */
    explicit ManualCallQueue (String name);

    /** Close the queue. If calls are placed into a closed queue, an exception
      is thrown.
    */
    void close ();

    /** Synchronize the queue by calling all pending functors.

        @return `true` if any functors were called.
    */
    bool synchronize ();

private:
    void signal ();
    void reset ();
};

#endif
