#include "receiver_def.h"
#include "rtp.h"
#include "util.h"

int rwindow = 0;                                /* receiver's windows size */
rtp_packet_t *packets = 0;                      /* packets cache for re-post */
int recvfd = 0;                                 /* receiver's file descriptor */
char buf[MAX_BUFFER_SIZE];                      /* buffer for data  max size 100MB*/
FILE *recvfp;                                   /* receiver's file pointer  */
int fsize = 0;                                  /* file's size */
int min = 0;                                    /* minium seq_num for window */
struct sockaddr_in sendAddr;                    /* address for recv */
socklen_t length = sizeof(struct sockaddr_in);  /* recv address's length */


int sendACK(struct sockaddr_in sendAddrT,uint32_t seq){
    /* send ACK packet */
    rtp_packet_t apacket;
    get_pack(&apacket, RTP_ACK, 0, seq);
    if (sendto(recvfd, &apacket, sizeof(apacket.rtp) + apacket.rtp.length, 0, (struct sockaddr *)&sendAddrT, sizeof(sendAddrT)) < 0){
        close(recvfd);
        return -1;
    }
    return 0;
}

int Wait(rtp_packet_t *spacket,struct sockaddr_in *saddr,socklen_t *slen,int iffunc(uint64_t),int elsefunc(rtp_packet_t,uint64_t*)){
    /* set timer */
    uint64_t timer = now_us();
    while(1){
        if (recvfrom(recvfd, spacket, PACKET_SIZE, 0, (struct sockaddr *)saddr, slen) < 0){   
            if(iffunc(timer)<0){
                return -1;
            }
        }
        else{
            int ret = elsefunc(*spacket,&timer);
            if(ret == 0){
                return 0;
            }else if(ret == 1){
                continue;
            }else{
                return -1;
            }
        }
    }
}

int init_else_func(rtp_packet_t spacket,uint64_t* timer){
    if (check(&spacket, RTP_START) < 0){
        close(recvfd);
        return -1;
    }
    else{
        return 0;
    }
}

int init_if_func(uint64_t timer){
    if (now_us() > timer + MAX_WAIT_RECV)
    {
        close(recvfd);
        return -1;
    }
    return 0;
}

/*
####################################################################################################

Function:initReceiver
@params port: port for bind and listen
@params window_size: rtp window size
@returns: 0 for initialize success
@returns: -1 for initialize failed

1. bind port and listen
2. when listening data > 0 indicate there comes packet with data
3. check packet's type if type is RTP_START return packet with RTP_ACK and create rtp-link
4. if timeout return -1
####################################################################################################
*/
int initReceiver(uint16_t port, uint32_t window_size)
{
    /* bind port */
    if ((recvfd = Bind(port)) < 0)
        return -1;

    rtp_packet_t spacket;
    struct sockaddr_in saddr;
    socklen_t slen = sizeof(struct sockaddr_in);
    if(Wait(&spacket,&saddr,&slen,init_if_func,init_else_func)<0){
        return -1;
    }
    if(sendACK(saddr,spacket.rtp.seq_num) < 0){
        return -1;
    }
    rwindow = window_size;
    packets = malloc(window_size * sizeof(rtp_packet_t));
    memset(packets, 0, window_size * sizeof(rtp_packet_t));
    return 0;
}

/*
####################################################################################################

Function:recvMessage
@params filename : path for save file

@returns: 0 for recv data and save file success
@returns: -1 for recv data failed or save file failed

1. open file and get a pointer so as to save file
2. listen data if packet's type = RTP_DATA compare seq_num with recevier's seq
    if seq_num > seq but seq_num < seq + windowsize record and cache it
    if seq_num > seq + windowsize drop the packet
    if seq_num = seq save the cache into buffer and return the mininum seq_num which not be cached
    if packet's type = RTP_END terminate Receiver

####################################################################################################
*/

int recv_else_func(rtp_packet_t dpacket,uint64_t* timer){
    /* recv data */
    *timer = now_us();
    if (check(&dpacket, RTP_DATA) < 0){
        if (check(&dpacket, RTP_END) == 0){
            if (dpacket.rtp.seq_num == min)
            {
                return 0;
            }
        }
    }else{
        /* copy data into buffer and move window */
        if (dpacket.rtp.seq_num < min + rwindow && dpacket.rtp.seq_num >= min){
            memcpy(&packets[dpacket.rtp.seq_num % rwindow], &dpacket, sizeof(dpacket.rtp) + dpacket.rtp.length);
            while (packets[min % rwindow].rtp.length){
                memcpy(buf + fsize, packets[min % rwindow].payload, packets[min % rwindow].rtp.length);
                fsize += packets[min % rwindow].rtp.length;
                packets[min % rwindow].rtp.length = 0;
                min++;
            }
        }
        else if (dpacket.rtp.seq_num >= min + rwindow){
            return 1;
        }
        if(sendACK(sendAddr,min) < 0){
            return -1;
        }
    }
    return 1;
}

int recv_if_func(uint64_t timer){
    if (now_us() > timer + MAX_WAIT_RECV)
    {
        return -1;
    }
    return 0;
}

int recvMessage(char *filename){
    if ((recvfp = fopen(filename, "wb+")) < 0){
        close(recvfd);
        return -1;
    }

    rtp_packet_t dpacket;
    if(Wait(&dpacket,&sendAddr,&length,recv_if_func,recv_else_func)<0){
        return -1;
    }
    return 0;
}

/*
####################################################################################################

Function:terminateReceiver
@params none

@returns: none

1. close the rtp-link and save file then free file pointer

####################################################################################################
*/

void terminateReceiver(){
    /* send End packet */
    rtp_packet_t eapacket;
    get_pack(&eapacket, RTP_ACK, 0, min);
    if (sendto(recvfd, &eapacket, sizeof(eapacket.rtp) + eapacket.rtp.length, 0, (struct sockaddr *)&sendAddr, sizeof(sendAddr)) < 0){
        close(recvfd);
        return;
    }
    if (fwrite(buf, sizeof(char), fsize, recvfp) < 0){
        close(recvfd);
        return;
    }
    if (fclose(recvfp) != 0){
        close(recvfd);
        return;
    }
    close(recvfd);
    return;
}

int recvopt_if_func(uint64_t timer){
    if (now_us() > timer + MAX_WAIT_RECV){
        return -1;
    }
    return 0;
}

int recvopt_else_func(rtp_packet_t dpacket,uint64_t* timer){
    *timer = now_us();
    if (check(&dpacket, RTP_DATA) < 0){
        if (check(&dpacket, RTP_END) == 0){
            if (dpacket.rtp.seq_num == min + 1){
                return 0;
            }
            else{
                return 0;
            }
        }
    }
    else{
        if (dpacket.rtp.seq_num < min + rwindow && dpacket.rtp.seq_num >= min){
            memcpy(&packets[dpacket.rtp.seq_num % rwindow], &dpacket, sizeof(dpacket.rtp) + dpacket.rtp.length);
            while (packets[min % rwindow].rtp.length)
            {
                memcpy(buf + fsize, packets[min % rwindow].payload, packets[min % rwindow].rtp.length);
                fsize += packets[min % rwindow].rtp.length;
                packets[min % rwindow].rtp.length = 0;
                min++;
            }
        }
        else if (dpacket.rtp.seq_num >= min + rwindow){
            return 1;
        }

        rtp_packet_t apacket;
        get_pack(&apacket, RTP_ACK, 0, dpacket.rtp.seq_num);
        if (sendto(recvfd, &apacket, sizeof(apacket.rtp) + apacket.rtp.length, 0, (struct sockaddr *)&sendAddr, sizeof(sendAddr)) < 0){
            close(recvfd);
            return -1;
        }
    }
    return 1;
}

int recvMessageOpt(char *filename)
{
    if ((recvfp = fopen(filename, "wb+")) < 0){
        close(recvfd);
        return -1;
    }

    rtp_packet_t dpacket;
    if(Wait(&dpacket,&sendAddr,&length,recvopt_if_func,recvopt_else_func) < 0){
        return -1;
    }
    return 0;
}

/*
####################################################################################################

Function:Bind
@params port : port for bind

@returns: binded file descriptor

1. bind udp socket and listen any address's through port

####################################################################################################
*/

int Bind(uint16_t port){
    int tempfd;
    if ((tempfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
        return -1;
    }
    struct sockaddr_in bindAddr;
    memset((void *)&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(port);
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(tempfd, (struct sockaddr *)&bindAddr, sizeof(struct sockaddr))){
        close(tempfd);
        return -1;
    }
    return tempfd;
}