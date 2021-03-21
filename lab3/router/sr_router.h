/*-----------------------------------------------------------------------------
 * File: sr_router.h
 * Date: ?
 * Authors: Guido Apenzeller, Martin Casado, Virkam V.
 * Contact: casado@stanford.edu
 *
 *---------------------------------------------------------------------------*/

#ifndef SR_ROUTER_H
#define SR_ROUTER_H

#include <netinet/in.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sr_protocol.h"
#include "sr_arpcache.h"

/* we dont like this debug , but what to do for varargs ? */
#ifdef _DEBUG_
#define Debug(x, args...) printf(x, ## args)
#define DebugMAC(x) \
  do { int ivyl; for(ivyl=0; ivyl<5; ivyl++) printf("%02x:", \
  (unsigned char)(x[ivyl])); printf("%02x",(unsigned char)(x[5])); } while (0)
#else
#define Debug(x, args...) do{}while(0)
#define DebugMAC(x) do{}while(0)
#endif

#define INIT_TTL 255
#define PACKET_DUMP_SIZE 1024

/* forward declare */
struct sr_if;
struct sr_rt;

/* define MACRO value for sending type 3 icmp*/

#define ECHO_REPLY 0
#define NET_UNREACHABLE 1
#define HOST_UNREACHABLE 2
#define PORT_UNREACHABLE 3
#define TIME_EXCEEDED 4
#define ECHO_REQUEST 8
/* ----------------------------------------------------------------------------
 * struct sr_instance
 *
 * Encapsulation of the state for a single virtual router.
 *
 * -------------------------------------------------------------------------- */

struct sr_instance
{
    int  sockfd;   /* socket to server */
    char user[32]; /* user name */
    char host[32]; /* host name */ 
    char template[30]; /* template name if any */
    unsigned short topo_id;
    struct sockaddr_in sr_addr; /* address to server */
    struct sr_if* if_list; /* list of interfaces */
    struct sr_rt* routing_table; /* routing table */
    struct sr_arpcache cache;   /* ARP cache */
    pthread_attr_t attr;
    FILE* logfile;
};

/* -- sr_main.c -- */
int sr_verify_routing_table(struct sr_instance* sr);

/* -- sr_vns_comm.c -- */
int sr_send_packet(struct sr_instance* , uint8_t* , unsigned int , const char*);
int sr_connect_to_server(struct sr_instance* ,unsigned short , char* );
int sr_read_from_server(struct sr_instance* );

/* -- sr_router.c -- */
void sr_init(struct sr_instance* );
void sr_handlepacket(struct sr_instance* , uint8_t * , unsigned int , char* );
int send_icmp_error_notify(struct sr_instance *sr, sr_ethernet_hdr_t *ether_hdr, 
                          sr_ip_hdr_t *ip_hdr, uint32_t des_ip, int TypeCode_icmp);
int send_icmp_reply(struct sr_instance *sr, char *iface, uint16_t *ip_id, 
                    uint8_t *ether_dhost, uint8_t *ether_shost, 
                    uint32_t dst_ip, uint32_t src_ip, uint8_t *icmp_data, 
                    uint16_t icmp_data_len, int TypeCode_icmp);
int send_arp_request(struct sr_instance *sr, struct sr_arpreq *req);
int send_arp_reply(struct sr_instance *sr, uint8_t *dst_etheraddr, uint8_t *src_etheraddr, 
                    uint32_t dst_ip, uint32_t src_ip, char *iface);
int checking_packet(uint8_t *packet, unsigned int len);
/* -- sr_if.c -- */
void sr_add_interface(struct sr_instance* , const char* );
void sr_set_ether_ip(struct sr_instance* , uint32_t );
void sr_set_ether_addr(struct sr_instance* , const unsigned char* );
void sr_print_if_list(struct sr_instance* );

#endif /* SR_ROUTER_H */
