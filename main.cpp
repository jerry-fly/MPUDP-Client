#include<iostream>
#include<fstream>
#include<stdio.h>
#include<unistd.h>
#include <string.h>
#include <sys/types.h>  
#include <sys/socket.h> 
#include <arpa/inet.h>  
#include <netinet/in.h> 
#include <net/if.h>
#include <queue>
#include <thread>
#include <mutex>
#include <folly/MPMCQueue.h>
#include <boost/asio.hpp>
#include "cbuf.h"
#include "timer.h"
#include "UDPPackage.h"
#include "assertions.h"

using namespace std;

#define SERVER_IP "10.0.0.1"
#define SERVER_PORT (40000)

#define RECV1_BLOCKS (1024)
#define RECV2_BLOCKS (1024)
#define APP_BLOCKS (256)

//ifstream ifile;

//boost::asio::io_service io_service;
//std::mutex message_queue_mutex;
//std::condition_variable queue_cond;
//socket
int sd_4G, sd_wifi;
struct sockaddr_in send_addr;
//pkg
UDPPackage *spkg_4G;
UDPPackage *spkg_retransmission;
UDPPackage *rpkg_4G;
UDPPackage *rpkg_wifi;
//global packet seq
int g_seq=1;
//MPMCQueue<struct UDPPackage>
//recv pool
folly::MPMCQueue<udp_packet_q_item_t*> *recv_free_pool1;
folly::MPMCQueue<udp_packet_q_item_t*> *recv_work_pool1;
folly::MPMCQueue<udp_packet_q_item_t*> *recv_free_pool2;
folly::MPMCQueue<udp_packet_q_item_t*> *recv_work_pool2;
struct UDPPackage* recv_buffer_alloc_1;
struct UDPPackage* recv_buffer_alloc_2;

std::vector<std::thread> threads_1;
std::vector<std::thread> threads_2;
//folly::MPMCQueue<int> total_buf(APP_BLOCKS);

struct sort_by_x
{ //按x降序
    bool operator()(struct UDPPackage* a, struct UDPPackage* b){
        return a->seq > b->seq;
    }
};

priority_queue<struct UDPPackage*, vector<struct UDPPackage*>, sort_by_x> message_queue;
//priority_queue<struct UDPPackage*, vector<struct UDPPackage*>, sort_by_x> retransmission_queue;
priority_queue<uint32_t, vector<uint32_t>, less<uint32_t>> retransmission_queue;

pthread_mutex_t message_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t message_queue_cond_lock = PTHREAD_COND_INITIALIZER;
pthread_mutex_t retransmission_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t retransmission_queue_cond_lock = PTHREAD_COND_INITIALIZER;
const int queue_size=256;

pthread_t timer_ppid;
long Timer[256];
unordered_map<uint32_t, long> seq_timer_map;
int timer_num;
//vector<struct UDPPackage*> message_queue;

static cbuf_t cmd;
static int line_1[200];
static int line_2[200];
//static int temp = 0;

//file
ofstream outfile;
int realtime_file_bytes = 0;

static bool line1_finish = false;
static bool line2_finish = false;


bool cmp_UDPPackage(struct UDPPackage* a, struct UDPPackage* b){
    return a->seq > b->seq;
}

void print_queue(priority_queue<int> q){
    printf("priority queue size %ld, element:\n", q.size());
    while (!q.empty())
    {
        printf("%d ", q.top()) ;
        q.pop();
    }
    printf("\n");  
}

void clear_queue(priority_queue<int, vector<int>, greater<int>>& q){
    //printf("priority queue size %ld, element:\n", q.size());
    //int temp;
    string str;
    while (!q.empty())
    {
        str=to_string(q.top()) + "\n";
        outfile.write(str.c_str(), str.length());
        //printf("%d ", q.top()) ;
        q.pop();
    }
    outfile.write("\n", sizeof("\n")-1);
    //printf("\n");  
}

void* producer_1(void *data){
    int32_t i = 0;
    for(i = 0; i < 200; i++)
    {
        line_1[i] = i+1000;
        cbuf_enqueue(&cmd, &line_1[i]);

        if(0 == (i % 9)) sleep(1);
    }

    line1_finish = true;

    return NULL;
}

void* producer_2(void *data){
    int32_t i = 0;
    for(i = 0; i < 200; i++)
    {
        line_2[i] = i+20000;
        cbuf_enqueue(&cmd, &line_2[i]);
        printf("enqueue %d\n", line_2[i]);
        if(0 == (i % 9)) sleep(1);
    }

    line2_finish = true;

    return NULL;
}

void* consumer(void *data){
    int32_t *ptr = NULL;
    while(1)
    {
        ptr = (int32_t *)cbuf_dequeue(&cmd);
        printf("dequeue %d\n",*ptr);

        if(cbuf_empty(&cmd) && line2_finish && line1_finish)
        {
            printf("quit\n");
            break;
        }
    }

    return NULL;
}

void *signal_receiver_task(void *args){
  pthread_detach(pthread_self());

  int end=0;
  while (end == 0) {
    signal_handle(&end);
  }
  exit(0);
}

static void timer_callback(sigval_t args){
    //printf("!!!\n");
    socklen_t send_len = sizeof(struct sockaddr);
    initUDPPackage(spkg_retransmission);
    spkg_retransmission->seq = (long)args.sival_ptr;
    spkg_retransmission->FLAG = FILE_RETRANSMISSION_REQ;
    //printf("retry seq %d\n", spkg_retransmission->seq);
    sendto(sd_4G, (char*)spkg_retransmission, sizeof(*spkg_retransmission), 0, (struct sockaddr *)&send_addr, send_len);
}

static inline int send_msg_to_task_locked( struct UDPPackage* buffer) {
    struct UDPPackage* message = (struct UDPPackage*) calloc(1, PACKSIZE);
    memcpy(message, buffer, PACKSIZE);
    if(message->FLAG == FILE_RETRANSMISSION_RES){
        //remove timer
        timer_remove(Timer[seq_timer_map[message->seq]], NULL);
        seq_timer_map.erase(message->seq);
        timer_num--;
    }
    pthread_mutex_lock (&message_queue_mutex);
    //std::lock_guard<std::mutex> lock(message_queue_mutex);
    size_t s=message_queue.size();

    // if ( s > queue_size )
    //   printf("Queue contains %ld messages, need retransmission!\n", s );


    message_queue.push(message);
    //queue_cond.notify_all();
    pthread_cond_broadcast(&message_queue_cond_lock);
    pthread_mutex_unlock (&message_queue_mutex);
    
    return 0;
}

int udp_init_4G(void *arg){
    struct sockaddr_in addr; 
	struct ifreq ifr;
	
	int port = 7788;
	//int sendlen = 0;
	//int i;
	
	if(arg) port = *(int *)arg;

	int sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);   
	if(sd < 0){  
	    printf("[log] socket function failed with error: %s\n", strerror(errno));
	    return -1;  
	}
    /* bind to interface */
	memset(&ifr, 0x00, sizeof(ifr));
	strcpy(ifr.ifr_name, "oaitun_ue1");
	if(setsockopt(sd, SOL_SOCKET, SO_BINDTODEVICE, (char *)&ifr, sizeof(ifr)))
	{
		printf("4G_sock SO_BINDTODEVICE failed : %s\n", strerror(errno));
		return -1;
	}

	
	memset(&addr, 0, sizeof(addr));   
	addr.sin_family = AF_INET;   
	addr.sin_port = htons(port);    
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(sd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
	     printf("[4G] bind error : %s\n", strerror(errno));   
	     exit(1);           
	}
    printf("[log] 4G socket function Success\n");
    return sd;  
}


void *udp_read_4G_loop(void *args){
    uint64_t count              = 0;
    udp_packet_q_item_t* worker = nullptr;

    //sched_params.apply(TASK_NONE, Logger::udp());

    while (1) {
        recv_free_pool1->blockingRead(worker);
        ++count;
        // std::cout << "d" << count << std::endl;
        // exit thread
        if (worker->buffer == nullptr) {
            free(worker);
            while (recv_work_pool1->readIfNotEmpty(worker)) {
                free(worker);
            }
            // std::cout << "exit d" << count << std::endl;
            return NULL;
        }
        worker->r_endpoint.addr_storage_len = sizeof(struct sockaddr_storage);
        if ((worker->size = recvfrom( sd_4G, worker->buffer, PACKSIZE, 0,
                (struct sockaddr*) &worker->r_endpoint.addr_storage,
                &worker->r_endpoint.addr_storage_len)) > 0) {
            recv_work_pool1->write(worker);
        } else {
            printf("[4G] Recvfrom failed %s\n", strerror(errno));
            recv_free_pool1->write(worker);
        }
        worker = nullptr;
    }
}

void *udp_worker_4G_loop(void *args){
    uint64_t count              = 0;
    udp_packet_q_item_t* worker = nullptr;

    //sched_params.apply(TASK_NONE, Logger::udp());
    while (1) {
        recv_work_pool1->blockingRead(worker);
        ++count;
        // std::cout << "w" << id << " " << count << std::endl;
        // exit thread
        if (worker->buffer) {
        //app_->handle_receive(worker->buffer, worker->size, worker->r_endpoint);
            //printf("[4G] recv : %d\n", worker->buffer->seq);
            send_msg_to_task_locked(worker->buffer);
            //cout<< "[4G] recv : " << worker->buffer->data << endl;

            recv_free_pool1->write(worker);
        } else {
            free(worker);
            // std::cout << "exit w" << id << " " << count << std::endl;
            while (recv_work_pool1->readIfNotEmpty(worker)) {
                free(worker);
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            return NULL;
        }
        worker = nullptr;
    }
}

int udp_init_wifi(void *arg){
    struct sockaddr_in addr; 

	//struct sockaddr_in send_addr;
	struct ifreq ifr;
	
	int port = 5566;
	//int sendlen = 0;
	//int i;
	
	if(arg) port = *(int *)arg;

	int sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);   
	if(sd < 0)  
	{  
	    printf("sd %d error : %s\n",sd , strerror(errno));  
	    return -1;  
	}
	memset(&ifr, 0x00, sizeof(ifr));
	
	strcpy(ifr.ifr_name, "wlx00117f1ba999");
	if(setsockopt(sd, SOL_SOCKET, SO_BINDTODEVICE, (char *)&ifr, sizeof(ifr)))
	{
		perror("wifi_sock SO_BINDTODEVICE failed\n");
		return -1;
	}
    // int on = 1;
    // if(setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
	// {
	// 	perror("wifi_sock SO_REUSEADDR failed\n");
	// 	return -1;
	// }
	
	
	memset(&addr, 0, sizeof(addr));   
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	// memset(&send_addr, 0, sizeof(send_addr));   
	// send_addr.sin_family = AF_INET;   
	// send_addr.sin_port = htons(SERVER_PORT);    
	// send_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

	if(bind(sd, (struct sockaddr*)&addr, sizeof(addr)) < 0){                 
	     printf("[wifi] bind error : %s\n", strerror(errno));   
	     exit(1);         
	}  
    printf("[log] WiFi socket function Success\n"); 
    return sd;
}

void *udp_read_wifi_loop(void *args){
    uint64_t count              = 0;
    udp_packet_q_item_t* worker = nullptr;

    //sched_params.apply(TASK_NONE, Logger::udp());

    while (1) {
        recv_free_pool2->blockingRead(worker);
        ++count;
        // std::cout << "d" << count << std::endl;
        // exit thread
        if (worker->buffer == nullptr) {
            free(worker);
            while (recv_work_pool2->readIfNotEmpty(worker)) {
                free(worker);
            }
            // std::cout << "exit d" << count << std::endl;
            return NULL;
        }
        worker->r_endpoint.addr_storage_len = sizeof(struct sockaddr_storage);
        if ((worker->size = recvfrom( sd_wifi, worker->buffer, PACKSIZE, 0,
                (struct sockaddr*) &worker->r_endpoint.addr_storage,
                &worker->r_endpoint.addr_storage_len)) > 0) {
            recv_work_pool2->write(worker);
        } else {
            printf("[WiFi] Recvfrom failed %s\n", strerror(errno));
            recv_free_pool2->write(worker);
        }
        worker = nullptr;
    }
}

void *udp_worker_wifi_loop(void *args){
    uint64_t count              = 0;
    udp_packet_q_item_t* worker = nullptr;

    //sched_params.apply(TASK_NONE, Logger::udp());
    while (1) {
        recv_work_pool2->blockingRead(worker);
        ++count;
        // std::cout << "w" << id << " " << count << std::endl;
        // exit thread
        if (worker->buffer) {
        //app_->handle_receive(worker->buffer, worker->size, worker->r_endpoint);
            //printf("[WiFi] recv : %d\n", worker->buffer->seq);
            send_msg_to_task_locked(worker->buffer);
            //cout<< "[WiFi] recv : " << worker->buffer->data<< endl;
            recv_free_pool2->write(worker);
        } else {
            free(worker);
            // std::cout << "exit w" << id << " " << count << std::endl;
            while (recv_work_pool2->readIfNotEmpty(worker)) {
                free(worker);
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            return NULL;
        }
        worker = nullptr;
    }
}
void *retransmission_task(void *args){
    while(1){
        pthread_mutex_lock(&retransmission_queue_mutex);
        while (retransmission_queue.empty())
        {
            pthread_cond_wait(&retransmission_queue_cond_lock, &retransmission_queue_mutex);
        }
        //start retransmission
        printf("retransmit seq %d\n", g_seq);
        socklen_t send_len = sizeof(struct sockaddr);
        initUDPPackage(spkg_retransmission);
        spkg_retransmission->seq = retransmission_queue.top();
        spkg_retransmission->FLAG = FILE_RETRANSMISSION_REQ;
        retransmission_queue.pop();
        pthread_mutex_unlock(&retransmission_queue_mutex);
        //usSleep(30000);
        sendto(sd_4G, (char*)spkg_retransmission, sizeof(*spkg_retransmission), 0, (struct sockaddr *)&send_addr, send_len);
        

    }
}

void *app_handler_task(void *args){
    int last_retry_req = 0;
    int retry_times = 0;
    socklen_t send_len = sizeof(struct sockaddr);
    while(1){
        if(g_seq - 1 == file_packet_num){
            printf("g_seq == file_packet_num == %d\n", file_packet_num);
            break;
        }
        pthread_mutex_lock(&message_queue_mutex);
        while(message_queue.empty()){
            pthread_cond_wait(&message_queue_cond_lock, &message_queue_mutex);
        }
        // if(!message_queue.empty() && message_queue.top()->seq != g_seq){
        //     if(last_retry_req != g_seq){
        //         //don't have been retransmited
            
        //         pthread_mutex_lock(&retransmission_queue_mutex);
        //         retransmission_queue.push(g_seq);
                
        //         pthread_cond_broadcast(&retransmission_queue_cond_lock);
        //         pthread_mutex_unlock(&retransmission_queue_mutex);
        //         last_retry_req = g_seq;
        //     }else{
        //         //do nothing, wait
        //     }
        // }
        while(!message_queue.empty()){
            if(message_queue.top()->seq == g_seq)
            {
                UDPPackage *nextPacket = message_queue.top();
                //printf("message queue size %ld\twrite %d --> File\n",message_queue.size() ,message_queue.top()->seq);
                message_queue.pop();
                
                //write file
                outfile.write(nextPacket->data, nextPacket->Length);
                realtime_file_bytes += nextPacket->Length;
                g_seq++;
                retry_times=0;
            }else if(message_queue.top()->seq < g_seq){
                //already received
                message_queue.pop();
            }else{
                //future packet
                if(message_queue.size() > queue_size){
                    
                    if( seq_timer_map.count(g_seq) == 0){
                        if(timer_num > 4) break;
                        printf("expected seq %d ,timer num %d\n", g_seq, timer_num);
                        // initUDPPackage(spkg_retransmission);
                        // spkg_retransmission->seq = g_seq;
                        // spkg_retransmission->FLAG = FILE_RETRANSMISSION_REQ;
                        // sendto(sd_4G, (char*)spkg_retransmission, sizeof(*spkg_retransmission), 0, (struct sockaddr *)&send_addr, send_len);
                        long resent_seq = g_seq;
                        long ret = timer_start(3, 0, &Timer[timer_num], TIMER_PERIODIC, timer_callback, (void *)resent_seq);
                        timer_num++;
                        //printf("timer_num %d\n", timer_num);
                        seq_timer_map.emplace(g_seq, Timer[timer_num]);
                        
                        retry_times=0;
                    }else{
                        retry_times++;
                    }
                    // if(retry_times > 10 ){
                    //     printf("EXCEPTION!\n");
                    //     //exit(1);
                    // }
                }
                break;
            }
        }

        pthread_mutex_unlock(&message_queue_mutex);
        
        
    }
    // io_service.reset();
    // io_service.stop();
    return nullptr;
}


// void *app_control(void *args){
    
//     while(1){
//         if (!over)
//         {
//             int a,b;
//             int count=0;
//             while(!recv_pool1.isEmpty()){
                
                
//                 if(recv_pool1.read(a)==true){
//                     // if(!total_buf.write(a)){
//                     //     break;
//                     // }
//                     sort_q.push(a);
//                 }else{
//                     printf("read recv_pool1 block\n");
//                 }
//                 if(count==RECV1_BLOCKS){
//                     break;
//                 }
//                 /*if app buffer is full, write to file*/
//                 if(sort_q.size()==APP_BLOCKS){
//                     printf("[recv_pool1] queue full, handling...\n");
//                     clear_queue(sort_q);
                    
//                     break;
//                 }
//             }
//             count=0;
//             while(!recv_pool2.isEmpty()){
                
                
//                 if(recv_pool2.read(b)==true){
//                     // if(!total_buf.write(b)){
//                     //     break;
//                     // }
//                     sort_q.push(b);
//                 }else{
//                     printf("read recv_pool2 block\n");
//                 }
//                 if(count==RECV2_BLOCKS){
//                     break;
//                 }
//                 /*if app buffer is full, write to file*/
//                 if(sort_q.size()==APP_BLOCKS){
//                     printf("[recv_pool2] queue full, handling...\n");
//                     clear_queue(sort_q);
//                     break;
//                 }
//             }
//             if(recv1_finish && recv2_finish && recv_pool1.isEmpty() && recv_pool2.isEmpty()){
//                 over=true;
//             }
//         }else{
//             if(!sort_q.empty()){
//                 clear_queue(sort_q);
//             }
//             break;
//         }
   
//     }
// }
void file_timer(){
    while(1){
        std::cout<< "Download \t"<< realtime_file_bytes <<std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}


int  main(){
    /*
    pthread_t    l_1;
    pthread_t    l_2;
    pthread_t    c;
    
    cbuf_init(&cmd);

    pthread_create(&l_1,NULL,producer_1,0);
    pthread_create(&l_2,NULL,producer_2,0);
    pthread_create(&c,NULL,consumer,0);

    pthread_join(l_1,NULL);
    pthread_join(l_2,NULL);
    pthread_join(c,NULL);

    cbuf_destroy(&cmd);
    */

    // pthread_t    ppid_r1;
    // pthread_t    ppid_r2;
    // pthread_t    ppid_app;
    //CHECK_INIT_RETURN(signal_mask());
    CHECK_INIT_RETURN(timer_init());
        //pthread_create(&timer_ppid, NULL, signal_receiver_task, NULL);
    string str = "../resource/"+debug_filename;
    strcpy(infilename, str.c_str());
    outfile.open(infilename, ifstream::out | ios::binary);
    if (!outfile)
    {
        printf("[log] open file error\n");
        return 1;
    }
    //pthread_kill(timer_ppid, SIGINT);
        //init rpkg
    spkg_4G = new UDPPackage(); 
    initUDPPackage(spkg_4G);//4G send
    spkg_retransmission = new UDPPackage(); 
    initUDPPackage(spkg_retransmission);//4G send
    rpkg_4G = new UDPPackage(); 
    initUDPPackage(rpkg_4G);//4G收
    // rpkg_wifi = new UDPPackage();
    // initUDPPackage(rpkg_wifi);//wifi收

    sd_4G = udp_init_4G(NULL);
    sd_wifi = udp_init_wifi(NULL);

    memset(&send_addr, 0, sizeof(send_addr));   
	send_addr.sin_family = AF_INET;   
	send_addr.sin_port = htons(SERVER_PORT);    
	send_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    socklen_t send_len = sizeof(struct sockaddr);
    //request
    spkg_4G->FLAG = FILE_INFO_REQUEST;
    sendto(sd_4G, (char*)spkg_4G, sizeof(*spkg_4G), 0, (struct sockaddr *)&send_addr, send_len);
    //response
    int recvret = -1;
    recvret = recvfrom(sd_4G, (char*)rpkg_4G, sizeof(*rpkg_4G), 0, (struct sockaddr *)&send_addr, &send_len);
    if (recvret < 0){
        printf("[log] client recvfrom fail\n");
        return 0;
    }
    //printf("??");
    if(rpkg_4G->FLAG == FILE_INFO_RESPONSE){
        file_packet_num = rpkg_4G->seq;
        printf("File_packet_num : %d\n", file_packet_num);
    }else{
        printf("cannot recv File_packet_num\n");
        return 0;
    }
       
    //usSleep(10000000);
    //init rpkg_pool
    recv_free_pool1 = new folly::MPMCQueue<udp_packet_q_item_t*>(RECV1_BLOCKS);
    recv_work_pool1 = new folly::MPMCQueue<udp_packet_q_item_t*>(RECV1_BLOCKS);
    recv_buffer_alloc_1 = (struct UDPPackage*) calloc(RECV1_BLOCKS, PACKSIZE);
    recv_free_pool2 = new folly::MPMCQueue<udp_packet_q_item_t*>(RECV2_BLOCKS);
    recv_work_pool2 = new folly::MPMCQueue<udp_packet_q_item_t*>(RECV2_BLOCKS);
    recv_buffer_alloc_2 = (struct UDPPackage*) calloc(RECV2_BLOCKS, PACKSIZE);

    for (int i = 0; i < RECV1_BLOCKS; i++) {
        udp_packet_q_item_t* p = (udp_packet_q_item_t*) calloc(1, sizeof(udp_packet_q_item_t));
        p->buffer = &recv_buffer_alloc_1[i ];//buffer not NULL
        //printf("p->buffer %p\n", p->buffer);
        //initUDPPackage(p->buffer);
        recv_free_pool1->blockingWrite(p);
    }
    for (int i = 0; i < RECV2_BLOCKS; i++) {
        udp_packet_q_item_t* p = (udp_packet_q_item_t*) calloc(1, sizeof(udp_packet_q_item_t));
        p->buffer = &recv_buffer_alloc_2[i ];//buffer not NULL
        //initUDPPackage(p->buffer);
        recv_free_pool2->blockingWrite(p);                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        
    }
    //handler
    std::thread t_handle = std::thread(app_handler_task, nullptr);
    //t_handle.detach();

    for (int i = 0; i < 1; i++) {
        std::thread t = std::thread(udp_worker_4G_loop, nullptr);
        t.detach();
        threads_1.push_back(std::move(t));
    }
    std::thread t1 = std::thread(udp_read_4G_loop, nullptr);
    t1.detach();
    threads_1.push_back(std::move(t1));

    for (int i = 0; i < 1; i++) {
        std::thread t = std::thread(udp_worker_wifi_loop, nullptr);
        t.detach();
        threads_2.push_back(std::move(t));
    }
    std::thread t2 = std::thread(udp_read_wifi_loop, nullptr);
    t2.detach();
    
    std::thread t3 = std::thread(retransmission_task, nullptr);
    t3.detach();
    
    std::thread t4(file_timer);
    t4.detach();

    //std::thread t5 = std::thread(signal_receiver_task, nullptr);

    //t5.detach();
    //receiver init complete
    initUDPPackage(spkg_4G);//4G send
    spkg_4G->FLAG = FILE_INFO_COMPLETE;
    sendto(sd_4G, (char*)spkg_4G, sizeof(*spkg_4G), 0, (struct sockaddr *)&send_addr, send_len); 
    auto start_download = std::chrono::high_resolution_clock::now();

    // once all udp servers initialized
    // io_service.run();
    // pause();

    t_handle.join();
    auto end_download = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_download - start_download);
    cout<< "total time : "<< duration.count() << " ms"<< "\taverage rate : "<< 3212265.0/1048576.0/duration.count()*1000*8<<" Mbps"<<std::endl;
    //send FIN
    initUDPPackage(spkg_4G);//4G send
    spkg_4G->FLAG = FILE_FIN;
    sendto(sd_4G, (char*)spkg_4G, sizeof(*spkg_4G), 0, (struct sockaddr *)&send_addr, send_len); 

    printf("[log] clean system resource...\n");
    outfile.close();
    close(sd_4G);
    close(sd_wifi);
    delete recv_free_pool1;
    delete recv_free_pool2;
    delete recv_work_pool1;
    delete recv_work_pool2;
    free(recv_buffer_alloc_1);
    free(recv_buffer_alloc_2);
    //prepare file
    // outfile.open(debug_filename, ofstream::out | ios::binary | ios::ate);
    // if (!outfile){
    //     printf("[log] open file error, file name:%s\n", debug_filename.c_str());
    //     return -1;
    // }

    //pthread_create(&ppid_r1,NULL,udp_recv_4G,0);
    //pthread_create(&ppid_r2,NULL,udp_recv_wifi,0);
    //pthread_create(&ppid_app,NULL,app_control,0);

    // pthread_join(ppid_r1,NULL);
    // pthread_join(ppid_r2,NULL);
    // pthread_join(ppid_app,NULL);

  
    //outfile.close();
    //print_queue(sort_q);
    return 0;
}


