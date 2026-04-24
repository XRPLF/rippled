#include <xrpld/app/main/BasicApp.h>

#include <xrpl/beast/core/CurrentThreadName.h>

#include <boost/asio/executor_work_guard.hpp>

#include <cstddef>
#include <string>

BasicApp::BasicApp(std::size_t numberOfThreads) : numberOfThreads_(numberOfThreads)
{
    work_.emplace(boost::asio::make_work_guard(io_context_));
    startIOThreads();
}

BasicApp::BasicApp(std::size_t numberOfThreads, DeferStart) : numberOfThreads_(numberOfThreads)
{
    work_.emplace(boost::asio::make_work_guard(io_context_));
}

void
BasicApp::startIOThreads()
{
    threads_.reserve(numberOfThreads_);

    for (std::size_t i = 0; i < numberOfThreads_; ++i)
    {
        threads_.emplace_back([this, n = i]() {
            beast::setCurrentThreadName("io svc #" + std::to_string(n));
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
