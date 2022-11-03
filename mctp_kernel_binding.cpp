#include "mctp_kernel_binding.h"
#include "mctp_kernel_utils.cpp"
#include <iostream>
void MCTPKernelBinding::setSd(int sock_d){
    this->sd = sock_d;
}

MCTPKernelBinding::MCTPKernelBinding(uint8_t type, int network){
    addr.smctp_family = AF_MCTP;
    addr.smctp_tag = MCTP_TAG_OWNER;
    addr.smctp_type = type;
    addr.smctp_network = network;

    recv_addr.smctp_family = AF_MCTP;
    recv_addr.smctp_addr.s_addr = MCTP_ADDR_ANY;
    recv_addr.smctp_type = type;


    int rc;
    rc = createSocket();
    if(rc < 0){
        err(EXIT_FAILURE, "Error creating socket: %s\n", strerror(errno));
    }
    setSd(rc);
    std::cout<<"Socket creation successful"<<std::endl;
};

int MCTPKernelBinding::sendReceiveMessage(mctp_eid_t destination_eid, ByteArray request, char response[], int response_size){
    int rc = sendMessage(destination_eid, request);
    if(rc < static_cast<int>(request.size()-1)){
        err(EXIT_FAILURE, "Error sending message: %s\n", strerror(errno));
    }
    std::cout<<"Send successful"<<std::endl;
    //setResponseTag();
    rc = receiveMessage(response, response_size);
    if(rc < 0){ 
        err(EXIT_FAILURE, "Error receiving message: %s\n", strerror(errno));
    }
    std::cout<<"Receive successful"<<std::endl;
     
    return rc;

}

void MCTPKernelBinding::setResponseTag(){
    recv_addr.smctp_tag = addr.smctp_tag & static_cast<unsigned char>(~MCTP_TAG_OWNER);
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
    auto p = message.data();
    p++;
    message_size = message_size-1;
    //const void* message_data_p = removePldmHeader(message, message_size);
    int rc = sendto(sd, p, message_size, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(struct sockaddr_mctp)); 
    return rc;
}

int MCTPKernelBinding::receiveMessage(char rxbuf[], int recv_len){
    socklen_t recv_addr_len = sizeof(recv_addr);
    int rc = recvfrom(sd, rxbuf, recv_len , 0 , reinterpret_cast<struct sockaddr*>(&recv_addr), &recv_addr_len);
    return rc; 
}



