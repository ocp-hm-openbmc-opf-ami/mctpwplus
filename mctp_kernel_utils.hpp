#include "mctp.h"
#include <vector>
#include <string.h>
using ByteArray = std::vector<uint8_t>;

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
