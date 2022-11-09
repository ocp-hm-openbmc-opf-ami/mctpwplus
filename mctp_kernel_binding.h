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
#include <boost/asio.hpp>
using ByteArray = std::vector<uint8_t>;

using ReceiveMessageCallback = std::function<void(void*, mctp_eid_t, bool, uint8_t, const ByteArray &, int)>;
class MCTPKernelBinding
{
    public:
    MCTPKernelBinding(uint8_t type, int network,boost::asio::io_context& io_context);
    struct sockaddr_mctp addr;
    struct sockaddr_mctp recv_addr;
    std::vector<std::vector<char>> queue;
    int sd;
    int fd;
    void read_looper();
    boost::asio::posix::stream_descriptor str;

    ReceiveMessageCallback receiveCallback;
    void setSd(int sock_d);
    void setResponseTag();
    int createSocket();
    int createNonBlockSocket();
    int sendMessage(mctp_eid_t destination_eid,const ByteArray& message);
    int receiveMessage(char rxbuf[], int recv_len);
    void setEid(mctp_eid_t eid);
    void receiveMessageAsync();
    int sendReceiveMessage(mctp_eid_t destination_eid, ByteArray request, char response[], int response_size);
};
