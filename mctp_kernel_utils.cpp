#include <vector>
#include <stdint.h>
void* removePldmHeader(std::vector<uint8_t> message, std::size_t &msgSize){
    auto messageData_it = message.data();
    messageData_it++;
    msgSize = msgSize - 1;
    return messageData_it;
}
