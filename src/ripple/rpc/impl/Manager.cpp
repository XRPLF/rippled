//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple {
namespace RPC {

Manager* Manager::New (Journal journal)
{
    return new ManagerImpl (journal);
}

}
}
