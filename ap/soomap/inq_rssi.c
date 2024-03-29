#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
static void print_result(bdaddr_t *bdaddr, char has_rssi, int rssi)
{
       char addr[18];
       ba2str(bdaddr, addr);
       printf("%17s", addr);
       if(has_rssi)
               printf(" RSSI:%d", rssi);
       else
               printf(" RSSI:n/a");
       printf("\n");
       //fflush(NULL);
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
       memset(&cp, 0, sizeof(cp));
       cp.mode = 0x01;
       //cp.mode = 0x00;
       
       if (hci_send_cmd(sock, OGF_HOST_CTL, OCF_WRITE_INQUIRY_MODE,WRITE_INQUIRY_MODE_RP_SIZE, &cp) < 0) 
       {
             perror("Can't set write inquiry mode");
             return 1;
       }
       return 0;
}
static int read_inquiry_mode(int sock)
{
       inquiry_cp cp;
       
       memset (&cp, 0, sizeof(cp));
       cp.lap[2] = 0x9e;
       cp.lap[1] = 0x8b;
       cp.lap[0] = 0x33;
       cp.num_rsp = 0;
       //cp.length = 0x30;
       cp.length = 0x03;
       if (hci_send_cmd (sock, OGF_LINK_CTL, OCF_INQUIRY, INQUIRY_CP_SIZE,&cp) < 0) 
       {
               perror("Can't start inquiry");
               return 1;
       }
       return 0;
}
static char *get_minor_device_name(int major, int minor)
{
       switch (major)
       {
         case  0:
           return " ";
         case  1:
           return "computer";
         case  2: 
           return "phone";
         case  3:
           return "Lan access";
         case  4:
           return "audio/video";
         case  8:
           return "toy";
         default:
           return "unknown";
       }

}

static int inquiry_with_rssi(int sock)
{
       unsigned char buf[HCI_MAX_EVENT_SIZE], *ptr;
       hci_event_hdr *hdr;
       char canceled = 0;
       inquiry_info_with_rssi *info_rssi;
       inquiry_info *info;
       int results, i, len;
       struct pollfd p;
       uint8_t cls[3];
       
       if(read_inquiry_mode(sock))
         return;
       
       p.fd = sock;
       p.events = POLLIN | POLLERR | POLLHUP;
       printf("Starting inquiry with RSSI...\n");
       while(1) {
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
                       results = ptr[0];
                       switch (hdr->evt) {
                               case EVT_INQUIRY_RESULT:
                                        for (i = 0; i < results; i++) {
                                               info = (void *)ptr +(sizeof(*info) * i) + 1;
                                               print_result(&info->bdaddr,0, 0);
                                       }
                                       break;
                               case EVT_INQUIRY_RESULT_WITH_RSSI:
                                       for (i = 0; i < results; i++) {
                                               info_rssi = (void *)ptr +(sizeof(*info_rssi) * i) + 1;
                                               memcpy(cls, info_rssi->dev_class,3);
                                               printf("device_name = %s\n",get_minor_device_name(cls[1] & 0x1f, cls[0] >> 2));
                                               print_result(&info_rssi->bdaddr, 1, info_rssi->rssi);
                                       }
                                       break;
                               case EVT_INQUIRY_COMPLETE:
                                       printf("completed\n");
                                       if(read_inquiry_mode(sock)) return;
                                       break;
                               default :
                                       printf("default\n");
                                       break;
                       }
               }
       }
  
}
static void scanner_start()
{
       int dev_id, sock = 0;

       dev_id = hci_get_route(NULL);
       sock = hci_open_dev( dev_id );
       if (dev_id < 0 || sock < 0) {
               perror("Can't open socket");
               return;
       }
       if(write_inquiry_mode(dev_id,sock))
          return;
       if(inquiry_with_rssi(sock)) 
          return;
       close(sock);
}
int main(int argc, char **argv)
{
       //int i; /* causes inq. result to have no rssi value */
       scanner_start();
       return 0;
}
