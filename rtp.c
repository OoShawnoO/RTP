#include "rtp.h"
#include "util.h"
#include <string.h>
#include <stdio.h>

void get_pack(rtp_packet_t *packet, uint8_t type, uint16_t length, uint32_t seq_num)
{
    packet->rtp.checksum = 0;
    packet->rtp.type = type;
    packet->rtp.length = length;
    packet->rtp.seq_num = seq_num;
    packet->rtp.checksum = compute_checksum(packet, sizeof(packet->rtp) + length);
}

int check(rtp_packet_t *packet, uint8_t type)
{   
    if (packet->rtp.type != type)   return -1;

    uint32_t check = packet->rtp.checksum;
    packet->rtp.checksum = 0;
    uint32_t sum = compute_checksum(packet, sizeof(packet->rtp) + packet->rtp.length);
    if (check != sum){
        return -1;
    }
    return 0;
}
