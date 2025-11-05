#ifndef XRPL_APP_BASICAPP_H_INCLUDED
#define XRPL_APP_BASICAPP_H_INCLUDED

#include <boost/asio/io_context.hpp>

#include <optional>
#include <thread>
#include <vector>

// This is so that the io_context can outlive all the children
class BasicApp
{
private:
    std::optional<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>>
        work_;
    std::vector<std::thread> threads_;
    boost::asio::io_context io_context_;

public:
    BasicApp(std::size_t numberOfThreads);
    ~BasicApp();

    boost::asio::io_context&
    get_io_context()
    {
        return io_context_;
    }
};

#endif
