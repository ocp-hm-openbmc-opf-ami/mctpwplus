#define PLDM_MESSAGE 0x01 
#include <sys/socket.h>
#include "mctp.h"
#include <stdlib.h>
#include <vector>
#include <err.h>
#include <string.h>
#include <functional>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>

using ByteArray = std::vector<uint8_t>;

using ReceiveMessageCallback = std::function<void(void*, mctp_eid_t, bool, uint8_t, const ByteArray &, int)>;

struct ReceivedMessage{
    struct sockaddr_mctp address;
    int bytes;
    ByteArray response;
};

class AddressConstructor{
    public:
        uint8_t messageType;
        int network;
        AddressConstructor(uint8_t messageType, int network);
        struct sockaddr_mctp constructAddress();
        struct sockaddr_mctp constructAddress(mctp_eid_t destinationEid);
        struct sockaddr_mctp constructAddress(mctp_eid_t destinationEid, uint8_t tag);
};
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
        boost::asio::steady_timer receiveTimer;
        ReceiveMessageCallback receiveCallback;
        AddressConstructor addressConstructor;

        void invokeCallback(ReceivedMessage& message);
        void initializeMctpConnection();
        int bindSocket(int socketDescriptor);
        std::unordered_map<uint8_t,ByteArray> tagResponseMap;
        void getReceivedMessages();
        bool removeTypeAndSendMessage(struct sockaddr_mctp& sendAddress,const ByteArray& message);
        bool findMatchingResponse(uint8_t tag, ByteArray& response);
        int createSocket();
        ReceivedMessage receiveMessage();
        void insertMessageType(ByteArray& message);
        uint8_t decodeTagOwner(uint8_t tag);
        uint8_t decodeTagValue(uint8_t tag);
        uint8_t encodeTagMessage(uint8_t tagValue, bool tagOwner);
};


