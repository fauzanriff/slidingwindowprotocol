/*
* cksum.h
*/

#ifndef CKSUM_H
#define CKSUM_H

#include <pcap/pcap.h>
#include <sys/types.h>
#include <pcap-bpf.h>
#include "header.h"

Byte cksum(Byte *buf, int count);
void initiateCksum(MESGB *message);

#endif