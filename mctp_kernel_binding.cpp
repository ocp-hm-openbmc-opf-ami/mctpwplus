#include "mctp_kernel_binding.h"
#include "mctp_kernel_utils.cpp"
#include <iostream>
void MCTPKernelBinding::setSd(int sock_d){
    this->sd = sock_d;
}

MCTPKernelBinding::MCTPKernelBinding(uint8_t type, int network, boost::asio::io_context& context):recv_timer(context), str(context) 

{
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
    str.assign(sd);
    read_looper();
    std::cout<<"Socket creation successful"<<std::endl;
};
//void MCTPKernelBinding::initializeStreamDescriptor(){
//
//    asyncReceiverFd.assign(sd);
//}
//void MCTPKernelBinding::startReceiveMessageAsync(ReceiveMessageCallback rxCb){
//    receiveCallback = rxCb;
//    receiveMessageAsync();
//}
//void MCTPKernelBinding::receiveMessageAsync(){
//    asyncReceiverFd.async_wait(boost::asio::posix::stream_descriptor::wait_error, [this](const boost::system::error_code& ec){
//            if(ec){
//            printf("Waiting...");
//            receiveMessageAsync();
//            }
//            char buf[4096];
//            int rc = receiveMessage(buf,4096);
//            if(rc>0){
//                std::vector<uint8_t> message;
//                for(int i=0;i<rc;i++){
//                message.push_back(buf[i]);
//                }
//                void *ptr = nullptr;
//                receiveCallback(ptr, recv_addr.smctp_addr.s_addr,(recv_addr.smctp_tag & (1<<(1-1))),(recv_addr.smctp_tag&0x07),message,1));
//            receiveMessageAsync();
//            }
//            });
//}
//void MCTPKernelBinding::register_reception(std::chrono::milliseconds timeout){
//    recv_timer.expires_after(timeout)
//}
int MCTPKernelBinding::yield_receive(std::vector<uint8_t> &response, uint8_t tag, std::chrono::milliseconds timeout){
    if(queue.find(tag)!=queue.end()){
        response = queue[tag];
        queue.erase(tag);
        return 1;
    }
    else{
        recv_timer.expires_after(timeout);
        recv_timer.wait();
        if(queue.find(tag)!=queue.end()){
            response = queue[tag];
            queue.erase(tag);
            return 1;
        }
        else{
            return 0;
        }
    return 0;
    }
}

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

void MCTPKernelBinding::read_looper(){
    
    printf("Readlooper to read\n");
    str.async_wait(boost::asio::posix::stream_descriptor::wait_read,[this](const boost::system::error_code& ec){
            printf("Ready to read\n");
            if(ec)
            {
                std::cout<<"Read error\n";
            }
            printf("Ready to read 2\n");
            char rxbuf[1048];
            int rc = receiveMessage(rxbuf,1048);
            if(rc<=0){
            err(EXIT_FAILURE, "Not received any, Trying again");
            read_looper();
            }
            printf("Received %d bytes:",rc);
            std::vector<uint8_t> data;
            for(int i=0;i<rc;i++){
            printf("0x%02x",rxbuf[i]);
            recv_timer.cancel();
            data.push_back(rxbuf[i]); 
            }
            queue[recv_addr.smctp_tag] = data;
            read_looper();
            }); 
}

void MCTPKernelBinding::setResponseTag(){
    recv_addr.smctp_tag = addr.smctp_tag & static_cast<unsigned char>(~MCTP_TAG_OWNER);
}
int MCTPKernelBinding:: createSocket(){
    sd = socket(AF_MCTP, SOCK_DGRAM, 0);
    if(sd < 0){
        err(EXIT_FAILURE, "Failed to open socket");
    }
    //type and addr information should come from user(pldmd, nvme, etc)
    struct sockaddr_mctp addr_;
    memset(&addr_, 0, sizeof(addr_));
    addr_.smctp_family = AF_MCTP;
    addr_.smctp_addr.s_addr = 0x09;
    addr_.smctp_type = 1; 
    int rc = bind(sd, reinterpret_cast<sockaddr*>(&addr_),
                  sizeof(addr_));
    if(rc){
        err(EXIT_FAILURE, "Failed to bind\n");
    }
    return sd; 
}

int MCTPKernelBinding:: createNonBlockSocket(){
    sd = socket(AF_MCTP, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    return sd; 
}

void MCTPKernelBinding::setEid(mctp_eid_t eid){
    MCTPKernelBinding::addr.smctp_addr.s_addr = eid;
}

int MCTPKernelBinding::sendMessage(mctp_eid_t destination_eid,const ByteArray& message){
    size_t message_size = message.size();
    setEid(destination_eid);
    auto p = message.data();
    p++;
    message_size = message_size-1;
    int rc = sendto(sd, p, message_size, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(struct sockaddr_mctp)); 
    return rc;
}

int MCTPKernelBinding::receiveMessage(char rxbuf[], int recv_len){
    socklen_t recv_addr_len = sizeof(recv_addr);
    int rc = recvfrom(sd, rxbuf, recv_len , 0 , reinterpret_cast<struct sockaddr*>(&recv_addr), &recv_addr_len);
    return rc; 
}



