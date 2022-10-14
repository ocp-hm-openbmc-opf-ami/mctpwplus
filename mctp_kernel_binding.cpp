//#define PLDM_MESSAGE 1
//#define MAX_MCTP_PAYLOAD 64
//#include <sys/socket.h>
//#include "mctp.h"
//#include "mctp_impl.hpp"
//using ByteArray = std::vector<uint8_t>;
//
//class MCTPKernelBinding
//{
//    struct sockaddr_mctp addr;
//    struct sockaddr_mctp recv_addr;
//    int sd;
//    int rc;
//    char rxbuf[MAX_MCTP_PAYLOAD];
//
//    void initializeMctpData(int type, mctp_eid_t  destinationEid, int network){
//        addr.smctp_family = AF_MCTP;
//        addr.smctp_type = type;
//        addr.smctp_network = network;
//        addr.smctp_addr.s_addr = destinationEid;
//        addr.smctp_tag = MCTP_TAG_OWNER;
//    }
//
//    int createSocket(){
//        sd = socket(AF_MCTP, SOCK_DGRAM, 0);
//        return sd; 
//    }
//
//    const void* removePldmHeader(ByteArray msg){
//        auto pldmHeader_it = msg.data();
//        pldmHeader_it++;
//        return pldmHeader_it;
//    }
//
//    int sendMessage(ByteArray msg){
//        auto trimmed_msg = removePldmHeader(msg);
//        size_t msg_len = msg.size()-1;
//        rc = sendto(sd, trimmed_msg,msg_len,0,reinterpret_cast<struct sockaddr*>(&addr), sizeof(struct sockaddr_mctp));
//        return rc;
//    }
//
//    
//    
//}
