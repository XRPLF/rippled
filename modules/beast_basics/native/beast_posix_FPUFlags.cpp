// Copyright (C) 2008 by Vinnie Falco, this file is part of VFLib.
// See the file LICENSE.txt for licensing information.

#pragma message(BEAST_LOC_"Missing platform-specific implementation")

FPUFlags FPUFlags::getCurrent ()
{
    return FPUFlags ();
}

void FPUFlags::setCurrent (const FPUFlags& flags)
{
}
