#include "mctp_kernel_binding.hpp"

AddressConstructor::AddressConstructor(uint8_t msgType, int net)
    :
        messageType(msgType), network(net)
{

};
struct sockaddr_mctp AddressConstructor::constructAddress()
{
    struct sockaddr_mctp address{};
    //memset(&address, 0, sizeof(address));
    address.smctp_addr.s_addr = MCTP_ADDR_ANY;
    address.smctp_type = messageType;
    address.smctp_family = AF_MCTP;
    
    return address;
};

struct sockaddr_mctp AddressConstructor::constructAddress(mctp_eid_t destinationEid)
{
    struct sockaddr_mctp address{};
    //memset(&address, 0, sizeof(address));
    address.smctp_addr.s_addr = destinationEid;
    address.smctp_type = messageType;
    address.smctp_family = AF_MCTP;
    address.smctp_tag = MCTP_TAG_OWNER;
    address.smctp_network = network;

    return address;
};

struct sockaddr_mctp AddressConstructor::constructAddress(mctp_eid_t destinationEid, uint8_t tag)
{
    struct sockaddr_mctp address{};
    //memset(&address, 0, sizeof(address));
    address.smctp_addr.s_addr = destinationEid;
    address.smctp_type = messageType;
    address.smctp_family = AF_MCTP;
    address.smctp_tag = tag;
    address.smctp_network = network;

    return address;
};
void MCTPKernelBinding::initializeMctpConnection()
{
    int socketDescriptor = createSocket();
    if(socketDescriptor < 0)
    {
        err(EXIT_FAILURE, "Failed to create socket");
    }

    if(bindSocket(socketDescriptor) != 0)
    {
        err(EXIT_FAILURE, "Failed to bind");    
    }

    socketStream.assign(socketDescriptor);
}

int MCTPKernelBinding::createSocket()
{
    int socketDescriptor = socket(AF_MCTP, SOCK_DGRAM | SOCK_NONBLOCK, 0);

    return socketDescriptor;
}

int MCTPKernelBinding::bindSocket(int socketDescriptor)
{
    struct sockaddr_mctp bindAddress = addressConstructor.constructAddress();
    int rc = bind(socketDescriptor, reinterpret_cast<struct sockaddr*>(&bindAddress),sizeof(bindAddress));
    
    return rc;
}

bool MCTPKernelBinding::sendMessage(mctp_eid_t destinationEid, const ByteArray& message)
{
    struct sockaddr_mctp sendAddress = addressConstructor.constructAddress(destinationEid);
   
    return (removeTypeAndSendMessage(sendAddress, message)); 
}

bool MCTPKernelBinding::sendMessage(mctp_eid_t destinationEid,
        const ByteArray& message, uint8_t messageTag, bool tagOwner)
{
    uint8_t tag = encodeTagMessage(messageTag, tagOwner);
     struct sockaddr_mctp sendAddress = addressConstructor.constructAddress(destinationEid,tag);
   
    return (removeTypeAndSendMessage(sendAddress, message)); 
}

bool MCTPKernelBinding::removeTypeAndSendMessage(
        struct sockaddr_mctp& sendAddress,
        const ByteArray& message)
{
    int rc = sendto(socketStream.native_handle(), message.data() + 1,
            message.size() - 1, 0,
            reinterpret_cast<struct sockaddr*>(&sendAddress),
            sizeof(sendAddress)); 
    
    if(rc == static_cast<int>(message.size()-1))
    {
        return true;
    }
    
    return false;
}

int MCTPKernelBinding::yieldReceive(boost::asio::yield_context yield,
        ByteArray &response, uint8_t tag, 
        std::chrono::milliseconds timeout)
{
    if(!findMatchingResponse(tag, response))
    {
        receiveTimer.expires_after(timeout);
        boost::system::error_code ec;
        receiveTimer.async_wait(yield[ec]);
        
        if(!findMatchingResponse(tag,response))
        {
            return 0;
        }
    }
    
    return 1;
}

void MCTPKernelBinding::getReceivedMessages()
{
    socketStream.async_wait(boost::asio::posix::stream_descriptor::wait_read,
            [this]
            (const boost::system::error_code& ec){
                if(ec)
                {
                    err(EXIT_FAILURE, "Error reading socket stream");
                }

                auto message = receiveMessage();
                if(message.bytes <= 0)
                {
                    getReceivedMessages();
                }

                receiveTimer.cancel();
                message.response.resize(message.bytes);
                insertMessageType(message.response);

                if(decodeTagOwner(message.address.smctp_tag) == 0)
                {
                    tagResponseMap[message.address.smctp_tag] = message.response;
                }
                else
                {
                    invokeCallback(message);
                }

                getReceivedMessages();
            }); 
}

ReceivedMessage MCTPKernelBinding::receiveMessage()
{
    ReceivedMessage receivedMessage;
    receivedMessage.response.resize(1048);
    receivedMessage.address = addressConstructor.constructAddress(0x09);
    socklen_t receiveAddressLength = sizeof(receivedMessage.address);

    receivedMessage.bytes = recvfrom(socketStream.native_handle(), receivedMessage.response.data(),
            receivedMessage.response.capacity(), 0,
            reinterpret_cast<struct sockaddr*>(&receivedMessage.address),
            &(receiveAddressLength));
    
    return receivedMessage; 
}

void MCTPKernelBinding::insertMessageType(ByteArray& message)
{
    message.insert(message.begin(),addressConstructor.messageType);
}

void MCTPKernelBinding::invokeCallback(ReceivedMessage& message)
{
    boost::asio::post([this, message](){
            void* p = nullptr;
            receiveCallback(p,message.address.smctp_addr.s_addr,
                    decodeTagOwner(message.address.smctp_tag),
                    decodeTagValue(message.address.smctp_tag),
                    message.response, 1);
            });
}

uint8_t MCTPKernelBinding::decodeTagOwner(uint8_t tag)
{    
    return ((tag&0x08)>>3);
}

uint8_t MCTPKernelBinding::decodeTagValue(uint8_t tag)
{
    return (tag & 0x07);

}

uint8_t MCTPKernelBinding::encodeTagMessage(uint8_t tagValue, bool tagOwner)
{
    if(tagOwner){
        tagValue |= 0x08;
    }
    
    return tagValue;

}

bool MCTPKernelBinding::findMatchingResponse(uint8_t tag, ByteArray& response)
{
    if(tagResponseMap.find(tag)!=tagResponseMap.end())
    {
        response = tagResponseMap[tag];
        tagResponseMap.erase(tag);
        
        return true;
    }
    
    return false;
}

MCTPKernelBinding::MCTPKernelBinding(uint8_t messageType, int network,
        boost::asio::io_context& context,
        ReceiveMessageCallback rxCb)
    :
        socketStream(context),
        receiveTimer(context), 
        receiveCallback(rxCb),
        addressConstructor(messageType, network)
{
    initializeMctpConnection();
    getReceivedMessages();
};

