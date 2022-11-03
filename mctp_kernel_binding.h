#define PLDM_MESSAGE 1
#define MAX_MCTP_PAYLOAD 64
#include <sys/socket.h>
#include "mctp.h"
#include <stdlib.h>
#include <vector>
#include <err.h>
#include <string.h>
#include <errno.h>
#include <functional>
using ByteArray = std::vector<uint8_t>;

class MCTPKernelBinding
{
    public:
    MCTPKernelBinding(uint8_t type, int network);

    struct sockaddr_mctp addr;
    struct sockaddr_mctp recv_addr;
    int sd;

    void setSd(int sock_d);
    void setResponseTag();
    int createSocket();
    int createNonBlockSocket();
    int sendMessage(mctp_eid_t destination_eid, ByteArray& message);
    int receiveMessage(char rxbuf[], int recv_len);
    void setEid(mctp_eid_t eid);

    int sendReceiveMessage(mctp_eid_t destination_eid, ByteArray request, char response[], int response_size);
};
