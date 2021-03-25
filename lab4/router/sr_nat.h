
#ifndef SR_NAT_TABLE_H
#define SR_NAT_TABLE_H

#include <inttypes.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <stdbool.h>
#include <stdio.h>
#include "sr_protocol.h"
#define ICMP_MAPPING_TIMEOUT (60)
#define TCP_ESTABLISHED_TIMEOUT (7440)
#define TCP_TRANSITORY (240)


typedef enum {
  nat_mapping_icmp,
  nat_mapping_tcp
  /* nat_mapping_udp, */
} sr_nat_mapping_type;

typedef enum {
  state_transitory = 1,
  state_established
}state_t;
struct sr_nat_connection {
  /* use 4 tuple: source/ destination IP address and source/ destination port 
    in order to determine connection */
  uint32_t src_ip;/*src ip*/
  uint32_t dst_ip;/*dst ip */

  uint16_t src_port;/*src port*/
  uint16_t dst_port;/*dst port*/

  /* add TCP connection state data members here */
  time_t last_established; /* time stamp for Established state */
  time_t last_transitory; /* time stamp for transitory state (partial open) */
  
  bool receive_SYN_int; /* receive SYN from internal side */
  bool receive_SYN_ext; /* receive SYN form external side */

  state_t state;
  
  struct sr_nat_connection *next;
};

struct sr_nat_mapping {
  sr_nat_mapping_type type;
  uint32_t ip_int; /* internal ip addr */
  uint32_t ip_ext; /* external ip addr */
  uint16_t aux_int; /* internal port or icmp id */
  uint16_t aux_ext; /* external port or icmp id */
  time_t last_updated; /* use to timeout mappings */
  bool valid; /* flag to mark valid or invalid */
  struct sr_nat_connection *conns; /* list of connections. null for ICMP */
  struct sr_nat_mapping *next;
};

struct sr_nat {
  /* add any fields here */
  struct sr_nat_mapping *mappings;

  /* threading */
  pthread_mutex_t lock;
  pthread_mutexattr_t attr;
  pthread_attr_t thread_attr;
  pthread_t thread;
};


int   sr_nat_init(struct sr_nat *nat);     /* Initializes the nat */
int   sr_nat_destroy(struct sr_nat *nat);  /* Destroys the nat (free memory) */
void *sr_nat_timeout(void *nat_ptr);  /* Periodic Timout */

/* Get the mapping associated with given external port.
   You must free the returned structure if it is not NULL. */
struct sr_nat_mapping *sr_nat_lookup_external(struct sr_nat *nat,
    uint16_t aux_ext, sr_nat_mapping_type type );

/* Get the mapping associated with given internal (ip, port) pair.
   You must free the returned structure if it is not NULL. */
struct sr_nat_mapping *sr_nat_lookup_internal(struct sr_nat *nat,
  uint32_t ip_int, uint16_t aux_int, sr_nat_mapping_type type );

/* Insert a new mapping into the nat's mapping table.
   You must free the returned structure if it is not NULL. */
struct sr_nat_mapping *sr_nat_insert_mapping(struct sr_nat *nat,
  uint32_t ip_int, uint16_t aux_int, sr_nat_mapping_type type );

struct sr_nat_connection* sr_lookup_nat_tcpconnection(struct sr_nat_mapping *mapping, sr_ip_hdr_t *ip_hdr);
struct sr_nat_connection* sr_insert_nat_tcpconnection(struct sr_nat_mapping *mapping, sr_ip_hdr_t *ip_hdr);
struct sr_nat_connection* sr_destroy_nat_tcpconnection(struct sr_nat_mapping *mapping, struct sr_nat_connection);
#endif