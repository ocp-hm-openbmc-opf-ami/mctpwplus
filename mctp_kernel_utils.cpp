#define MCTP_TAG_MASK
#include "mctp.h"
#include "mctp_kernel_utils.hpp"

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
