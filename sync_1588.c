#include <time.h>

#include <linux/if_packet.h>
#include <linux/if_ether.h>

#include<sys/socket.h>
#include<sys/ioctl.h>
#include<net/if.h>
#include<net/ethernet.h>
#include<arpa/inet.h>

#include<string.h>
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<stdint.h>

#define GPTP_ETHERTYPE 		0x88F7

#define GPT_FLAG_TWOSTEP 	0x0200
#define GPT_FLAG_UNICAST	0x0400
#define GPT_FLAG_TIMETRACABLE   0x0008


static const uint8_t broadcast_addr[ETH_ALEN] = { 0x00, 0x15, 0x5d, 0x73, 0xfa, 0x3b };

/** Acc. to IEEE 1588 Table 18.
 */
struct ptp_header
{
	uint8_t messageType;		/* 1 , 0 */
	uint8_t versionPTP;		/* 1 , 1 */
	uint16_t messageLength;		/* 2 , 2 */
	uint8_t domainNumber;		/* 1 , 4 */
	uint8_t reserved_1;		/**/
	uint16_t flagField;		/**/
	uint64_t correctionField;	/**/
	uint32_t reserved_2;		/**/
	uint8_t sourcePortIdentity[10];	/**/
	uint16_t seqenceId;		/**/
	uint8_t controlField;		/**/
	uint8_t logMessageInterval;	/**/
};


struct ptp_sync
{
	uint64_t sec;
	uint32_t nsec;
};


void ptp_header_init(struct ptp_header *hdr)
{
	memset(hdr, 0, sizeof(*hdr));
	hdr->messageType = 0x10;
	hdr->versionPTP = 2;
	hdr->flagField = GPT_FLAG_TWOSTEP | GPT_FLAG_UNICAST;
	hdr->messageLength = 44;
}


void ptp_sync_init(struct ptp_sync *msg)
{
	memset(msg, 0, sizeof(*msg));
}


int ptp_header_ton(uint8_t *dest, const struct ptp_header *src)
{
	dest[0]  		 = 	  (src->messageType);
	dest[1]			 = 	  (src->versionPTP);
	*(uint16_t *)(&dest[2])  = htobe16(src->messageLength);
	dest[4]  		 =	  (src->domainNumber);
	*(uint16_t *)(&dest[6])  = htobe16(src->flagField);
	*(uint64_t *)(&dest[8])  = htobe64(src->correctionField);

	memcpy(&dest[9], src->sourcePortIdentity, 10);

	*(uint16_t *)(&dest[30]) = htobe16(src->seqenceId);
	dest[32] 		 = 	  (src->controlField);
	dest[33] 		 = 	  (src->logMessageInterval);

	return 34;
}

int ptp_sync_ton(uint8_t *dest, const struct ptp_sync *src)
{
	*(uint16_t *)(&dest[0])  = htobe16((src->sec >> 32) & 0xffff);
	*(uint32_t *)(&dest[2])  = htobe32(src->sec & 0xffffffff);
	*(uint32_t *)(&dest[6])  = htobe32(src->nsec);

	return 10;
}


void ptp_debug_hex(const uint8_t *src, int n)
{
	for (int i = 0; i < n; i++)
	{
		printf("%02x ", src[i]);
	}
	printf("\n");
}


int main(int arg, char **argv)
{
	int sock = -1;
	uint8_t if_addr[ETH_ALEN];
	uint8_t dest_addr[ETH_ALEN];
	char buf[256] = "Hello World";
	int if_index;

	sock = socket(PF_PACKET, SOCK_RAW, htons(GPTP_ETHERTYPE));
	if (sock <= 0) 
	{
		perror("socket()");
		exit(1);
	}
	
	printf("socket open: %d\n", sock);
		
	
	struct ifreq ifr;
	memset(&ifr, 0, sizeof (ifr));
	strncpy(ifr.ifr_name, "eth0", IFNAMSIZ - 1);
	memcpy(dest_addr, broadcast_addr, ETH_ALEN);

	if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0)
	{
		perror ("SIOCGIFINDEX");
	}
	if_index = ifr.ifr_ifindex;

	if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0)
	{
		perror("SIOCGIFHWADDR");
		memcpy(if_addr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
	}


	struct ether_header *eh;

	/* Ethernet header */
	eh = (struct ether_header *) buf;
	memcpy(eh->ether_shost, if_addr, ETH_ALEN);
	memcpy(eh->ether_dhost, dest_addr, ETH_ALEN);
	eh->ether_type = htons(GPTP_ETHERTYPE);

	int send_len = sizeof (*eh);

	struct sockaddr_ll sock_addr;
	sock_addr.sll_ifindex = if_index;
	sock_addr.sll_halen = ETH_ALEN;
	memcpy(sock_addr.sll_addr, dest_addr, ETH_ALEN);

	struct ptp_header ptp_hdr;
	struct ptp_sync ptp_sync;

	ptp_header_init(&ptp_hdr);
	send_len += ptp_header_ton(buf + send_len, &ptp_hdr);
	ptp_sync_init(&ptp_sync);
	ptp_sync.sec = 1;
	ptp_sync.nsec = 2;
	send_len += ptp_sync_ton(buf + send_len, &ptp_sync);
	
	ptp_debug_hex(buf, sizeof (*eh) + send_len);

	printf("Sending %d bytes...\n", send_len);
	if (sendto(sock, buf, send_len, 0,
		  (struct sockaddr *) &sock_addr, sizeof (sock_addr)) < 0)
	{
		perror ("sendto()");
	}

	close(sock);
	printf("socket closed.\n");
	
	return 0;
}

