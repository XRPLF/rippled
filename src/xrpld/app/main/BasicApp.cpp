#include <xrpld/app/main/BasicApp.h>

#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/basics/TraceLog.h>

#include <boost/asio/executor_work_guard.hpp>

#include <cstddef>
#include <string>

BasicApp::BasicApp(std::size_t numberOfThreads)
{
    TRACE_FUNC();
    work_.emplace(boost::asio::make_work_guard(io_context_));
    threads_.reserve(numberOfThreads);

    for (std::size_t i = 0; i < numberOfThreads; ++i)
    {
        threads_.emplace_back([this, i]() {
            beast::setCurrentThreadName("io svc #" + std::to_string(i));
            this->io_context_.run();
        });
    }
}

BasicApp::~BasicApp()
{
    TRACE_FUNC();
    work_.reset();

    for (auto& t : threads_)
        t.join();
}
