/**********************************************************************
 * file:  sr_router.c
 * date:  Mon Feb 18 12:50:42 PST 2002
 * Contact: casado@stanford.edu
 *
 * Description:
 *
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <stdio.h>
#include <assert.h>
#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"
#include "sr_arpcache.h"
#include "sr_utils.h"

/* Define some field value */

/* --------- IP fields ------------ */
#define DEFAULT_TTL (64)
#define DEFAULT_TOS (0)
#define IPV4_VERSION (4)
#define IPV6_VERSION (6)
#define DEFAULT_IP_HL (5)

/* ---------- ICMP fields ------------ */
#define ICMP_NET_UNREACHABLE_TYPE (3)
#define ICMP_NET_UNREACHABLE_CODE (0)

#define ICMP_HOST_UNREACHABLE_TYPE (3)
#define ICMP_HOST_UNREACHABLE_CODE (1)

#define ICMP_PORT_UNREACHABLE_TYPE (3)
#define ICMP_PORT_UNREACHABLE_CODE (3)

#define ICMP_TIME_EXCEEDED_TYPE (11)
#define ICMP_TIME_EXCEEDED_CODE (0)

#define ICMP_ECHO_REPLY_TYPE (0)
#define ICMP_ECHO_REPLY_CODE (0)

uint8_t broadcast_ether_addr[ETHER_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // similar to unsigned char

/* -----------------Declaration of Static Function---------------------*/
static sr_icmp_t3_hdr_t * create_icmp_type3_header(void *icmp_data, int TypeCode_icmp);
static sr_icmp_hdr_t *create_icmp_header(uint8_t *icmp_data, uint16_t icmp_data_len);
static sr_ip_hdr_t *create_ip_header(uint16_t ip_id,  uint32_t dst_ip, uint32_t src_ip,
                                    int TypeCode_icmp, uint16_t icmp_data_len);
static sr_ethernet_hdr_t *create_ethernet_header(uint8_t *dest_host_addr, uint8_t *src_host_addr, uint16_t ether_type);
static sr_arp_hdr_t *create_arp_header(unsigned char *dst_hw_addr, unsigned char *src_hw_addr, 
                                        uint32_t dst_ip, uint32_t src_ip, unsigned short arp_opcode);

/*---------------------------------------------------------------------
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 *
 *---------------------------------------------------------------------*/

void sr_init(struct sr_instance* sr)
{
    /* REQUIRES */
    assert(sr);

    /* Initialize cache and cache cleanup thread */
    sr_arpcache_init(&(sr->cache));

    pthread_attr_init(&(sr->attr));
    pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_t thread;

    pthread_create(&thread, &(sr->attr), sr_arpcache_timeout, sr);
    
    /* Add initialization code here! */

} /* -- sr_init -- */

/* -------------- CREATION FUNCTION: remember free after using ------------------- */
static sr_icmp_t3_hdr_t * create_icmp_type3_header(void *icmp_data, int TypeCode_icmp)
{
    /* create icmp header (type 3) */
    sr_icmp_t3_hdr_t *icmp_hdr = (struct sr_icmp_t3_hdr *)calloc(1, sizeof(struct sr_icmp_t3_hdr));
    switch(TypeCode_icmp)
    {
        case NET_UNREACHABLE:
                                fprintf(stderr,"Destionation net unreachable!\n");
                                icmp_hdr->icmp_type = ICMP_NET_UNREACHABLE_TYPE;// 1 byte: no need to use ntohs
                                icmp_hdr->icmp_code = ICMP_NET_UNREACHABLE_CODE;
                                break;
        case HOST_UNREACHABLE:
                                fprintf(stderr,"Destionation host unreachable!\n");
                                icmp_hdr->icmp_type = ICMP_HOST_UNREACHABLE_TYPE;// 1 byte: no need to use ntohs
                                icmp_hdr->icmp_code = ICMP_HOST_UNREACHABLE_CODE;
                                break;
        case PORT_UNREACHABLE:
                                fprintf(stderr,"Destionation port unreachable!\n");
                                icmp_hdr->icmp_type = ICMP_PORT_UNREACHABLE_TYPE;// 1 byte: no need to use ntohs
                                icmp_hdr->icmp_code = ICMP_PORT_UNREACHABLE_CODE;
                                break;
        case TIME_EXCEEDED:
                                fprintf(stderr,"Time exceeded!\n");
                                icmp_hdr->icmp_type = ICMP_TIME_EXCEEDED_TYPE;// 1 byte: no need to use ntohs
                                icmp_hdr->icmp_code = ICMP_TIME_EXCEEDED_CODE;
                                break;
    }
    memcpy(icmp_hdr->data,(uint8_t *)icmp_data,ICMP_DATA_SIZE); /* copy data from IP header (including 8 byte data icmp)*/
    icmp_hdr->icmp_sum = 0;
    icmp_hdr->icmp_sum = cksum(icmp_hdr,sizeof(struct sr_icmp_t3_hdr));
    return icmp_hdr;
}/* -- create ICMP header (error ICMP) --*/

static sr_icmp_hdr_t *create_icmp_header(uint8_t *icmp_data, uint16_t icmp_data_len)
{
    /* create icmp header (type 0: reply) */
    sr_icmp_hdr_t *icmp_hdr = (sr_icmp_hdr_t *)calloc(1, sizeof(sr_icmp_hdr_t) + icmp_data_len);
    icmp_hdr->icmp_type = ICMP_ECHO_REPLY_TYPE;// 1 byte: no need to use ntohs
    icmp_hdr->icmp_code = ICMP_ECHO_REPLY_CODE;
    memcpy(icmp_hdr + sizeof(sr_icmp_hdr_t), icmp_data, icmp_data_len);
    icmp_hdr->icmp_sum =0;
    icmp_hdr->icmp_sum = cksum(icmp_hdr,sizeof(struct sr_icmp_hdr) + icmp_data_len);
    return icmp_hdr;
}/* -- create ICMP header (error ICMP) --*/

static sr_ip_hdr_t *create_ip_header(uint16_t ip_id,  uint32_t dst_ip, uint32_t src_ip,
                                     int TypeCode_icmp, uint16_t icmp_data_len)
{
    sr_ip_hdr_t *ip_icmp_hdr = (sr_ip_hdr_t *)calloc(1, sizeof(sr_ip_hdr_t));
    ip_icmp_hdr->ip_hl = DEFAULT_IP_HL;
    ip_icmp_hdr->ip_v = IPV4_VERSION;
    ip_icmp_hdr->ip_tos = DEFAULT_TOS;
    if(TypeCode_icmp != ECHO_REPLY)
        ip_icmp_hdr->ip_len = (uint16_t)(htons(sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_t3_hdr_t)));
    else
        ip_icmp_hdr->ip_len = (uint16_t)(htons(sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t)+ icmp_data_len));// send echo reply
    ip_icmp_hdr->ip_id = (uint16_t)(htons(ip_id)); /*plus 1: research more */
    ip_icmp_hdr->ip_off = (uint16_t)(htons(IP_DF));
    ip_icmp_hdr->ip_ttl = DEFAULT_TTL;
    ip_icmp_hdr->ip_p = ip_protocol_icmp;
    ip_icmp_hdr->ip_src = (uint32_t)htonl(src_ip);

    #ifdef DEBUG_PRINT
    fprintf(stderr, "Sending ICMP packet to ");
	print_addr_ip_int(dst_ip); /*dst_ip : host byte ordered */
    #endif

    ip_icmp_hdr->ip_dst = (uint32_t)htonl(dst_ip); 
    ip_icmp_hdr->ip_sum = 0;
    ip_icmp_hdr->ip_sum = cksum(ip_icmp_hdr, sizeof(sr_ip_hdr_t));
    return ip_icmp_hdr;
}/* -- create IP header --*/

static sr_ethernet_hdr_t *create_ethernet_header(uint8_t *dest_host_addr, uint8_t *src_host_addr, uint16_t ether_type)
{
    sr_ethernet_hdr_t *ether_icmp_hdr = (sr_ethernet_hdr_t *)calloc(1, sizeof(sr_ethernet_hdr_t));
    memcpy(ether_icmp_hdr->ether_dhost, dest_host_addr, ETHER_ADDR_LEN);
    memcpy(ether_icmp_hdr->ether_shost, src_host_addr, ETHER_ADDR_LEN);
    if(ether_type == ethertype_ip)
        ether_icmp_hdr->ether_type = htons(ethertype_ip);
    else
        ether_icmp_hdr->ether_type = htons(ethertype_arp);
    return ether_icmp_hdr;
}/* -- create ethernet header --*/

static sr_arp_hdr_t *create_arp_header(unsigned char *dst_hw_addr, unsigned char *src_hw_addr, uint32_t dst_ip, uint32_t src_ip, unsigned short arp_opcode)
{
    sr_arp_hdr_t *arp_hdr = (sr_arp_hdr_t *)calloc(1, sizeof(sr_arp_hdr_t));
    arp_hdr->ar_hrd = htons(arp_hrd_ethernet);
    arp_hdr->ar_pro = htons(arp_pro_addr);
    arp_hdr->ar_hln = ETHER_ADDR_LEN;
    arp_hdr->ar_pln = sizeof(uint32_t); //4 byte: IP
    if(arp_opcode == arp_op_request)
        arp_hdr->ar_op = htons(arp_op_request);
    else if (arp_opcode == arp_op_reply)
        arp_hdr->ar_op = htons(arp_op_reply);
    memcpy(arp_hdr->ar_sha , src_hw_addr, ETHER_ADDR_LEN);
    arp_hdr->ar_sip = htonl(src_ip);
    memcpy(arp_hdr->ar_tha , dst_hw_addr, ETHER_ADDR_LEN);
    arp_hdr->ar_tip = htonl(dst_ip);
    return arp_hdr;
}

int send_icmp_error_notify(struct sr_instance *sr, sr_ethernet_hdr_t *ether_hdr, sr_ip_hdr_t *ip_hdr, 
                            uint32_t dst_ip, int TypeCode_icmp)
{
    int ret;

    /* Getting routing entry and some info related to this interface (source IP, MAC address) */
    sr_rt_tt *routing_entry = NULL;
    routing_entry = sr_longest_prefix_match(sr, htonl(dst_ip));//dst_ip = source ip of icmp request (doesnt like ip in arp request)
    struct sr_if *interface = NULL;
    interface = sr_get_interface(sr, routing_entry->interface);

    /* create icmp header first */
    sr_icmp_t3_hdr_t *icmp_hdr = create_icmp_type3_header(ip_hdr,TypeCode_icmp);// include data

    /* create IP header */
    sr_ip_hdr_t *ip_icmp_hdr = create_ip_header(ntohs(ip_hdr->ip_id) , dst_ip, ntohl(interface->ip), TypeCode_icmp, 0); // no datalen +1

    /* create Ethernet header */
    sr_ethernet_hdr_t *ether_icmp_hdr = create_ethernet_header(ether_hdr->ether_shost, interface->addr, ethertype_ip);

    /* create packet and send*/
    unsigned int buflen = sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_t3_hdr_t);
    uint8_t *buf = (uint8_t *)calloc(1, buflen);
    memcpy(buf, ether_icmp_hdr, sizeof(sr_ethernet_hdr_t));
    memcpy(buf + sizeof(sr_ethernet_hdr_t), ip_icmp_hdr, sizeof(sr_ip_hdr_t));
    memcpy(buf + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t), icmp_hdr, sizeof(sr_icmp_t3_hdr_t));

    #ifdef DEBUG_PRINT
    print_hdrs(buf, buflen);
    #endif

    ret = sr_send_packet(sr, buf, buflen, interface->name);

    /*free memory allocated */
    free(buf);
    free(ether_icmp_hdr);
    free(ip_icmp_hdr);
    free(icmp_hdr);
    return ret;
}

int send_icmp_reply(struct sr_instance *sr, char *iface, uint16_t ip_id, 
                    uint8_t *ether_dhost, uint8_t *ether_shost, 
                    uint32_t dst_ip, uint32_t src_ip, uint8_t *icmp_data, 
                    uint16_t icmp_data_len, int TypeCode_icmp)
{
    int ret;
    /* create icmp header first */
    sr_icmp_hdr_t *icmp_hdr = create_icmp_header(icmp_data, icmp_data_len);// including data

    /* create IP header */
    sr_ip_hdr_t *ip_icmp_hdr = create_ip_header(ip_id, dst_ip, src_ip, TypeCode_icmp, icmp_data_len);

    /* create Ethernet header */
    sr_ethernet_hdr_t *ether_icmp_hdr = create_ethernet_header(ether_dhost, ether_shost, ethertype_ip);

    /* create packet and send*/
    unsigned int buflen = sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t) + icmp_data_len;
    uint8_t *buf = (uint8_t *)calloc(1, buflen);
    memcpy(buf, ether_icmp_hdr, sizeof(sr_ethernet_hdr_t));
    memcpy(buf + sizeof(sr_ethernet_hdr_t), ip_icmp_hdr, sizeof(sr_ip_hdr_t));
    memcpy(buf + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t), icmp_hdr, sizeof(sr_icmp_hdr_t) + icmp_data_len);  

    #ifdef DEBUG_PRINT
    print_hdrs(buf, buflen);
    #endif

    ret = sr_send_packet(sr, buf, buflen, iface);

    /*free memory allocated */
    free(buf);
    free(ether_icmp_hdr);
    free(ip_icmp_hdr);
    free(icmp_hdr);
    return ret;
}

int send_arp_request(struct sr_instance *sr, struct sr_arpreq *req)
{
    int ret;

    uint32_t dst_ip = req->ip; /* finding MAC address refered to destination IP address */

    #ifdef DEBUG_PRINT
    printf("destination IP address:\n");
    print_addr_ip_int(ntohl(dst_ip));
    #endif

    /* Getting routing entry and some info related to this interface (source IP, MAC address) */
    sr_rt_tt *routing_entry = NULL;
    routing_entry = sr_longest_prefix_match(sr, dst_ip);//dst_ip = source ip of icmp request (doesnt like ip in arp request)
    struct sr_if *interface = NULL;
    interface = sr_get_interface(sr, routing_entry->interface);
    char *iface = interface->name;

    /* Create ARP request header */
    sr_arp_hdr_t *arp_hdr = create_arp_header(broadcast_ether_addr, interface->addr, ntohl(dst_ip), ntohl(interface->ip), arp_op_request);

    /* Create ARP reply header */
    sr_ethernet_hdr_t *ether_hdr = create_ethernet_header(broadcast_ether_addr, interface->addr, ethertype_arp);

    /* Create buf for packet and send it */
    unsigned int buflen = sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t);
    uint8_t *buf = (uint8_t *)calloc(1, buflen);
    memcpy(buf, ether_hdr, sizeof(sr_ethernet_hdr_t));
    memcpy(buf + sizeof(sr_ethernet_hdr_t), arp_hdr, sizeof(sr_arp_hdr_t));

    #ifdef DEBUG_PRINT
    print_hdrs(buf, buflen);
    #endif

    ret = sr_send_packet(sr, buf, buflen, iface);

    /*free memory */
    free(buf);
    free(ether_hdr);
    free(arp_hdr);

    return ret;
}

int send_arp_reply(struct sr_instance *sr, uint8_t *dst_etheraddr, uint8_t *src_etheraddr, uint32_t dst_ip, uint32_t src_ip, char *iface)
{
    int ret;

    /* Create ARP request header */
    sr_arp_hdr_t *arp_hdr = create_arp_header(dst_etheraddr, src_etheraddr,  dst_ip, src_ip, arp_op_reply);

    /* Create ARP reply header */
    sr_ethernet_hdr_t *ether_hdr = create_ethernet_header(dst_etheraddr, src_etheraddr, ethertype_arp);

    /* Create buf for packet and send it */
    unsigned int buflen = sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t);
    uint8_t *buf = (uint8_t *)calloc(1, buflen);
    memcpy(buf, ether_hdr, sizeof(sr_ethernet_hdr_t));
    memcpy(buf + sizeof(sr_ethernet_hdr_t), arp_hdr, sizeof(sr_arp_hdr_t));

    #ifdef DEBUG_PRINT
    print_hdrs(buf, buflen);
    #endif

    ret = sr_send_packet(sr, buf, buflen, iface);

    /*free memory */
    free(buf);
    free(ether_hdr);
    free(arp_hdr);

    return ret;
}
/*
 *   Pseudocode
  # When sending packet to next_hop_ip
   entry = arpcache_lookup(next_hop_ip)
   if entry:
       use next_hop_ip->mac mapping in entry to send the packet
       free entry
   else:
       req = arpcache_queuereq(next_hop_ip, packet, len)
       handle_arpreq(req)
*/
int forward_packet(struct sr_instance *sr, uint8_t* packet, uint32_t len, uint32_t dst_ip)
{
    int ret;
    /* make a copy */
    uint8_t *copy_buf = malloc(len);
	memcpy(copy_buf, packet, len);
	sr_ethernet_hdr_t *eth_hdr = (sr_ethernet_hdr_t *)copy_buf;
	sr_ip_hdr_t *ip_hdr = (sr_ip_hdr_t *) (copy_buf + sizeof(sr_ethernet_hdr_t));

    /* Check TTL (use for traceroute)*/
    if(ip_hdr->ip_ttl == 1)
        return TIME_EXCEEDED;

    /* find routing entry matching with destination ip address */
    sr_rt_tt *routing_entry = NULL;
    routing_entry = sr_longest_prefix_match(sr, dst_ip);
    if(routing_entry == NULL)
        return NET_UNREACHABLE;// return and send icmp (net unreachable)

    /* match -> find MAC address*/
    struct sr_if *interface_entry = NULL;
    interface_entry = sr_get_interface(sr, routing_entry->interface);
    char *iface = interface_entry->name;
    
    /*ARP lookup*/
    struct sr_arpentry *arp_entry = NULL;

    arp_entry = sr_arpcache_lookup(&sr->cache, dst_ip);
    
	if (arp_entry) {
        /* update source and destination MAC address */
		memcpy(eth_hdr->ether_shost, interface_entry->addr, ETHER_ADDR_LEN);
		memcpy(eth_hdr->ether_dhost, arp_entry->mac, ETHER_ADDR_LEN);
	} else {
		fprintf(stderr, "MAC not found in ARP cache, queuing this request\n");
		struct sr_arpreq *req = sr_arpcache_queuereq(&sr->cache, ip_hdr->ip_dst, copy_buf, len, iface);
        sr_handle_arp_req(sr, req);
		return 0;
	}
    
	/* Update ttl and checksum value after finding arp entry */
	ip_hdr->ip_ttl--;
	ip_hdr->ip_sum = 0;
	ip_hdr->ip_sum = cksum(ip_hdr, sizeof(sr_ip_hdr_t));
	
    /* forwading packet */
	ret = sr_send_packet(sr, copy_buf, len, iface);
	free(copy_buf);
	return ret;
}
int checking_packet(uint8_t *packet, unsigned int len)
{
    uint32_t check_len = sizeof(sr_ethernet_hdr_t); // init first header layer of packet (MAC header)
    uint16_t computed_cksum = 0;
    uint16_t correct_cksum = 0;
    sr_ip_hdr_t *ip_hdr = NULL;
    sr_icmp_hdr_t *icmp_hdr = NULL;
    sr_arp_hdr_t *arp_hdr = NULL;

    if(len < check_len)
    {
        fprintf(stderr,"[ERROR]: Receiving packet broken (MAC header)!\n");
        return -1;
    }
    else{
        #ifdef DEBUG_PRINT
        print_hdr_eth(packet);
        #endif
        uint16_t ethtype = ethertype(packet);
        if(ethtype == ethertype_ip){
            /* IP */
            check_len += sizeof(sr_ip_hdr_t);
            if (len < check_len) {
                fprintf(stderr, "[ERROR]: Receiving packet broken (IP header)!\n");
                return -1;
            }

            #ifdef DEBUG_PRINT
            print_hdr_ip(packet + sizeof(sr_ethernet_hdr_t));
            #endif

            uint8_t ip_proto = ip_protocol(packet + sizeof(sr_ethernet_hdr_t));
            /*check sum*/
            ip_hdr = (sr_ip_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));
            correct_cksum = ip_checksum(packet + sizeof(sr_ethernet_hdr_t));
            ip_hdr->ip_sum = 0;
            computed_cksum = cksum(ip_hdr, sizeof(sr_ip_hdr_t));
            ip_hdr->ip_sum = correct_cksum;
            if(computed_cksum != correct_cksum)
            {
                fprintf(stderr,"[ERROR]: Checksum sum when computing IP header!\n");
                return -1;
            }
            if(ip_proto == ip_protocol_icmp){/* ICMP */
                check_len += sizeof(sr_icmp_hdr_t);
                if(len < check_len)
                    fprintf(stderr, "[ERROR]: insufficient length in ICMP header\n");
                else
                {
                    #ifdef DEBUG_PRINT
                    print_hdr_icmp(packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
                    #endif
                }
                icmp_hdr = (sr_icmp_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
                correct_cksum = icmp_checksum(packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
                icmp_hdr->icmp_sum = 0;
                computed_cksum = cksum(icmp_hdr, len - sizeof(sr_ethernet_hdr_t) - sizeof(sr_ip_hdr_t));
                icmp_hdr->icmp_sum = correct_cksum;
                if(computed_cksum != correct_cksum)
                {
                    fprintf(stderr,"[ERROR]: Incorrect checksum sum when computing ICMP header!\n");
                    return -1;
                }
            }
            else
                fprintf(stderr,"[ERROR]: Unsupported protocol: UDP/ TCP!\n");
        }
        else if (ethtype == ethertype_arp) { /* ARP */
            check_len += sizeof(sr_arp_hdr_t);
            arp_hdr = (sr_arp_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));                            
            if (len < check_len)
                fprintf(stderr, "[ERROR]: Failed to print ARP header, insufficient length\n");
            else
            {
                #ifdef DEBUG_PRINT
                print_hdr_arp((uint8_t *)arp_hdr);
                #endif
            }
        }
        else {
            fprintf(stderr, "[ERROR]: Unrecognized Ethernet Type: %d\n", ethtype);
            return -1;
        }
    }
    return 0;
}
/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *

 *---------------------------------------------------------------------*/

void sr_handlepacket(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */)
{
    /* REQUIRES */
    assert(sr);
    assert(packet);
    assert(interface);

    printf("*** -> Received packet of length %d \n",len);
    int ret;

    /* Checking packet */
    if(-1 == (ret = checking_packet(packet, len)))
    {
        fprintf(stderr, "[ERROR]: Receving packet\n");
        return;
    }

    sr_ethernet_hdr_t *ether_hdr = NULL;
    sr_ip_hdr_t *ip_hdr = NULL;
    sr_icmp_hdr_t *icmp_hdr = NULL;
    uint8_t *icmp_data = NULL;
    unsigned int icmp_datalen = 0;
    sr_arp_hdr_t *arp_hdr = NULL;

    struct sr_if *current_interface = NULL;
    struct sr_if *next_interface = NULL;
    ether_hdr = (sr_ethernet_hdr_t *)packet;

    // sr_rt_tt *routing_entry = NULL;
    struct sr_if *interface_entry = NULL;
    // char *iface = NULL;
    if(ethertype(packet) == ethertype_ip){
        /* IP */
        ip_hdr = (sr_ip_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));
        
        #ifdef DEBUG_PRINT
        fprintf(stderr, "Source IP address: ");
		print_addr_ip_int(ntohl(ip_hdr->ip_src));
		fprintf(stderr, " Destination IP address: ");
		print_addr_ip_int(ntohl(ip_hdr->ip_dst));
		fprintf(stderr, " ID of header: %u\n", ntohs(ip_hdr->ip_id));
        #endif

        for(current_interface = sr->if_list; current_interface != NULL; current_interface = next_interface)
        {
            next_interface = current_interface->next;
            fprintf(stderr,"current ip inteface:\n");
            print_addr_ip_int(ntohl(current_interface->ip));
            if(ntohl(ip_hdr->ip_dst) == ntohl(current_interface->ip))
            {
                /* sent to this host */
                if(ip_hdr->ip_p == ip_protocol_icmp)
                {
                    /* send ICMP reply */
                    icmp_hdr = (sr_icmp_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
                    if(icmp_hdr->icmp_type != ECHO_REQUEST)
                    {
                        fprintf(stderr, "[ERROR]: This is not icmp request! -> quit!\n");
                        return;
                    }
                    // routing_entry = sr_longest_prefix_match(sr, ip_hdr->ip_src);//dst_ip = source ip of icmp request (doesnt like ip in arp request)
                    // interface_entry = sr_get_interface(sr, routing_entry->interface);
                    // iface = interface->name;
                    icmp_data = (uint8_t *)(icmp_hdr + sizeof(sr_icmp_hdr_t));
                    icmp_datalen = len - sizeof(sr_ethernet_hdr_t) - sizeof(sr_ip_hdr_t) -sizeof(sr_icmp_hdr_t);
                    if(icmp_datalen <= 0)
                    {
                        fprintf(stderr,"[ERROR]: No data in ICMP echo request !\n");
                        return;
                    }
                    printf("*** <- Sent ICMP reply \n");
                    ret = send_icmp_reply(sr, interface, ntohs(ip_hdr->ip_id), ether_hdr->ether_shost, ether_hdr->ether_dhost,
                                        ntohl(ip_hdr->ip_src),ntohl(ip_hdr->ip_dst), icmp_data, icmp_datalen, ECHO_REPLY);
                    if(-1 == ret)
                        fprintf(stderr, "[ERROR]: Sending icmp reply\n");
                    return;
                }
                else
                {
                    /* UDP/ TCP: unsupported protocol ->send_icmp_error_notify */
                    printf("*** <- Sent ICMP notify: Port unreachable \n");
                    send_icmp_error_notify(sr, ether_hdr, ip_hdr, ntohl(ip_hdr->ip_src), PORT_UNREACHABLE);
                    return;
                }
            }
            else{
                /* forward packet */
                printf("*** <- Forward Packet\n");
                ret = forward_packet(sr, packet, len, ip_hdr->ip_dst); // dont care interface we received this packet
                if(ret != 0)
                {
                    printf("*** <- Forward Packet: Failed - Sent ICMP notify error:%d \n", ret);
                    send_icmp_error_notify(sr, ether_hdr, ip_hdr, ntohl(ip_hdr->ip_src), ret);
                }
                return;
            }
        }
    }
    else if(ethertype(packet) == ethertype_arp)
    {
        /* handle ARP request or  ARP reply */
        arp_hdr = (sr_arp_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));
        
        if(ntohs(arp_hdr->ar_op) == arp_op_request)
        {
            /* handle ARP request */
            interface_entry = sr_get_interface(sr, interface);
            
            printf("*** <- Sent ARP reply packet of length %d \n",len);
            ret = send_arp_reply(sr, arp_hdr->ar_sha,interface_entry->addr, ntohl(arp_hdr->ar_sip), ntohl(arp_hdr->ar_tip), interface);
            if(-1 == ret)
                fprintf(stderr, "[ERROR]: Sending ARP reply!\n");
            return;
        }
        else if(ntohs(arp_hdr->ar_op) == arp_op_reply)
        {
            /* handle ARP reply */
            printf("*** <- Handle ARP reply packet of length %d \n",len);
            unsigned char *dst_etheraddr_reply = arp_hdr->ar_sha; /* Get source MAC address */
            unsigned char *src_etheraddr_reply = arp_hdr->ar_tha; /* Get source MAC address */

            uint32_t dst_ip_reply = arp_hdr->ar_sip; /* Get source IP address */
            /* sending a copy of packet stored in arp_request queue (no need to do in real packet)  */
            sr_handle_arp_reply(sr, dst_etheraddr_reply, src_etheraddr_reply, dst_ip_reply);
            return;
        }
    }
}/* end sr_ForwardPacket */

