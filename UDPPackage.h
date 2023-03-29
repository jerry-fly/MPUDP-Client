#include<string>
#include<string.h>
// #include<cstring>
#include<stdint.h>
//#include<WinSock2.h>
//#include<WinUser.h>
//#include <Windows.h>
#include<atomic>
#include<limits.h>


using namespace std;
//debug
#define debug true

//缓冲区大小4202496B（512*8208B）()
#define BUFSIZE 4259840//4202496
#define BUFNUM 4096//512 //(BUFSIZE/PACKSIZE)
//报文首部长度16B
#define UDPHEADLEN 16
//报文data最大8KB   1k
#define PACKDATASIZE 1024//8192
//一个pkg大小8208B 1040
int PACKSIZE = PACKDATASIZE+UDPHEADLEN;
//序号范围0~2^32-1
uint32_t SEQMAX = UINT_MAX;
// FLAG VALUE
#define SYN 100
#define SYNACK 101
#define ACK 1
#define FIN 10
#define FINACK 11

#define FILE_INFO_REQUEST 2
#define FILE_INFO_RESPONSE 3
#define FILE_INFO_COMPLETE 4
#define FILE_DATA 5
#define FILE_RETRANSMISSION_REQ 6
#define FILE_RETRANSMISSION_RES 7
#define FILE_FIN 8
//滑动窗口大小N个报文，小于BUFNUM/2
#define N 64
//滑动窗口大小B（报文数量N（报文按最大长度计算））
int SLIDE_WINSIZE = N*PACKDATASIZE;
//timer handle num
#define TIMER_MAX BUFNUM

//文件路径
char infilename[100]; //(server use) "test/1.jpg" "test/2.jpg" "test/3.jpg" "test/helloworld.txt"
char outfilename[100]; //(client use) "output/1.jpg" "output/2.jpg" "output/3.jpg" "output/helloworld.txt"
string debug_filename = "test.mp4";
int file_len;
int file_packet_num;

struct UDPPackage
{//首部16字节
    uint32_t seq;
    uint32_t ack;
    uint8_t FLAG;
    uint8_t NOTUSED;
    uint16_t WINDOWSIZE;
    uint16_t Length; //Bytes,不包含首部，data字节数
    uint16_t Checksum;
    char data[PACKDATASIZE];
};

typedef struct endpoint_s {
    struct sockaddr_in addr_storage;
    socklen_t addr_storage_len;
}endpoint;

typedef struct udp_packet_q_item_s {
  struct UDPPackage* buffer;
  endpoint r_endpoint;
  size_t size;
} udp_packet_q_item_t;

void initUDPPackage(UDPPackage *u){
    u->seq = 0;
    u->ack = 0;
    u->FLAG = 0;
    u->NOTUSED = 0;
    u->WINDOWSIZE = 0;
    u->Length = 0;
    u->Checksum = 0;
    //printf("u %p , u->data %p\n",u , u->data);
    memset(u->data, 0, PACKDATASIZE);
    //printf("!!!\n");
}

uint16_t checksumFunc(UDPPackage *pkg, int pkg_size){
    int count = (pkg_size + 1) / 2;
    uint16_t *buf = new uint16_t[pkg_size+1];
    memset(buf, 0, pkg_size+1);
    memcpy(buf, (uint16_t*)pkg, pkg_size);
	unsigned int checksum = 0;
	while (count--) {
		checksum += *buf++;
		if (checksum & 0xffff0000) {
			checksum &= 0xffff;
			checksum++;
		}
	}
	return ~(checksum & 0xffff);
}

