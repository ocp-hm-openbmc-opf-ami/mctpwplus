#include "mctp_kernel_binding.h"
#include <err.h>
#include <stdio.h>
void test(){
    ByteArray msg;
    char *rxbuf;
    msg.push_back(0x80);
    msg.push_back(0x02);
    
    MCTPKernelBinding mctpk(0x00, 1);
    int r = mctpk.sendReceiveMessage(0xCD, msg, &rxbuf);
    for(int i=0;i<r;i++){
        printf("0x%02x ",rxbuf[i]);
    }
}
