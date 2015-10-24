#include "cksum.h"

Byte cksum(Byte *buf, int count){
    register u_long sum = 0;
    while (count--){
        sum += *buf++;
        if (sum & 0xFFFF0000){
            /* carry occurred, so wrap around */
            sum &= 0xFFFF;
            sum++; 
        }
    }
    return ~(sum & 0xFFFF);
}

void initiateCksum(MESGB *message){
    message->checksum = cksum(message->data, 1);
}