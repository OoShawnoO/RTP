#include "sender_def.h"
#include "rtp.h"
#include "util.h"

char buffer[MAX_BUFFER_SIZE];           /* data buffer max 100MB */
uint64_t timer = 0;                     /* a timer for time wait */
struct sockaddr_in recvAddr;            /* address for send data */
int swindow = 0;                        /* sender's window size*/
int *spackets = 0;                      /* packet's seq list */
int sendfd = 0;                         /* sender's file descriptor */
int seq = 0;                            /* expect seq_num */
uint32_t randomsum = 0;

int sendStart(const char *receiver_ip, uint16_t receiver_port){
    /* build udp socket and save recv address */
    if ((sendfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
        return -1;
    }
    memset((void *)&recvAddr, 0, sizeof(recvAddr));
    recvAddr.sin_family = AF_INET;
    recvAddr.sin_addr.s_addr = inet_addr(receiver_ip);
    recvAddr.sin_port = htons(receiver_port);
    /* get a random seq_num then post START packet */
    randomsum = (uint32_t)rand()%100;
    rtp_packet_t spacket;
    get_pack(&spacket, RTP_START, 0, randomsum);
    if (sendto(sendfd, &spacket, spacket.rtp.length + sizeof(spacket.rtp), 0, (const struct sockaddr *)&recvAddr, (socklen_t)sizeof(recvAddr)) <= 0){
        close(sendfd);
        return -1;
    }
    return 0;
}


int getFileSize(const char* message){
    FILE *rfp;
    fpos_t fposition;
    int fsize;
    if ((rfp = fopen(message, "rb")) == NULL){
        return -1;
    }
    fgetpos(rfp, &fposition);
    fseek(rfp, 0, SEEK_END);
    fsize = ftell(rfp);
    fsetpos(rfp, &fposition);
    if (fread(buffer, sizeof(char), fsize, rfp) == 0){
        return -1;
    }
    if (fclose(rfp) != 0){
        return -1;
    }
    return fsize;
}

int WaitACK(rtp_packet_t *apacket,struct sockaddr_in *sendAddr,socklen_t *slen,int iffunc(uint64_t),int elsefunc(rtp_packet_t,uint64_t*)){
    timer = now_us();
    while (1){
        /* wait ACK packet */
        if (recvfrom(sendfd, &apacket, PACKET_SIZE, MSG_DONTWAIT,(struct sockaddr*)sendAddr, slen)< 0){
            // if (now_us() > timer + MAX_WAIT_ACK){
            //     close(sendfd);
            //     return -1;
            // }
            if(iffunc(timer) < 0){
                return -1;
            }
        }
        else{
            // if (check(&apacket, RTP_ACK) < 0){
            // }
            // else if (randomsum + 1 != apacket.rtp.seq_num){
            //     close(sendfd);
            //     return -1;
            // }
            // else{
            //     close(sendfd);
            //     return -1;
            // }
            if(elsefunc(*apacket,&timer) < 0){
                return -1;
            }
        }
    }
}


/*
####################################################################################################

Function:initSender
@params recevier_ip : ip for send
@params port : port for send
@returns: 0 for initialize success
@returns: -1 for initialize failed

1. build udp socket
2. save receiver's address
3. send packet with RTP_START and wait for ACK with right seq_num
4. if timeout return -1
####################################################################################################
*/

int s_init_if_func(uint64_t timer){
    if (now_us() > timer + MAX_WAIT_ACK){
        close(sendfd);
        return -1;
    }
    return 0;
}

int s_init_else_func(rtp_packet_t apacket,uint64_t* timer){
    if (check(&apacket, RTP_ACK) < 0){
    }
    else if (randomsum + 1 != apacket.rtp.seq_num){
        close(sendfd);
        return -1;
    }
    else{
        close(sendfd);
        return -1;
    }
    return 0;
}

int initSender(const char *receiver_ip, uint16_t receiver_port, uint32_t window_size){
    if(sendStart(receiver_ip,receiver_port) < 0){
        return -1;
    }
    struct sockaddr_in sendAddr;
    socklen_t slen = sizeof(struct sockaddr_in);
    rtp_packet_t apacket;
    /* recv from sender */
    timer = now_us();
    while (1){
        if (recvfrom(sendfd, &apacket, PACKET_SIZE, MSG_DONTWAIT, (struct sockaddr *)&sendAddr, &slen) >= 0){
            /* recv data */
            /* check type */
            if (check(&apacket, RTP_ACK) < 0){
            }else if (randomsum != apacket.rtp.seq_num){
                /* seq is not random wrong */
                close(sendfd);
                return -1;
            }else{
                /* right */
                break;
            }
            return 0;
            
        }else{
            /* no data */
            /* check timer timeout or not */
            if (now_us() > timer + MAX_WAIT_ACK){
                /* time out */
                rtp_packet_t epacket;
                get_pack(&epacket, RTP_END, 0, randomsum + 1);
                if (sendto(sendfd, &epacket, epacket.rtp.length + sizeof(epacket.rtp), 0, (const struct sockaddr *)&recvAddr, (socklen_t)sizeof(recvAddr)) <= 0){
                    close(sendfd);
                    return -1;
                }
                if(WaitACK(&apacket,&sendAddr,&slen,s_init_if_func,s_init_else_func)<0){
                    return -1;
                }
            }
        }
    }

    swindow = window_size;
    spackets = malloc(window_size * sizeof(int));

    return 0;
}

/*
####################################################################################################

Function:sendMessage
@params message : file path for read

@returns: 0 for send success
@returns: -1 for send failed

1. get file's size
2. send packet with each part data of file and RTP_DATA type
3. wait for ACK and if receive END terminate sender
4. if timeout return -1
5. this function re-post all seq_num part data of file in the window

####################################################################################################
*/

int sendMessage(const char *message){
    /* get file total size */
    int fsize = 0;
    if((fsize = getFileSize(message))<0){
        return -1;
    }
    int min = 0;
    /* build packet */
    rtp_packet_t dpacket;
    struct sockaddr_in sendAddr;
    socklen_t slen = sizeof(struct sockaddr_in);
    rtp_packet_t apacket;
    /* set timer */
    timer = now_us();
    while (min * PAYLOAD_SIZE < fsize){
        int ret = recvfrom(sendfd, &apacket, PACKET_SIZE, MSG_DONTWAIT, (struct sockaddr *)&sendAddr, &slen);
        if (ret < 0){
            /* no recv */
            /* check timer if timeout repost all packet */
            if (now_us() > timer + MAX_WAIT_ACK)
            {
                seq = min;
            }
        }
        else{
            /* recv */
            /* check packet type and seq_num */
            if (apacket.rtp.seq_num > min && check(&apacket,RTP_ACK)>=0){
                timer = now_us();
                min = apacket.rtp.seq_num;
            }
        }
        /* send data of file and move seq */
        while (seq < min + swindow && seq * PAYLOAD_SIZE < fsize){
            int dlength = 0;
            dlength = ((fsize - seq * PAYLOAD_SIZE) > PAYLOAD_SIZE) ? PAYLOAD_SIZE : fsize - seq * PAYLOAD_SIZE;
            memcpy(dpacket.payload, buffer + seq * PAYLOAD_SIZE, dlength);
            get_pack(&dpacket, RTP_DATA, dlength, seq);
            if (sendto(sendfd, &dpacket, dpacket.rtp.length + sizeof(dpacket.rtp), 0, (const struct sockaddr *)&recvAddr, (socklen_t)sizeof(recvAddr)) <= 0){
                return -1;
            }
            seq++;
        }
    }
    return 0;
}

/*
####################################################################################################

Function:terminateSender
@params none

@returns: none

1. send packet with RTP_END to close the rtp-link with receiver
2. wait for ACK if timeout return -1
3. close the file descriptor

####################################################################################################
*/

int terminate_if_func(uint64_t timer){
    if (now_us() > timer + MAX_WAIT_ACK){
        close(sendfd);
        return -1;
    }
    return 0;
}

int terminate_else_func(rtp_packet_t apacket,uint64_t* timer){
    if (check(&apacket, RTP_ACK) < 0 || seq != apacket.rtp.seq_num){
        return 1;
    }
    else{
        return 0;
    }
}

void terminateSender(){
    /* send End packet */
    rtp_packet_t epacket;
    get_pack(&epacket, RTP_END, 0, seq);
    if (sendto(sendfd, &epacket, epacket.rtp.length + sizeof(epacket.rtp), 0, (const struct sockaddr *)&recvAddr, (socklen_t)sizeof(recvAddr)) <= 0){
        return;
    }
    /* set timer */
    timer = now_us();
    struct sockaddr_in sendAddr;
    socklen_t slen = sizeof(struct sockaddr_in);
    rtp_packet_t apacket;
    while (1){
        /* wait ACK packet */
        if (recvfrom(sendfd, &apacket, PACKET_SIZE, MSG_DONTWAIT, (struct sockaddr *)&sendAddr, &slen) < 0){
            /* no packet */
            /* check timeout */
            if (now_us() > timer + MAX_WAIT_ACK){
                close(sendfd);
                return;
            }
        }else{
            /* have data*/
            /* check type and seq_num */
            if (check(&apacket, RTP_ACK) < 0 || seq != apacket.rtp.seq_num){
            }
            else{
                break;
            }
        }
    }
    close(sendfd);
    return;
}

/*
####################################################################################################

Function:sendMessageOpt
@params message : file path for read

@returns: 0 for send success
@returns: -1 for send failed

1. get file's size
2. send packet with each part data of file and RTP_DATA type
3. wait for ACK and if receive END terminate sender
4. if timeout return -1
5. this function just re-post the needed seq_num part data of file

####################################################################################################
*/

int sendMessageOpt(const char *message){
    /* get total size of data */
    int fsize = 0;
    if((fsize = getFileSize(message))<0){
        return -1;
    }
    int min = 0;

    rtp_packet_t dpacket;
    struct sockaddr_in sendAddr;
    socklen_t slen = sizeof(struct sockaddr_in);
    rtp_packet_t apacket;
    /* set timer */
    timer = now_us();
    while (min * PAYLOAD_SIZE < fsize){
        /* recv ACK packet and send DATA packet record not save */
        int ret = recvfrom(sendfd, &apacket, PACKET_SIZE, MSG_DONTWAIT, (struct sockaddr *)&sendAddr, &slen);
        if (ret < 0){
            if (now_us() > timer + MAX_WAIT_ACK){
                seq = min;
            }
        }else{
            if (check(&apacket, RTP_ACK) < 0){
            }
            else if (apacket.rtp.seq_num < min + swindow){
                timer = now_us();
                if (apacket.rtp.seq_num >= min){
                    spackets[apacket.rtp.seq_num % swindow] = 1;
                    while (spackets[min % swindow]){
                        spackets[min % swindow] = 0;
                        min++;
                    }
                }
            }
        }
        
        while (seq < min + swindow && seq * PAYLOAD_SIZE < fsize){
            if (spackets[seq % swindow] == 1){
                seq++;
                continue;
            }
            /* just re-port not save packet */
            int dlength = 0;
            dlength = ((fsize - seq * PAYLOAD_SIZE) > PAYLOAD_SIZE) ? PAYLOAD_SIZE : fsize - seq * PAYLOAD_SIZE;
            memcpy(dpacket.payload, buffer + seq * PAYLOAD_SIZE, dlength);
            get_pack(&dpacket, RTP_DATA, dlength, seq);
            if (sendto(sendfd, &dpacket, dpacket.rtp.length + sizeof(dpacket.rtp), 0, (const struct sockaddr *)&recvAddr, (socklen_t)sizeof(recvAddr)) <= 0){
                return -1;
            }
            seq++;
        }
    }
    return 0;
}