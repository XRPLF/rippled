#include <xrpld/app/main/BasicApp.h>

#include <xrpl/beast/core/CurrentThreadName.h>

#include <boost/asio/executor_work_guard.hpp>

BasicApp::BasicApp(std::size_t numberOfThreads)
{
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
    work_.reset();

    for (auto& t : threads_)
        t.join();
}
