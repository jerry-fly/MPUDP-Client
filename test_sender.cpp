#include<iostream>
#include<fstream>
#include<stdio.h>
#include<time.h>
//#include<string>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>  
#include <sys/time.h> 
#include <sys/socket.h> 
#include <arpa/inet.h>  
#include <netinet/in.h>
#include <pthread.h>  
#include <cmath>
//#include <stdint.h>
//#include<WinSock2.h>
#include "UDPPackage.h"
//定时器库
#include "assertions.h"
#include "queue.h"
#include "timer.h"
/* g++ -std=c++11 test_sender.cpp -lpthread -lmytimer -L ./build/ -lrt */
using namespace std;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct sockaddr_in saddr1, saddr2;
//rpkg,spkg
UDPPackage *rpkg;
UDPPackage *spkg1;
UDPPackage *spkg2;
int seq=0;

int init_if1(){
    int sd = socket(PF_INET, SOCK_DGRAM, 0);
    if(sd == -1) {
        perror("socket");
        exit(-1);
    }   
    //服务器的地址信息
    //struct sockaddr_in saddr;
    saddr1.sin_family = AF_INET;
    saddr1.sin_port = htons(9999);
    inet_pton(AF_INET, "127.0.0.1", &saddr1.sin_addr.s_addr);
    return sd;
}
int init_if2(){
    int sd = socket(PF_INET, SOCK_DGRAM, 0);
    if(sd == -1) {
        perror("socket");
        exit(-1);
    }
    //服务器的地址信息
    //struct sockaddr_in saddr;
    saddr2.sin_family = AF_INET;
    saddr2.sin_port = htons(9998);
    inet_pton(AF_INET, "127.0.0.1", &saddr2.sin_addr.s_addr);
    return sd;
}

void* sendThread1(void *args){
    //int seq=0;
    int sd=-1;
    if(args){
        sd=*(int *)args;
    }else{
        exit(-1);
    }
    while(1){
        initUDPPackage(spkg1);
        pthread_mutex_lock(&mutex);
        spkg1->seq = seq;
        pthread_mutex_unlock(&mutex);
        //seq = (seq + 1) % SEQMAX;
        __sync_add_and_fetch (&seq,1);
        spkg1->Length = PACKDATASIZE;
        sprintf(spkg1->data, "hello , I am 4G %d", spkg1->seq);
        printf("%s\n", spkg1->data);
        sendto(sd, spkg1, sizeof(*spkg1), 0, (struct sockaddr *)&saddr1, sizeof(struct sockaddr));
        usSleep(100000);
    }
}

void* sendThread2(void *args){
    //int seq=0;
    int sd=-1;
    if(args){
        sd=*(int *)args;
    }else{
        exit(-1);
    }
    while(1){
        initUDPPackage(spkg2);
        pthread_mutex_lock(&mutex);
        spkg2->seq = seq;
        pthread_mutex_unlock(&mutex);
        //seq = (seq + 1) % SEQMAX;
        __sync_add_and_fetch (&seq,1);
        spkg2->Length = PACKDATASIZE;
        sprintf(spkg2->data, "hello , I am WiFi %d", spkg2->seq);
        printf("%s\n", spkg2->data);
        //memcpy(spkg->data, file_data + sent_offset, spkg->Length);
        sendto(sd, spkg2, sizeof(*spkg2), 0, (struct sockaddr *)&saddr2, sizeof(struct sockaddr));
        usSleep(100000);
    }
}

int main(){
    pthread_t ppid_s1,ppid_s2;

    spkg1 = new UDPPackage();
    initUDPPackage(spkg1);
    spkg2 = new UDPPackage();
    initUDPPackage(spkg2);

    
    int sd1 = init_if1();
    int sd2 = init_if2();

    int rc = pthread_create(&ppid_s1, NULL, sendThread1, (void*)&sd1);
    if (rc != 0){
        printf("{---CreateSendThread error: %s}\n", strerror(errno));
        return 1;
    }
    rc = pthread_create(&ppid_s2, NULL, sendThread2, (void*)&sd2);
    if (rc != 0){
        printf("{---CreateSendThread error: %s}\n", strerror(errno));
        return 1;
    }
    pthread_join(ppid_s1,NULL);
    pthread_join(ppid_s2,NULL);
    return 0;
}