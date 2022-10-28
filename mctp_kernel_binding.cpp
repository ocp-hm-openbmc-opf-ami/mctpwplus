#include "mctp_kernel_binding.h"
#include "mctp_kernel_utils.cpp"
#include <err.h>
#include <errno.h>
#include <string.h>
#include <vector>
using ByteArray = std::vector<uint8_t>;
MCTPKernelBinding::MCTPKernelBinding(uint8_t type, int network){
    addr.smctp_family = AF_MCTP;
    addr.smctp_tag = MCTP_TAG_OWNER;
    addr.smctp_type = type;
    addr.smctp_network = network;

    recv_addr.smctp_family = AF_MCTP;
    recv_addr.smctp_addr.s_addr = MCTP_ADDR_ANY;
    recv_addr.smctp_type = type;
};

int MCTPKernelBinding::sendReceiveMessage(mctp_eid_t destination_eid, ByteArray request, char **response){
    int rc;
    rc = createSocket();
    if(rc < 0){
        err(EXIT_FAILURE, "Error creating socket: %s\n", strerror(errno));
    }
    rc = sendMessage(destination_eid, request);
    if(rc < static_cast<int>(request.size()-1)){
        err(EXIT_FAILURE, "Error sending message: %s\n", strerror(errno));
    }
    rc = receiveMessage(response);
    if(rc < 0){ 
        err(EXIT_FAILURE, "Error receiving message: %s\n", strerror(errno));
    }
    return rc;
}
int MCTPKernelBinding:: createSocket(){
    sd = socket(AF_MCTP, SOCK_DGRAM, 0);
    return sd; 
}

int MCTPKernelBinding:: createNonBlockSocket(){
    sd = socket(AF_MCTP, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    return sd; 
}

void MCTPKernelBinding::setEid(mctp_eid_t eid){
    MCTPKernelBinding::addr.smctp_addr.s_addr = eid;
}

int MCTPKernelBinding::sendMessage(mctp_eid_t destination_eid, ByteArray& message){

    size_t message_size = message.size();
    if(message_size < 1 || message_size>64){
        err(EXIT_FAILURE,"Invalid messsage length: %zu",message_size);
    }
    setEid(destination_eid);
    //const void* message_data_p = removePldmHeader(message, message_size);
    int rc = sendto(sd, message.data(), message_size, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(struct sockaddr_mctp)); 
    return rc;
}

int MCTPKernelBinding::receiveMessage(char **rxbuf){
    auto recv_len = sizeof(rxbuf);
    socklen_t recv_addr_len = sizeof(recv_addr);
    int rc = recvfrom(sd, rxbuf, recv_len , 0 , reinterpret_cast<struct sockaddr*>(&recv_addr), &recv_addr_len);
    return rc;
}



