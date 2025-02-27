#include "protocol.hpp"
#include "socket_if.hpp"

#include <gtest/gtest.h>

using ByteArray = std::vector<uint8_t>;

boost::asio::io_context ioRes;
boost::asio::io_context ioReq;

int socketNumber = 1;

void requester()
{
    uint8_t eid = 9;
    const uint8_t msgTag = 0;
    const bool tagOwner = false;
    ByteArray req1 = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    ByteArray req2 = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
    ByteArray req3 = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};

    auto sktInt = std::make_shared<mctpw::SocketInterface>(
        std::to_string(socketNumber), ioReq);
    sktInt->setMessageReceivedCallback(
        [&sktInt, &req1, &req2, &req3](uint8_t, bool, uint8_t,
                                       const ByteArray& payload) {
            if (payload != req1 && payload != req2 && payload != req3)
            {
                FAIL();
            }
        });

    boost::asio::spawn(
        ioReq,
        [&sktInt, &eid, &req1](boost::asio::yield_context yield) {
            std::pair<std::error_code, ByteArray> res =
                sktInt->sendReceiveYield(yield, eid, req1,
                                         std::chrono::milliseconds(100));

            if (std::get<std::error_code>(res))
            {
                FAIL();
            }

            if (std::get<ByteArray>(res) != req1)
            {
                FAIL();
            }
        },
        {});

    sktInt->sendReceiveAsync(
        [&req2](boost::system::error_code ec, const std::vector<uint8_t>&) {
            if (ec)
            {
                FAIL();
            }
        },
        eid, req2, std::chrono::milliseconds(100));

    sktInt->sendAsync(
        [](boost::system::error_code ec, const int) {
            if (ec)
            {
                FAIL();
            }
        },
        eid, msgTag, tagOwner, req3);

    ioReq.run();
}

void waitForRequest(
    std::unique_ptr<boost::asio::local::stream_protocol::socket>& resSocket,
    boost::asio::streambuf& resBuffer)
{
    static int cnt = 0;
    if (cnt == 3)
    {
        return;
    }
    cnt++;

    boost::asio::async_read_until(
        *resSocket, resBuffer, isCompleteRequest,
        [&resSocket, &resBuffer](boost::system::error_code ec,
                                 std::size_t length) {
            if (ec || length == 0)
            {
                if (ec == boost::asio::error::eof)
                {
                    ioRes.stop();
                }
                else
                {
                    waitForRequest(resSocket, resBuffer);
                }
                return;
            }

            std::vector<uint8_t> reqBuf(length);
            boost::asio::buffer_copy(boost::asio::buffer(reqBuf),
                                     resBuffer.data(), length);
            resBuffer.consume(length);

            boost::asio::spawn(
                ioRes,
                [reqBuf = std::move(reqBuf),
                 &resSocket](boost::asio::yield_context) {
                    boost::asio::write(
                        *resSocket,
                        boost::asio::buffer(reqBuf.data(), reqBuf.size()));
                },
                {});

            waitForRequest(resSocket, resBuffer);
        });
}

void echoResponder()
{
    constexpr char unixSktAbsPath[] = "\0mctp";
    constexpr size_t unixSktAbsPathLen = sizeof(unixSktAbsPath) - 1;

    std::string path(unixSktAbsPath, unixSktAbsPathLen);

    path += std::to_string(socketNumber);

    boost::asio::local::stream_protocol::acceptor acceptor(ioRes, path);
    std::unique_ptr<boost::asio::local::stream_protocol::socket> resSocket;
    boost::asio::local::basic_endpoint<boost::asio::local::stream_protocol>
        resSocketEp(path);

    boost::asio::streambuf resBuffer;

    acceptor.async_accept(
        resSocketEp, [&resSocket, &resBuffer](
                         boost::system::error_code ec,
                         boost::asio::local::stream_protocol::socket socket) {
            if (ec)
            {
                FAIL();
            }

            resSocket =
                std::make_unique<boost::asio::local::stream_protocol::socket>(
                    std::move(socket));
            waitForRequest(resSocket, resBuffer);
        });

    ioRes.run();
}

TEST(SocketIfTest, SocketIf)
{
    pid_t pid = fork();

    if (pid == -1)
    {
        FAIL();
    }
    else if (pid > 0)
    {
        echoResponder();
    }
    else
    {
        requester();
    }
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
