#define PLDM_MESSAGE 0x01
#include "mctp_kernel_utils.hpp"
#include <sys/socket.h>
#include "mctp.h"
#include <stdlib.h>
#include <vector>
#include <err.h>
#include <string.h>
#include <functional>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/spawn.hpp>

using ByteArray = std::vector<uint8_t>;

using ReceiveMessageCallback = std::function<void(void*, mctp_eid_t, bool, uint8_t, const ByteArray &, int)>;


class MCTPKernelBinding
{
    public:

        MCTPKernelBinding(uint8_t type, int network,
                boost::asio::io_context& io_context,
                ReceiveMessageCallback rxCb);

        bool sendMessage(mctp_eid_t destinationEid,
                const ByteArray& message);
        bool sendMessage(mctp_eid_t destinationEid, 
                const ByteArray& message, uint8_t messageTag, 
                bool tagOwner);
        int yieldReceive(boost::asio::yield_context yield, ByteArray &response,
                uint8_t tag, std::chrono::milliseconds timeout);

    private:

        boost::asio::posix::stream_descriptor socketStream;
        std::unordered_map<uint8_t,ByteArray> tagResponseMap;
        boost::asio::steady_timer receiveTimer;
        ReceiveMessageCallback receiveCallback;
        AddressConstructor addressConstructor;

        void initializeMctpConnection();
        int createSocket();
        int bindSocket(int socketDescriptor);
        void insertMessageType(ByteArray& message);
        bool removeTypeAndSendMessage(struct sockaddr_mctp sendAddress,const ByteArray& message);
        void getReceivedMessages();
        ReceivedMessage receiveMessage();
        bool findMatchingResponse(uint8_t tag, ByteArray& response);
        void invokeCallback(ReceivedMessage message);
};


