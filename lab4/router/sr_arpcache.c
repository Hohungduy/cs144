#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include "sr_arpcache.h"
#include "sr_router.h"
#include "sr_if.h"
#include "sr_protocol.h"
#include "sr_utils.h"
#include "sr_rt.h"

void send_reply_host_unreachable(struct sr_instance *sr, struct sr_arpreq *req)
{
    sr_ethernet_hdr_t *ether_hdr = NULL;
    sr_ip_hdr_t *ip_hdr = NULL;
    // unsigned int len = 0;
    // char *iface = NULL;
    uint32_t dst_ip = 0; /* destination ip address */
    struct sr_packet *current_packet = NULL, *next_packet = NULL;
    int ret = 0;

    if(req->packets ==  NULL)
        return;

    for(current_packet = req->packets; current_packet != NULL; current_packet = next_packet)
    {
        next_packet = current_packet->next;
        ether_hdr = (sr_ethernet_hdr_t *)(current_packet->buf);
        ip_hdr = (sr_ip_hdr_t *)(current_packet->buf + ETHERNET_HDR_SIZE);
        // len = current_packet->len;
        // iface = current_packet->iface;
        dst_ip = ip_hdr->ip_src;/* reply to the source address that requested */
        ret = send_icmp_error_notify(sr, ether_hdr, ip_hdr, ntohl(dst_ip), HOST_UNREACHABLE); /* free after using */
        if(-1 == ret)
            return;
    }
}
/*
    Handle sending ARP requests if necessary:
    
    Pseudocode for use of these structures follows:
        function handle_arpreq(req):
        if difftime(now, req->sent) >= 1.0
            if req->times_sent >= 5:
                send icmp host unreachable to source addr of all pkts waiting
                    on this request
                arpreq_destroy(req)
            else:
                send arp request
                req->sent = now
                req->times_sent++
*/
void sr_handle_arp_req(struct sr_instance *sr, struct sr_arpreq *req){
    time_t current_time = time(NULL);
    struct sr_arpcache *cache = (struct sr_arpcache *)&sr->cache;
    int ret;
    uint32_t dst_ip;
    struct sr_packet * current_packet = NULL, * next_packet = NULL;
    sr_ethernet_hdr_t *ether_hdr = NULL;
    sr_ip_hdr_t *ip_hdr = NULL;

    if (difftime(current_time, req->sent) >= 1.0)
    {
        if(req->times_sent >= 5)
        {
            #ifdef DEBUG_PRINT
            fprintf(stderr,"[%d]:%s: Cannot find MAC address matching with the IP address \
                        below (after 5 time sent)\n",__LINE__, __func__);
            print_addr_ip_int(ntohl(req->ip));
            #endif
            send_reply_host_unreachable(sr, req);// send all packets related to this requests
            sr_arpreq_destroy(cache, req);
        }
        else{
            printf("*** <- Sending ARP request!\n");
            ret = send_arp_request(sr, req);// find MAC address related to IP address (argument req->ip passed)
            // MAYBE: USE LPM everytime that we send arp request to increase the possibility of finding the possible interface.
            if(-1 == ret)
            {
                for(current_packet = req->packets; current_packet != NULL; current_packet = next_packet)
                {
                    next_packet = current_packet->next;
                    ether_hdr = (sr_ethernet_hdr_t *)current_packet->buf;
                    ip_hdr = (sr_ip_hdr_t *)(current_packet->buf + ETHERNET_HDR_SIZE);
                    dst_ip = ip_hdr->ip_src;
                    send_icmp_error_notify(sr, ether_hdr, ip_hdr, ntohl(dst_ip), HOST_UNREACHABLE);
                }
                sr_arpreq_destroy(cache, req);
            }
            req->sent = time(NULL);
            req->times_sent++;
        }
    }
}

/*
    Handle ARP reply: Move entries form the ARP request queue to the ARP entries caches:
    This function shoud be called when receiving ARP reply from other devices

    Pseudocode:

    # When servicing an arp reply that gives us an IP->MAC mapping
    req = arpcache_insert(ip, mac)

    if req:
        send all packets on the req->packets linked list
        arpreq_destroy(req)

    Notice:
    - dst_ip: network byte ordered (long)
*/
void sr_handle_arp_reply(struct sr_instance *sr, unsigned char *dst_mac, unsigned char *src_mac, uint32_t dst_ip)
{
    struct sr_arpreq *req =  NULL;
    struct sr_packet *current_packet = NULL, *next_packet = NULL;
    struct sr_arpcache *cache = (struct sr_arpcache *)&sr->cache;

    sr_rt_tt *routing_entry = NULL;
    routing_entry = sr_longest_prefix_match(sr, dst_ip);//dst_ip = source ip of icmp request (doesnt like ip in arp request)
    struct sr_if *interface = NULL;
    interface = sr_get_interface(sr, routing_entry->interface);
    char *iface = interface->name;
    int ret;

    sr_ethernet_hdr_t *ether_hdr = NULL;

    if((req = sr_arpcache_insert(cache, dst_mac, dst_ip)) == NULL)
        return;

    /* send all packets on the req->packets linked list */
    if(req->packets == NULL)
        return;

    for(current_packet = req->packets; current_packet != NULL; current_packet = next_packet)
    {
        next_packet = current_packet->next;
        ether_hdr = (sr_ethernet_hdr_t *)(current_packet->buf);
        memset(ether_hdr->ether_dhost, 0, ETHER_ADDR_LEN);
        memcpy(ether_hdr->ether_dhost, dst_mac, ETHER_ADDR_LEN);
        memset(ether_hdr->ether_shost, 0, ETHER_ADDR_LEN);
        memcpy(ether_hdr->ether_shost, src_mac, ETHER_ADDR_LEN);
        fprintf(stderr,"[%d]: %s\n", __LINE__, __func__);
        #ifdef DEBUG_PRINT
        print_hdrs(current_packet->buf, current_packet->len);
        #endif
        ret = sr_send_packet(sr,current_packet->buf, current_packet->len, iface);
        if(-1 == ret)
            break;
    }
    sr_arpreq_destroy(cache, req);
}
/* 
  This function gets called every second. For each request sent out, we keep
  checking whether we should resend an request or destroy the arp request.
  See the comments in the header file for an idea of what it should look like.
  
  Pseudocode:
     void sr_arpcache_sweepreqs(struct sr_instance *sr) {
       for each request on sr->cache.requests:
           handle_arpreq(request)
   }
*/
void sr_arpcache_sweepreqs(struct sr_instance *sr) { 
    /* Fill this in*/
    struct sr_arpcache cache = sr->cache;
    struct sr_arpreq *current_req = NULL, *next_req = NULL; 

    if(cache.requests == NULL)
        return;

    for(current_req = cache.requests; current_req != NULL; current_req = next_req)
    {
        next_req = current_req->next;
        sr_handle_arp_req(sr, current_req);
    }
}

/* You should not need to touch the rest of this code. */

/* Checks if an IP->MAC mapping is in the cache. IP is in network byte order.
   You must free the returned structure if it is not NULL. */
struct sr_arpentry *sr_arpcache_lookup(struct sr_arpcache *cache, uint32_t ip) {
    pthread_mutex_lock(&(cache->lock));
    
    struct sr_arpentry *entry = NULL, *copy = NULL;
    
    int i;
    for (i = 0; i < SR_ARPCACHE_SZ; i++) {
        if ((cache->entries[i].valid) && (cache->entries[i].ip == ip)) {
            entry = &(cache->entries[i]);
        }
    }
    
    /* Must return a copy b/c another thread could jump in and modify
       table after we return. */
    if (entry) {
        copy = (struct sr_arpentry *) malloc(sizeof(struct sr_arpentry));
        memcpy(copy, entry, sizeof(struct sr_arpentry));
    }
        
    pthread_mutex_unlock(&(cache->lock));
    
    return copy;
}

/* Adds an ARP request to the ARP request queue. If the request is already on
   the queue, adds the packet to the linked list of packets for this sr_arpreq
   that corresponds to this ARP request. You should free the passed *packet.
   
   A pointer to the ARP request is returned; it should not be freed. The caller
   can remove the ARP request from the queue by calling sr_arpreq_destroy. */
struct sr_arpreq *sr_arpcache_queuereq(struct sr_arpcache *cache,
                                       uint32_t ip,
                                       uint8_t *packet,           /* borrowed */
                                       unsigned int packet_len,
                                       char *iface)
{
    pthread_mutex_lock(&(cache->lock));
    
    struct sr_arpreq *req;
    for (req = cache->requests; req != NULL; req = req->next) {
        if (req->ip == ip) {
            break;
        }
    }
    
    /* If the IP wasn't found, add it */
    if (!req) {
        req = (struct sr_arpreq *) calloc(1, sizeof(struct sr_arpreq));
        req->ip = ip;
        req->next = cache->requests;
        cache->requests = req;
    }
    
    /* Add the packet to the list of packets for this request */
    if (packet && packet_len && iface) {
        struct sr_packet *new_pkt = (struct sr_packet *)malloc(sizeof(struct sr_packet));
        
        new_pkt->buf = (uint8_t *)malloc(packet_len);
        memcpy(new_pkt->buf, packet, packet_len);
        new_pkt->len = packet_len;
		new_pkt->iface = (char *)malloc(sr_IFACE_NAMELEN);
        strncpy(new_pkt->iface, iface, sr_IFACE_NAMELEN);
        new_pkt->next = req->packets;
        req->packets = new_pkt;
    }
    
    pthread_mutex_unlock(&(cache->lock));
    
    return req;
}

/* This method performs two functions:
   1) Looks up this IP in the request queue. If it is found, returns a pointer
      to the sr_arpreq with this IP. Otherwise, returns NULL.
   2) Inserts this IP to MAC mapping in the cache, and marks it valid. */
struct sr_arpreq *sr_arpcache_insert(struct sr_arpcache *cache,
                                     unsigned char *mac,
                                     uint32_t ip)
{
    pthread_mutex_lock(&(cache->lock));
    
    struct sr_arpreq *req, *prev = NULL, *next = NULL; 
    for (req = cache->requests; req != NULL; req = req->next) {
        if (req->ip == ip) {            
            if (prev) {
                next = req->next;
                prev->next = next;
            } 
            else {
                next = req->next;
                cache->requests = next;
            }
            
            break;
        }
        prev = req;
    }
    
    int i;
    for (i = 0; i < SR_ARPCACHE_SZ; i++) {
        if (!(cache->entries[i].valid))
            break;
    }
    
    if (i != SR_ARPCACHE_SZ) {
        memcpy(cache->entries[i].mac, mac, 6);
        cache->entries[i].ip = ip;
        cache->entries[i].added = time(NULL);
        cache->entries[i].valid = 1;
    }
    
    pthread_mutex_unlock(&(cache->lock));
    
    return req;
}

/* Frees all memory associated with this arp request entry. If this arp request
   entry is on the arp request queue, it is removed from the queue. */
void sr_arpreq_destroy(struct sr_arpcache *cache, struct sr_arpreq *entry) {
    pthread_mutex_lock(&(cache->lock));
    
    if (entry) {
        struct sr_arpreq *req, *prev = NULL, *next = NULL; 
        for (req = cache->requests; req != NULL; req = req->next) {
            if (req == entry) {                
                if (prev) {
                    next = req->next;
                    prev->next = next;
                } 
                else {
                    next = req->next;
                    cache->requests = next;
                }
                
                break;
            }
            prev = req;
        }
        
        struct sr_packet *pkt, *nxt;
        
        for (pkt = entry->packets; pkt; pkt = nxt) {
            nxt = pkt->next;
            if (pkt->buf)
                free(pkt->buf);
            if (pkt->iface)
                free(pkt->iface);
            free(pkt);
        }
        
        free(entry);
    }
    
    pthread_mutex_unlock(&(cache->lock));
}

/* Prints out the ARP table. */
void sr_arpcache_dump(struct sr_arpcache *cache) {
    fprintf(stderr, "\nMAC            IP         ADDED                      VALID\n");
    fprintf(stderr, "-----------------------------------------------------------\n");
    
    int i;
    for (i = 0; i < SR_ARPCACHE_SZ; i++) {
        struct sr_arpentry *cur = &(cache->entries[i]);
        unsigned char *mac = cur->mac;
        fprintf(stderr, "%.1x%.1x%.1x%.1x%.1x%.1x   %.8x   %.24s   %d\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ntohl(cur->ip), ctime(&(cur->added)), cur->valid);
    }
    
    fprintf(stderr, "\n");
}

/* Initialize table + table lock. Returns 0 on success. */
int sr_arpcache_init(struct sr_arpcache *cache) {  
    /* Seed RNG to kick out a random entry if all entries full. */
    srand(time(NULL));
    
    /* Invalidate all entries */
    memset(cache->entries, 0, sizeof(cache->entries));
    cache->requests = NULL;
    
    /* Acquire mutex lock */
    pthread_mutexattr_init(&(cache->attr));
    pthread_mutexattr_settype(&(cache->attr), PTHREAD_MUTEX_RECURSIVE_NP);
    int success = pthread_mutex_init(&(cache->lock), &(cache->attr));
    
    return success;
}

/* Destroys table + table lock. Returns 0 on success. */
int sr_arpcache_destroy(struct sr_arpcache *cache) {
    return pthread_mutex_destroy(&(cache->lock)) && pthread_mutexattr_destroy(&(cache->attr));
}

/* Thread which sweeps through the cache and invalidates entries that were added
   more than SR_ARPCACHE_TO seconds ago. */
void *sr_arpcache_timeout(void *sr_ptr) {
    struct sr_instance *sr = sr_ptr;
    struct sr_arpcache *cache = &(sr->cache);
    
    while (1) {
        sleep(1.0);
        
        pthread_mutex_lock(&(cache->lock));
    
        time_t curtime = time(NULL);
        
        int i;    
        for (i = 0; i < SR_ARPCACHE_SZ; i++) {
            if ((cache->entries[i].valid) && (difftime(curtime,cache->entries[i].added) > SR_ARPCACHE_TO)) {
                cache->entries[i].valid = 0;
            }
        }
        
        sr_arpcache_sweepreqs(sr);

        pthread_mutex_unlock(&(cache->lock));
    }
    
    return NULL;
}

