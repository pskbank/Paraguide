/*************************************************************************
// author       : Park Seongku.
// app          : Bank AP ( daemon )
// updated date : 2013-7-15
// update       : ap의 mac address를 Server에 보내는 부분 추가   2013-06-18
//                mac address를 async방식으로 읽어 오도록 수정   2013-06-30
//                am 06:00에 프로그램 재시작하도록 수정          2013-07-15
// compile option:  gcc -o bankap bankap.c -lbluetooth -lpthread
**************************************************************************/
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <netdb.h>
#include <assert.h>

#include <errno.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <sys/poll.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <pthread.h>
#include <time.h>
#include <net/if.h>       // for get_mac_addr
#include <sys/ioctl.h>    // for get_mac_addr  

#define MAX_RESP   10
#define TRUE       1

#define SA      struct sockaddr
#define MAXLINE 4096
#define MAXSUB  300

#define LISTENQ 1024

extern int h_errno;
char   g_process_mode;
unsigned char sMacAddr[20]={0};
int running;
int debug =0;

void notify(FILE *file, char *message)
{
     time_t timer;
     struct tm now;

     time(&timer);
     localtime_r(&timer,&now);
     fprintf(file, "%04d-%02d-%02d,%02d:%02d:%02d,%s\n", now.tm_year+1900,now.tm_mon+1,now.tm_mday,
         now.tm_hour,now.tm_min,now.tm_sec,message);
     fflush(file);
}

typedef struct _dnode{
 char addr[30];  // mac address
 int  dead_cnt;  // not founded count. 
 time_t access_at;   
 char send_flag;
 long send_cnt;
 struct _dnode *prev;  
 struct _dnode *next;  
} dnode;
dnode *head, *tail;   //전역 변수로 정의
//*********************************************************************
// 연결 리스트 초기화 
//*********************************************************************
void init_dnode(void)  
{
 head=(dnode*)malloc(sizeof(dnode));  //head의 공간 확보
 tail=(dnode*)malloc(sizeof(dnode));  //tail의 공간 확보
 memset(head->addr,0,sizeof(head->addr));
 head->next=tail;
 head->prev=head;
 memset(tail->addr,0,sizeof(tail->addr));
 tail->next=tail;
 tail->prev=head;
}
//*********************************************************************
//mac address key로 연결 리스트에서 찾아내는 함수
//*********************************************************************
dnode *find_dnode(char *addr)  
{
 dnode *s;
 s=head->next;
 // 동일 mac address를 찾거나 tail에 도달하면 끝냄.
 while(strcmp(s->addr,addr)!=0 && s != tail)  
  s=s->next;
 return s;
}
//********************************************************************
//mac address 값을 갖는 노드를 찾아서 삭제
//********************************************************************
int delete_dnode(char *addr)  
{
 dnode *s;  
 s=find_dnode(addr);
 if(s != tail)
 {
  s->prev->next=s->next;
  s->next->prev=s->prev;
  free(s);
  return 1;
 }
 return 0;
}
//********************************************************************
// 맨 뒤의 노드에 삽입
//********************************************************************
dnode *insert_dnode(char *addr)  
{
 dnode *i;

 i=(dnode*)malloc(sizeof(dnode));
 memset(i->addr,0,sizeof(i->addr));
 strcpy(i->addr,addr);
 i->dead_cnt=0;
 time(&(i->access_at));
 i->send_flag=0;
 i->send_cnt=0;
 tail->prev->next=i;
 i->prev=tail->prev;
 tail->prev=i;
 i->next=tail;
 return i;
}

//*********************************************************************
//입력 노드부터 끝까지 화면에 출력하는 함수
//*********************************************************************
void print_dnode(dnode* p) 
{
 printf("\n");
 while(p != tail)
 {
  printf("mac:= %s dead_cnt = %d  access_at=%s send_flag=%d send_cnt=%ld\n", p->addr, p->dead_cnt,ctime(&(p->access_at)),p->send_flag,p->send_cnt);
  p=p->next;
 }
}
//*********************************************************************
//현재의 연결리스트를 모두 삭제
//*********************************************************************
void free_dnode(void)  
{
 dnode *p;
 dnode *s;
 p=head->next;
 while(p != tail)
 {
  s=p;
  p=p->next;
  free(s);
 }
 head->next=tail;
 tail->prev=head;
}
//*********************************************************************
// 조회된 Mac_address로 노드를 정리하면서 노티를 요청
//*********************************************************************
void update_dnode(char *addr)
{
	dnode *s;

	time_t now_time;
    int tt_day,tt_hour,tt_min,tt_sec;
    struct tm *ptime;
    char tbuf[1024]={0};
	
    double d_diff;
                    
    memset(tbuf,0,sizeof(tbuf));
    s=head->next;
    // tail에 도달하면 끝
    while(s != tail){  
        // 시간계산을 위해 아래와 같이 복잡하게 계산
   	    now_time=time(NULL);
        d_diff=difftime(now_time,s->access_at);
        tt_day = d_diff/(double)(60*60*24);
        d_diff -= (double)(tt_day *(double)(60*60*24));
        tt_hour = (int)(d_diff/(double)(60*60));
        d_diff -= (double)(tt_hour *(int)(60*60));
        tt_min = (int)d_diff/(double)(60);
        d_diff -= (double)(tt_min*(int)60);
        tt_sec = (int)d_diff;
        
        ptime =localtime(&now_time);
        strftime(tbuf,1024,"%0H%0M",ptime);
        // founded mac address
  	    if(strcmp(s->addr,addr)==0) {
  		    s->dead_cnt=0;
  	    }
  	    else{
  			s->dead_cnt++;
  	    }
  	    // 1시간 이상 mac address가 검색되지 않거나 dead-cnt가 일정 갯수 이상되면 blue zone에 없는 것으로 간주 삭제
  	    //if ( tt_hout>=1 || s->dead_cnt > 5000)
        if ( tt_min>=10)
        {
            delete_dnode(s->addr);
            notify(stderr,"[ DELETED node]");
            return;
   	    }
        // 오전 06:00에 자동으로 rebooting하도록 설정
        if(strcmp(tbuf,"0600")==0) system("shutdown -r now");

        s=s->next;
    }  // the end of while loop
}
//*************************************************************************************
// Server에 Http구문의 명령을 전달하기 위한 함수 ( ghttp library 대체용 )
//*************************************************************************************
int post_http(int sockfd, char *host, char *page, char *poststr)
{
    char sendline[MAXLINE + 1], recvline[MAXLINE + 1];
    ssize_t n;

    memset(sendline,0,sizeof(sendline));
    memset(recvline,0,sizeof(recvline));

    snprintf(sendline, MAXSUB,
         "POST %s HTTP/1.0\r\n"
         "Host: %s\r\n"
         "Content-type: application/x-www-form-urlencoded\r\n"
         "Content-length: %d\r\n\r\n"
         "%s", page, host, strlen(poststr), poststr);

    write(sockfd, sendline, strlen(sendline));
    while ((n = read(sockfd, recvline, MAXLINE)) > 0) {
        recvline[n] = '\0';
        //printf("%s", recvline);
    }
    if(strstr(recvline,"success") == NULL) // not found
       return 1;

    return 0;
}
//************************************************************************************
//  Program이 daemon으로 동작하게 작업해 주는 함수
//************************************************************************************
void create_daemon()
{
	pid_t pid,sid;  
	  
    pid = fork();
    if(pid < 0) exit(0); // could not fork

    if(pid > 0) {
      notify(stderr,"WhatshowAP Child process created\n");
      exit(0);
    }
    
    umask(027);
   
    sid = setsid();
    if(sid < 0)
    {
      exit(0);
    }
   
    chdir("/");

	close(STDIN_FILENO); 
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

}
//*************************************************************************************
// Thread request_access function  - updated by pskbank  2013-07-17
//*************************************************************************************
void * thread_request_access( dnode *s)
{
    //printf("Thread start  \n");
    int sockfd;
    struct sockaddr_in servaddr;

    char **pptr;
    //********** Soomcloud setting *******
    char hname[30] = "www.soomcloud.com";
    char page[30] = "/apps/access_new_phone";
    char poststr[80] = "bt_ap[mac_address]=";
    if(s==NULL){
        printf("request_access dnode s is null\n");
        return;
    }
    strcat(poststr,sMacAddr);
    strcat(poststr,"&user[bt_mac_address]=");
    strcat(poststr,s->addr);
    //************************************

    char str[80]={0};
    struct hostent *hptr;

    memset(str,0,sizeof(str));
    if ((hptr = gethostbyname(hname)) == NULL) {
        fprintf(stderr, " gethostbyname error for host: %s: %s\n",
            hname, hstrerror(h_errno));
        return;
    }
    if (hptr->h_addrtype == AF_INET
        && (pptr = hptr->h_addr_list) != NULL) {
    /*    printf("address: %s\n",
               inet_ntop(hptr->h_addrtype, *pptr, str,
                 sizeof(str))); */
        inet_ntop(hptr->h_addrtype, *pptr,str,sizeof(str));
    } else {
        fprintf(stderr, "Error call inet_ntop \n");
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    // bzero(&servaddr, sizeof(servaddr));
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(80);
    inet_pton(AF_INET, str, &servaddr.sin_addr);

    if(connect(sockfd, (SA *) & servaddr, sizeof(servaddr))==0){
       s->send_cnt++;
       if(post_http(sockfd, hname, page, poststr)) 
            s->send_flag=0; 
       else 
            s->send_flag=1;
    }       
    close(sockfd);
}

//*************************************************************************************
// Thread request_stay function  - updated by pskbank  2013-06-18
//*************************************************************************************
void * thread_request_stay( dnode *s)
{
    int sockfd;
    struct sockaddr_in servaddr;

    char **pptr;
    //********** Soomcloud setting *******
    char hname[30] = "whatshow.kr";
    char page[30] = "/api/v1/users/stay";
    char poststr[80] = "user[mac_address]=";
    if(s==NULL) {
        printf("request_stay dnode s is null\n");
        return;
    }
    strcat(poststr,s->addr);
    strcat(poststr,"&store[mac_address]=");
    strcat(poststr,sMacAddr);
    //************************************

    char str[80]={0};
    struct hostent *hptr;

    memset(str,0,sizeof(str));
    if ((hptr = gethostbyname(hname)) == NULL) {
        fprintf(stderr, " gethostbyname error for host: %s: %s\n",
            hname, hstrerror(h_errno));
        return;
    }
    //printf("hostname: %s\n", hptr->h_name);
    if (hptr->h_addrtype == AF_INET
        && (pptr = hptr->h_addr_list) != NULL) {
    /*    printf("address: %s\n",
               inet_ntop(hptr->h_addrtype, *pptr, str,
                 sizeof(str))); */
        inet_ntop(hptr->h_addrtype, *pptr,str,sizeof(str));
    } else {
        fprintf(stderr, "Error call inet_ntop \n");
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    // bzero(&servaddr, sizeof(servaddr));
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(80);
    inet_pton(AF_INET, str, &servaddr.sin_addr);

    if(connect(sockfd, (SA *) & servaddr, sizeof(servaddr))==0){ 
       if(post_http(sockfd, hname, page, poststr)) 
          s->send_flag=1; 
       else 
           s->send_flag=2;
    }
    close(sockfd);
}

//***********************************************************************************
// Server에 Notification 요청
//***********************************************************************************
int request_notification(dnode *s)
{
    pthread_t re;
    pthread_attr_t attr;
    void *thread_s;
    
    if( pthread_attr_init(&attr) !=0){
			return 1;
		}
	if( pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED) !=0) {
			return 1;
		}

    if(pthread_create(&re,&attr,(void *)thread_request_access,s)!=0){
			return 1;
		}
	if(pthread_attr_destroy(&attr) !=0){
			return 1;
		}
	return 0;
}
//***********************************************************************************
// Server에 stay 상태임을 보냄
//***********************************************************************************
int send_stay_mode(dnode *s)
{
    pthread_t re;
    pthread_attr_t attr;
    void *thread_s;
    
    if( pthread_attr_init(&attr) !=0){
			return 1;
		}
	if( pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED) !=0) {
			return 1;
		}

    if(pthread_create(&re,&attr,(void *)thread_request_stay,s)!=0){
			return 1;
		}
	if(pthread_attr_destroy(&attr) !=0){
			return 1;
		}
	return 0;
}

//*************************************************************************************
// Server에 Http구문의 명령을 전달하기 위한 함수 
//*************************************************************************************
int get_http(int sockfd, char *host, char *page, char *poststr)
{
    char sendline[MAXLINE + 1], recvline[MAXLINE + 1];
    ssize_t n;

    memset(sendline,0,sizeof(sendline));
    memset(recvline,0,sizeof(recvline));
    snprintf(sendline, MAXSUB,
         "GET %s HTTP/1.0\r\n"
         "Host: %s\r\n"
         "Content-type: application/x-www-form-urlencoded\r\n"
         "Content-length: %d\r\n\r\n"
         "%s", page, host, strlen(poststr), poststr);

    write(sockfd, sendline, strlen(sendline));
    while ((n = read(sockfd, recvline, MAXLINE)) > 0) {
        recvline[n] = '\0';
        //printf("%s", recvline);
    }
    if(strstr(recvline,"success") == NULL)  // not found
       return 1;

    return 0;
}

//*************************************************************************************
// Reset request        
//*************************************************************************************
void reset_request( )
{
    int sockfd;
    struct sockaddr_in servaddr;

    char **pptr;
    //********** Soomcloud setting *******
    char hname[30] = "whatshow.kr";
    char page[30] = "/api/v1/users/reset_devices";
    char poststr[50]={0};
    //************************************
    char str[50]={0};
    struct hostent *hptr;

    memset(str,0,sizeof(str));
    if ((hptr = gethostbyname(hname)) == NULL) {
        fprintf(stderr, " gethostbyname error for host: %s: %s\n",
            hname, hstrerror(h_errno));
        return;
    }
    if (hptr->h_addrtype == AF_INET
        && (pptr = hptr->h_addr_list) != NULL) {
        /*printf("address: %s\n",
               inet_ntop(hptr->h_addrtype, *pptr, str,
                 sizeof(str)));*/ 
        inet_ntop(hptr->h_addrtype, *pptr,str,sizeof(str));
    } 
    else {
        fprintf(stderr, "Error call inet_ntop \n");
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    // bzero(&servaddr, sizeof(servaddr));
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(80);
    inet_pton(AF_INET, str, &servaddr.sin_addr);

    if(connect(sockfd, (SA *) & servaddr, sizeof(servaddr))==0)
       get_http(sockfd, hname, page, poststr); 

    close(sockfd);
}
//*************************************************************************************
//  Get Mac Address function  : 2013-06-18  by pskbank        
//*************************************************************************************
int get_mac_address( char *pIface )
{
	int nSD; // Socket descriptor

	struct ifreq sIfReq; // Interface request

	struct if_nameindex *pIfList; // Ptr to interface name index

	struct if_nameindex *pListSave; // Ptr to interface name index
	
	unsigned char cMacAddr[9]={0};  // MAC address

    memset(cMacAddr,0,sizeof(cMacAddr));

	// Initialize this function

	pIfList = (struct if_nameindex *)NULL;

	pListSave = (struct if_nameindex *)NULL;

	#ifndef SIOCGIFADDR
        // The kernel does not support the required ioctls
	    return 0;
	#endif

	// Create a socket that we can use for all of our ioctls
	nSD = socket( PF_INET, SOCK_STREAM, 0 );
	if ( nSD < 0 )
	{
		// Socket creation failed, this is a fatal error
		printf( "File %s: line %d: Socket failed\n", __FILE__, __LINE__ );
		return 0;
	}
	// Obtain a list of dynamically allocated structures
	pIfList = pListSave = if_nameindex();
	// Walk thru the array returned and query for each interface's
	// address
	for ( pIfList; *(char *)pIfList != 0; pIfList++ )
	{
	    // Determine if we are processing the interface that we
	    // are interested in
	    if ( strcmp(pIfList->if_name, pIface) )
	        // Nope, check the next one in the list
		    continue;
		
	    strncpy( sIfReq.ifr_name, pIfList->if_name, IF_NAMESIZE );
	    // Get the MAC address for this interface
	    if ( ioctl(nSD, SIOCGIFHWADDR, &sIfReq) != 0 )
	    {
	        // We failed to get the MAC address for the interface
		    printf( "File %s: line %d: Ioctl failed\n", __FILE__, __LINE__ );
            close(nSD);
		    return 0;
        }   

	    memmove( (void *)&cMacAddr[0], (void *)&sIfReq.ifr_ifru.ifru_hwaddr.sa_data[0], 6 );
	    break;
	} // the end of for
    memset(sMacAddr,0,sizeof(sMacAddr));
	sprintf(sMacAddr,"%02X:%02X:%02X:%02X:%02X:%02X\0",cMacAddr[0],cMacAddr[1],cMacAddr[2],cMacAddr[3],cMacAddr[4],cMacAddr[5]);
	// Clean up things and return
	if_freenameindex( pListSave );
	close( nSD );
	return 1;
}

int scan_reset_device(int devid)
{
    int ctl;

    ctl = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
    ioctl(ctl, HCIDEVDOWN, devid);
    ioctl(ctl, HCIDEVUP, devid);
    shutdown(ctl, 2);
    close(ctl);
    return hci_open_dev(devid);
}
static void process_result(bdaddr_t *bdaddr, char has_rssi, int rssi,uint8_t *pcls)
{
       char addr[20]={0};
       time_t now_time;
       int tt_day,tt_hour,tt_min,tt_sec;
       struct tm *ptime;	
	
       double d_diff;
       dnode *s;
       char tbuf[1024]={0};
       char print_buf[1048]={0};
       char tmp[20]={0};
       char device_name[20]={0};
       uint8_t cls[4]={0};
       int  device_type1;
       int  device_type2;

       memset(tbuf,0,sizeof(tbuf));
       memset(tmp,0,sizeof(tmp));
       memset(device_name,0,sizeof(device_name));
       memset(print_buf,0,sizeof(print_buf));
       memset(cls,0,sizeof(cls));
       memset(addr,0,sizeof(addr));
       ba2str(bdaddr, addr);
       if(has_rssi)
               sprintf(tmp,"RSSI:%d", rssi);
       else
               sprintf(tmp,"RSSI:n/a");
       memcpy(cls,pcls,3);
       device_type1 = (int)(cls[1] & 0x1f);
       device_type2 = (int)(cls[0] >> 2);
       sprintf(device_name,"unknown");
       switch(device_type1)
       {
            case 1: // computer
                    sprintf(device_name,"computer");
                    break;
            case 2: // phone
                    switch(device_type2)
                    {
                      case 1:
                        sprintf(device_name,"cellular phone");
                        break;
                      case 3:
                        sprintf(device_name,"Smart phone");
                        break;
                      default:
                        sprintf(device_name,"General phone");
                        break;
                    }
                    break;
            case 3: // lan access
                    sprintf(device_name,"lan_access");
                    break;
            case 4: // audio/video
                    sprintf(device_name,"audio-video");
                    break;
            case 8: // toy
                    sprintf(device_name,"toy");
                    break;
            default:
                    sprintf(device_name,"unknown");
                    break;
        }
       sprintf(print_buf,"mac:%17s %s dev_type: %s",addr,tmp,device_name);
       notify(stdout,print_buf);

       if(!(device_type1 == 2 && device_type2==3)) // 스마트폰이 아니면 pass
          return;

       // search in linked list 
       s = find_dnode(addr);
       if(s != tail) // Mac address table에 이미 등록된 Mac address이면
       {
		    now_time=time(NULL);
            d_diff=difftime(now_time,s->access_at);
            tt_day = d_diff/(double)(60*60*24);
            d_diff -= (double)(tt_day *(double)(60*60*24));
            tt_hour = (int)(d_diff/(double)(60*60));
            d_diff -= (double)(tt_hour *(int)(60*60));
            tt_min = (int)d_diff/(double)(60);
            d_diff -= (double)(tt_min*(int)60);
            tt_sec = (int)d_diff;
            s->dead_cnt=0;
		    // 성공적으로 Noti를 요청한 후 3분이 경과되면 서버에 고객이 store에 머물고 있다고 알림.  2013-6-17  by pskbank
            /*
            if(s->send_flag==1 && tt_min==3 ) 
            {
                 printf("send stay_mode start\n");
                 send_stay_mode(s);
                 if(s->send_flag==1) 
                 	  strcpy(tbuf,"Send Success");
                 else 
                 	  strcpy(tbuf,"Send Failed");
                 sprintf(print_buf,"[SEND_STAY_REQUEST===================> mac : %s ret_msg: %s\n",addr,tbuf);
                 notify(stdout,print_buf);
            }
            */
            // 성공적으로 Server에서 Receive가 되지 않았으면 서버에 요청. reset mode 로 실행시도 계속 보냄
            if(g_process_mode>2 || (s->send_flag==0 && s->send_cnt<5) )
            {
                 s->access_at=time(NULL); 
                 request_notification(s);
                 if(s->send_flag==1) 
                 	  strcpy(tbuf,"Send Success");
                              else strcpy(tbuf,"Send Failed");
 
                    sprintf(print_buf,"[RE request_notification]===========> mac : %s ret_msg: %s\n",addr,tbuf);
                    notify(stdout,print_buf); 
            }
       }
       else   // Mac address table에 없다면  처음으로 서버에 노티를 요청
       {
  		       // 맨 뒤에 새 노드를 삽입
             s=insert_dnode(addr); 
             s->access_at=time(NULL);
             // 서버에 Notification 요청
             request_notification(s);
             if(s->send_flag==1) 
             	   strcpy(tbuf,"Send Success");
             else 
             	   strcpy(tbuf,"Send Failed");
  		     sprintf(print_buf,"[First request_notification]============> mac : %s ret_msg: %s\n",addr,tbuf);
             notify(stdout,print_buf);
       }
       update_dnode(addr);
}
static int write_inquiry_mode(int dev_id, int sock)
{
       struct hci_filter flt;
       write_inquiry_mode_cp cp;

       hci_filter_clear(&flt);
       hci_filter_set_ptype(HCI_EVENT_PKT, &flt);
       hci_filter_set_event(EVT_INQUIRY_RESULT, &flt);
       hci_filter_set_event(EVT_INQUIRY_RESULT_WITH_RSSI, &flt);
       hci_filter_set_event(EVT_INQUIRY_COMPLETE, &flt);
       if (setsockopt(sock, SOL_HCI, HCI_FILTER, &flt, sizeof(flt)) < 0) {
               perror("Can't set HCI filter");
               return 1;
       }
       cp.mode = 0x01;
       if (hci_send_cmd(sock, OGF_HOST_CTL, OCF_WRITE_INQUIRY_MODE,WRITE_INQUIRY_MODE_RP_SIZE, &cp) < 0) 
       {
             perror("Can't set write inquiry mode");
             return 1;
       }
}
static int read_inquiry_mode(int sock)
{
       inquiry_cp cp;
       
       memset (&cp, 0, sizeof(cp));
       cp.lap[2] = 0x9e;
       cp.lap[1] = 0x8b;
       cp.lap[0] = 0x33;
       cp.num_rsp = 0;
       cp.length = 0x30;
       if (hci_send_cmd (sock, OGF_LINK_CTL, OCF_INQUIRY, INQUIRY_CP_SIZE,&cp) < 0) 
       {
               perror("Can't start inquiry");
               return 1;
       }
       return 0;
}
static int inquiry_with_rssi(int sock)
{
       unsigned char buf[HCI_MAX_EVENT_SIZE+1], *ptr;
       hci_event_hdr *hdr;
       char canceled = 0;
       inquiry_info_with_rssi *info_rssi;
       inquiry_info *info;
       int results, i, len;
       struct pollfd p;
       time_t now_time;
       struct tm *ptime;
       char tbuf[30]={0};
	
       memset(buf,0,sizeof(buf));
       if(read_inquiry_mode(sock))
         return;
       
       p.fd = sock;
       p.events = POLLIN | POLLERR | POLLHUP;
       printf("Starting inquiry with RSSI...\n");
       while(running) {
               p.revents = 0;
               /* poll the BT device for an event */
               if (poll(&p, 1, -1) > 0) {
                       len = read(sock, buf, sizeof(buf));
                       if (len < 0)
                               continue;
                       else if (len == 0)
                               break;
                       hdr = (void *) (buf + 1);
                       ptr = buf + (1 + HCI_EVENT_HDR_SIZE);
                       results = (int)ptr[0];
                       switch (hdr->evt) {
                               case EVT_INQUIRY_RESULT:
                                        for (i = 0; i < results; i++) {
                                               info = (void *)ptr +(sizeof(*info) * i) + 1;
                                               process_result(&info->bdaddr,0, 0,info_rssi->dev_class);
                                       }
                                       break;
                               case EVT_INQUIRY_RESULT_WITH_RSSI:
                                       for (i = 0; i < results; i++) {
                                               info_rssi = (void *)ptr +(sizeof(*info_rssi) * i) + 1;
                                               process_result(&info_rssi->bdaddr, 1, info_rssi->rssi,info_rssi->dev_class);
                                       }
                                       break;
                               case EVT_INQUIRY_COMPLETE:
                                       read_inquiry_mode(sock);
                                       break;
                       }
               }
	
  	    now_time=time(NULL);
       
        ptime =localtime(&now_time);
        strftime(tbuf,1024,"%0H%0M",ptime);
 
        // 오전 06:00에 자동으로 rebooting하도록 설정
        if(strcmp(tbuf,"0600")==0) system("shutdown -r now");


       }// the end of while
  
}

static void cancel_inquiry(int sock)
{
  int err;

  err = hci_send_cmd(sock,OGF_LINK_CTL, OCF_INQUIRY_CANCEL,0,NULL);
  if(debug) printf("cancel_inquiry: err=%d\n",err);
}

static void scanner_start()
{
       int dev_id, sock = 0;
       char buf[80]={0};

       memset(buf,0,sizeof(buf));
       dev_id = hci_get_route(NULL);
       sock = hci_open_dev( dev_id );
       if (dev_id < 0 || sock < 0) {
               
               sprintf(buf,"Can't open bluetooth socket");
               notify(stderr,buf);
               system("sudo /etc/init.d/bluetooth restart");
               goto end_rtn;
       }
 
       if(write_inquiry_mode(dev_id,sock))
          goto end_rtn;

       // start daemon mode
       if(g_process_mode == 1)
           create_daemon();
 
       inquiry_with_rssi(sock); 
end_rtn:
       cancel_inquiry(sock);
       if(hci_close_dev(sock)<0) 
       {
         perror("Can't close HCI device");
         exit(1);
       }
}
void leave(int sig) 
{
    notify(stderr,"Treminating...");
    running=0;
    free_dnode();
}
int main(int argc, char **argv)
{
    char ch;
    int  retry_cnt=0;

    running = 1;

    (void) signal(SIGINT,leave);

    if(argc<2) 
    {
       printf("usage: <program name> d | t \n");
       return 0;
    }

    get_mac_address("eth0");
    printf("My Mac address = %s\n",sMacAddr);
    
    ch = argv[1][0];

    switch(ch)
    {
       case  'd':
                   printf("BT daemon start\n");
                   g_process_mode=1;
                   break;
       case  't':
                   g_process_mode=2;
                   printf("BT Testmode start\n");
                   break;
       case  'r':
                   g_process_mode=3;
                   printf("BT reset start\n");
                   // reset_request();
       default:
                   printf("For daemon whatshowap d , For test whatshowap t\n");
                   return 0;
    }

    init_dnode();
    while(running)
    {
        scanner_start();
        retry_cnt++;
        if(g_process_mode>1) printf("error  retry cnt =  %d\n",retry_cnt);

        sleep(10);
        
        if(retry_cnt > 5)
            system("shutdown -r now");
    }
    return 0;
}
