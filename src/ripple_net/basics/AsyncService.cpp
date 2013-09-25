//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

AsyncService::AsyncService (char const* name, Stoppable& parent)
    : Stoppable (name, parent)
{
}

AsyncService::~AsyncService ()
{
    // If this goes off it means the
    // AsyncService API contract was violated.
    //
    bassert (m_pendingIo.get() == 0);
}

void AsyncService::serviceCountIoPending ()
{
    ++m_pendingIo;
}

bool AsyncService::serviceCountIoComplete (boost::system::error_code const& ec)
{
    // If this goes off, the count is unbalanced.
    bassert (m_pendingIo.get() > 0);

    --m_pendingIo;

    if (! ec || ec == boost::asio::error::operation_aborted)
        return true;

    return false;
}

void AsyncService::onServiceIoComplete ()
{
    //stopped();
}
