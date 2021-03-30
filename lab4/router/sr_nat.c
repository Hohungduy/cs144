
#include <signal.h>
#include <assert.h>
#include "sr_nat.h"
#include "sr_utils.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
char *ext_ip_eth2 = "184.72.104.221";
char *int_ip_eth1 = "10.0.1.1";
#define DEBUG_PRINT
int sr_nat_init(struct sr_nat *nat) { /* Initializes the nat */

  assert(nat);

  /* Acquire mutex lock */
  pthread_mutexattr_init(&(nat->attr));
  pthread_mutexattr_settype(&(nat->attr), PTHREAD_MUTEX_RECURSIVE_NP);
  int success = pthread_mutex_init(&(nat->lock), &(nat->attr));

  /* Initialize timeout thread */

  pthread_attr_init(&(nat->thread_attr));
  pthread_attr_setdetachstate(&(nat->thread_attr), PTHREAD_CREATE_JOINABLE);
  pthread_attr_setscope(&(nat->thread_attr), PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setscope(&(nat->thread_attr), PTHREAD_SCOPE_SYSTEM);
  pthread_create(&(nat->thread), &(nat->thread_attr), sr_nat_timeout, nat);

  /* CAREFUL MODIFYING CODE ABOVE THIS LINE! */

  nat->mappings = NULL;
  /* Initialize any variables here */
  return success;
}


int sr_nat_destroy(struct sr_nat *nat) {  /* Destroys the nat (free memory) */

  pthread_mutex_lock(&(nat->lock));

  /* free nat memory here */
  struct sr_nat_mapping* mapping = NULL, *next_mapping = NULL;
  struct sr_nat_connection *conn = NULL, *next_conn = NULL;
  if((mapping = nat->mappings) == NULL)
    goto end;
  for(mapping = nat->mappings; mapping != NULL; mapping = next_mapping)
  {
    next_mapping = mapping->next;
    if(mapping)
    {
      if((conn = mapping->conns) == NULL)
        goto next;
      for(conn = mapping->conns; conn != NULL; conn = next_conn)
      {
        next_conn = conn->next;
        if(conn)
          free(conn);
      }
next:
      free(mapping);
    }
  }
end:
  pthread_kill(nat->thread, SIGKILL);
  return pthread_mutex_destroy(&(nat->lock)) &&
    pthread_mutexattr_destroy(&(nat->attr));
}

void sr_nat_destroy_mapping(struct sr_nat *nat, struct sr_nat_mapping *entry)
{
  pthread_mutex_lock(&(nat->lock));
  if(entry){
    struct sr_nat_mapping *mapping = NULL, *next_mapping = NULL, *prev_mapping = NULL;
    for(mapping = nat->mappings; mapping != NULL; mapping = mapping->next){
      if(mapping == entry) {
        if(prev_mapping){
          next_mapping = mapping->next;
          prev_mapping->next = next_mapping;
        }
        else{
          next_mapping = mapping->next;
          nat->mappings = next_mapping;
        }
        break;
      }
      prev_mapping = mapping;
    }
    struct sr_nat_connection *conn =NULL, *next_conn = NULL;
    for(conn = mapping->conns; conn; conn = next_conn)
    {
      next_conn = conn->next;
      if(conn)
        free(conn);
    }
    free(entry);
  }

  pthread_mutex_unlock(&(nat->lock));
}
void *sr_nat_timeout(void *nat_ptr) {  /* Periodic Timout handling */
  struct sr_nat *nat = (struct sr_nat *)nat_ptr;
  while (1) {
    sleep(1.0);
    pthread_mutex_lock(&(nat->lock));
    time_t curtime = time(NULL);

    /* handle periodic tasks here */
    struct sr_nat_mapping *mapping_entry = NULL;
    struct sr_nat_connection *conn = NULL, *next_conn = NULL;

    for(mapping_entry = nat->mappings; mapping_entry != NULL; mapping_entry = mapping_entry->next)
    {
      if((mapping_entry->type == nat_mapping_icmp) && (mapping_entry->valid) && (difftime(curtime, mapping_entry->last_updated) > ICMP_MAPPING_TIMEOUT))
      {
        /* ICMP type */
        fprintf(stderr, "[%d]: %s \n", __LINE__, __func__);
        mapping_entry->valid = false;
      }
      if((mapping_entry->type == nat_mapping_tcp) && (mapping_entry->valid))
      {
        /* TCP type */
        /* checking connection */
        if((conn = mapping_entry->conns) == NULL)
          goto next;/* get out of this searching */
        fprintf(stderr, "[%d]: %s \n", __LINE__, __func__);
        for(conn = mapping_entry->conns; conn; conn = next_conn)
        {
          next_conn = conn->next;
          if(conn->state == state_established)
          {
            if(difftime(curtime, conn->last_established) >= TCP_ESTABLISHED_TIMEOUT)
            {
              /* checking timeout when its state is establishment */
              fprintf(stderr, "[%d]: %s \n", __LINE__, __func__);
              sr_destroy_nat_tcpconnection(nat, mapping_entry, conn);/* destroy connection entry if condition is matching */
            }
          }
          else if(conn->state == state_transitory)
          {
            if(difftime(curtime, conn->last_transitory) >= TCP_TRANSITORY)
            {
              /* checking timeout when its state is transitory */
              fprintf(stderr, "[%d]: %s \n", __LINE__, __func__);
              sr_destroy_nat_tcpconnection(nat, mapping_entry, conn); /* destroy connection entry if condition is matching*/
            }
          }
          if((difftime(curtime, conn->last_transitory) >= TCP_TRANSITORY_UNSOLICITED) && 
            (conn->receive_SYN_ext == true) && (conn->state == state_transitory))
          {
            /* checking timeout when its state is transitory and unsolicited */
            fprintf(stderr, "[%d]: %s \n", __LINE__, __func__);
            sr_destroy_nat_tcpconnection(nat, mapping_entry, conn);/* destroy connection entry if condition is matching */
          }
        }
      }
next:
      if(mapping_entry->valid == false)
      {
        fprintf(stderr, "[%d]: %s \n", __LINE__, __func__);
        sr_nat_destroy_mapping(nat, mapping_entry); /* destroy matching entry */
      }
    }
    pthread_mutex_unlock(&(nat->lock));
  }
  return NULL;
}

/* Get the mapping associated with given external port.
   You must free the returned structure if it is not NULL. */
struct sr_nat_mapping *sr_nat_lookup_external(struct sr_nat *nat,
    uint16_t aux_ext, sr_nat_mapping_type type ) {

  pthread_mutex_lock(&(nat->lock));

  /* handle lookup here, malloc and assign to copy */
  struct sr_nat_mapping *copy = NULL, *mapping_entry = NULL, *match_entry = NULL;
  /* mapping entry: iterator, entry: match entry: the entry that we want, copy: The copy of entry */
  if(!(mapping_entry = nat->mappings))
  {
    pthread_mutex_unlock(&(nat->lock));
    return NULL; /* no entry */
  }
  for(mapping_entry = nat->mappings; mapping_entry != NULL; mapping_entry = mapping_entry->next)
  {
    if((mapping_entry->aux_ext == aux_ext) && (mapping_entry->valid = true))
    {
      match_entry = mapping_entry;
    }
  }
  
  if(match_entry)
  {
    copy = (struct sr_nat_mapping *)calloc(1, sizeof(struct sr_nat_mapping));
    memcpy(copy, match_entry, sizeof(struct sr_nat_mapping));
  }

  pthread_mutex_unlock(&(nat->lock));
  return copy;
}

/* Get the mapping associated with given internal (ip, port) pair.
   You must free the returned structure if it is not NULL. */
struct sr_nat_mapping *sr_nat_lookup_internal(struct sr_nat *nat,
  uint32_t ip_int, uint16_t aux_int, sr_nat_mapping_type type ) {

  pthread_mutex_lock(&(nat->lock));

  /* handle lookup here, malloc and assign to copy. */
  struct sr_nat_mapping *copy = NULL, *mapping_entry = NULL, *match_entry = NULL;
  /* mapping entry: iterator, entry: match entry: the entry that we want, copy: The copy of entry */
  if(!(mapping_entry = nat->mappings))
  {
    pthread_mutex_unlock(&(nat->lock));
    return NULL; /* no entry */
  }
  for(mapping_entry = nat->mappings; mapping_entry != NULL; mapping_entry = mapping_entry->next)
  {
    if((mapping_entry->ip_int == ip_int) && (mapping_entry->aux_int == aux_int) && 
        (mapping_entry->type == type) && (mapping_entry->valid = true))
    {
      match_entry = mapping_entry;
    }
  }

  if(match_entry)
  {
    copy = (struct sr_nat_mapping *)calloc(1, sizeof(struct sr_nat_mapping));
    memcpy(copy, match_entry, sizeof(struct sr_nat_mapping));
  }
  /* Must return a copy so that another thread can jump and modify table after we return */
  pthread_mutex_unlock(&(nat->lock));
  return copy;
}

/* Insert a new mapping into the nat's mapping table.
   Actually returns a copy to the new mapping, for thread safety.
 */
/* all of this fielsd in NAT mapping table is in network byte ordered */
struct sr_nat_mapping *sr_nat_insert_mapping(struct sr_nat *nat,
  uint32_t ip_int, uint16_t aux_int, sr_nat_mapping_type type ) {

  pthread_mutex_lock(&(nat->lock));
  srand(time(NULL));

  /* handle insert here, create a mapping, and then return a copy of it */
  struct sr_nat_mapping *mapping_entry = NULL, *copy = NULL;
  struct in_addr ext_addr;

  for(mapping_entry = nat->mappings; mapping_entry != NULL; mapping_entry = mapping_entry->next)
  {
    if((mapping_entry->ip_int == ip_int) && (mapping_entry->aux_int == aux_int)&& 
      (mapping_entry->type == type) && (mapping_entry->valid = true))
      break;
  }
  /* If it was not found or there are no mapping entry in mapping table */
  if(!mapping_entry)
  {
    mapping_entry = (struct sr_nat_mapping*)calloc(1, sizeof(struct sr_nat_mapping));
    /* init some field */
    mapping_entry->type = type;/* network byte ordered */
    mapping_entry->aux_int = aux_int;/* network byte ordered */
    mapping_entry->ip_int = ip_int;/* network byte ordered */
    if(inet_aton(ext_ip_eth2,&ext_addr) == 0)
    {
      fprintf(stderr, "[Error]: cannot convert %s to valid IP\n", ext_ip_eth2);
      pthread_mutex_unlock(&(nat->lock));
      exit(EXIT_FAILURE); 
    }
    mapping_entry->ip_ext = ext_addr.s_addr;/* network byte ordered */
    // mapping_entry->aux_ext = htons(generate_random(1024, 65535));/* network byte ordered */ 
    mapping_entry->aux_ext = aux_int;/* network byte ordered */
    mapping_entry->valid = true;
    mapping_entry->last_updated = time(NULL);
    /* insert this mapping into list */
    mapping_entry->next = nat->mappings;
    nat->mappings = mapping_entry;
  }
  if(mapping_entry)
  {
    copy = (struct sr_nat_mapping *)calloc(1, sizeof(struct sr_nat_mapping));
    memcpy(copy, mapping_entry, sizeof(struct sr_nat_mapping));
  }

  /* write add connection */
  pthread_mutex_unlock(&(nat->lock));
  return copy;
}

/*lookup connectiun matching the specific criteria: 4 tuple*/
struct sr_nat_connection* sr_lookup_insert_or_update_nat_tcpconnection(struct sr_nat *nat, sr_ip_hdr_t *ip_hdr, uint32_t ip_int, uint16_t aux_int, uint16_t aux_ext, pkt_direct_t direct)
{
  pthread_mutex_lock(&(nat->lock));

  tcphdr_t *tcp_hdr = (tcphdr_t*)((uint8_t *)ip_hdr + IP_HDR_SIZE);
  struct sr_nat_connection *conn = NULL, *next_conn = NULL, *copy = NULL;
  struct sr_nat_mapping *mapping_entry = NULL, *mapping = NULL;
  int ret;

  if(direct == inbound)
  {
    for(mapping_entry = nat->mappings; mapping_entry != NULL; mapping_entry = mapping_entry->next)
    {
      if((mapping_entry->aux_ext == aux_ext) && (mapping_entry->valid = true))
      {
        mapping = mapping_entry;
      }
    }
  }
  else if(direct == outbound)
  {
    for(mapping_entry = nat->mappings; mapping_entry != NULL; mapping_entry = mapping_entry->next)
    {
      if((mapping_entry->ip_int == ip_int) && (mapping_entry->aux_int == aux_int) && (mapping_entry->valid = true))
      {
        mapping = mapping_entry;
      }
    }
  }
  if(mapping == NULL)
    fprintf(stderr, "[%d]: %s - no mapping !!!\n", __LINE__, __func__);
  if(NULL == (conn = mapping->conns))
      fprintf(stderr, "[%d]: %s - no connection !!!\n", __LINE__, __func__);
  for(conn = mapping->conns; conn != NULL; conn = next_conn)
  {
    next_conn = conn->next;
    if(direct == inbound)
    {
      fprintf(stderr, "conn->int_conn_ip: %d\n", conn->int_conn_ip);
      fprintf(stderr, "conn->int_conn_port: %d\n", conn->int_conn_port);
      fprintf(stderr, "conn->ext_conn_ip: %d\n", conn->ext_conn_ip);
      fprintf(stderr, "conn->ext_conn_port: %d\n", conn->ext_conn_port);
      fprintf(stderr, "mapping->ip_int: %d\n", mapping->ip_int);
      fprintf(stderr, "mapping->aux_int: %d\n", mapping->aux_int);
      fprintf(stderr, "ip_hdr->ip_src: %d\n", ip_hdr->ip_src);
      fprintf(stderr, "tcp_hdr->th_sport: %d\n", tcp_hdr->th_sport);
      if((conn->int_conn_ip == mapping->ip_int) && (conn->ext_conn_ip == ip_hdr->ip_src) 
      && (conn->int_conn_port == mapping->aux_int) && (conn->ext_conn_port == tcp_hdr->th_sport))
      {
        break;
      }
    }
    else if (direct == outbound)
    {
      fprintf(stderr, "conn->int_conn_ip: %d\n", conn->int_conn_ip);
      fprintf(stderr, "conn->int_conn_port: %d\n", conn->int_conn_port);
      fprintf(stderr, "conn->ext_conn_ip: %d\n", conn->ext_conn_ip);
      fprintf(stderr, "conn->ext_conn_port: %d\n", conn->ext_conn_port);
      fprintf(stderr, "mapping->ip_int: %d\n", mapping->ip_int);
      fprintf(stderr, "mapping->aux_int: %d\n", mapping->aux_int);
      fprintf(stderr, "ip_hdr->ip_dst: %d\n", ip_hdr->ip_dst);
      fprintf(stderr, "tcp_hdr->th_dport: %d\n", tcp_hdr->th_dport);
      if((conn->int_conn_ip == mapping->ip_int) && (conn->ext_conn_ip == ip_hdr->ip_dst) 
      && (conn->int_conn_port == mapping->aux_int) && (conn->ext_conn_port == tcp_hdr->th_dport))
      {
        break;
      }
    }
  }
  if(conn == NULL)
    fprintf(stderr, "[%d]: %s - no connection !!!\n", __LINE__, __func__);
  if(!conn)
  {
    /* Insert new connection and return a copy*/
    copy = sr_insert_nat_tcpconnection(mapping, ip_hdr, direct);
    if(copy == NULL)
      fprintf(stderr, "[%d]: %s\n", __LINE__, __func__);
  }

  if(conn)
  {
    if(-1 == (ret = sr_update_nat_tcpconnection(conn, ip_hdr, direct)))
    {
      fprintf(stderr, "[ERROR]: Update tcp connection state!\n");
    }
    copy = (struct sr_nat_connection*)calloc(1, sizeof(struct sr_nat_connection));
    memcpy(copy, conn, sizeof(struct sr_nat_connection));
  }

  pthread_mutex_unlock(&(nat->lock));
  return copy;
}

/* Insert new connection and its state 
  return NULL if this is not SYN packet*/
struct sr_nat_connection* sr_insert_nat_tcpconnection(struct sr_nat_mapping *mapping, sr_ip_hdr_t *ip_hdr, pkt_direct_t direct)
{
  srand(time(NULL));
  tcphdr_t *tcp_hdr = (tcphdr_t*)((uint8_t *)ip_hdr + IP_HDR_SIZE);
  struct sr_nat_connection *conn = NULL, *copy = NULL;
  conn = (struct sr_nat_connection *)calloc(1, sizeof(struct sr_nat_connection));
  /* init internal ip/port */
  conn->int_conn_ip = mapping->ip_int;
  conn->int_conn_port = mapping->aux_int;
  fprintf(stderr, "[%d]: %s\n", __LINE__, __func__);
  if(direct == inbound)
  {
    /* inbound */
    /* init external ip/port */
    conn->ext_conn_ip = ip_hdr->ip_src;
    conn->ext_conn_port = tcp_hdr->th_sport;
    printf("[%d]: %s - tcp_hdr->th_flags: %x\n", __LINE__, __func__, tcp_hdr->th_flags);
    if(tcp_hdr->th_flags & TH_SYN)
    {
      /* init external seqno and its state*/
      conn->receive_SYN_ext = true;
      /* remeber check this fields after returning
      if it is set -> dont forward it
      */
      conn->init_ext_seqno = tcp_hdr->th_seq;
      printf("conn->init_ext_seqno: %d\n", conn->init_ext_seqno );
      conn->init_int_seqno = 0;
      conn->last_transitory = time(NULL);
      conn->state = state_transitory;
    }
    else
    {
      fprintf(stderr, "[%d]: %s - [ERROR]: This is not SYN packet \n", __LINE__, __func__);
      free(conn);
      return NULL;
    }
    /* insert this mapping into list */
    fprintf(stderr, "[%d]: %s\n", __LINE__, __func__);
    conn->next = mapping->conns;
    mapping->conns = conn;
  }
  else
  {
    /* outbound */
    /* init external ip/port */
    conn->ext_conn_ip = ip_hdr->ip_dst;
    conn->ext_conn_port = tcp_hdr->th_dport;
    printf("[%d]: %s - tcp_hdr->th_flags : %x\n", __LINE__, __func__, tcp_hdr->th_flags);
    if(tcp_hdr->th_flags & TH_SYN)
    {
      conn->receive_SYN_int = true;
      conn->init_int_seqno = tcp_hdr->th_seq;
      printf("conn->init_int_seqno: %d\n", conn->init_int_seqno );
      conn->init_ext_seqno = 0;
      conn->last_transitory = time(NULL);
      conn->state = state_transitory;
    }
    else
    {
      fprintf(stderr, "[%d]: %s - [ERROR]: This is not SYN packet \n", __LINE__, __func__);
      free(conn);
      return NULL;
    }
    /* insert this mapping into list */
    fprintf(stderr, "[%d]: %s\n", __LINE__, __func__);
    conn->next = mapping->conns;
    mapping->conns = conn;
  }
  fprintf(stderr, "[%d]: %s\n", __LINE__, __func__);
  /*  make a copy and return it */
  if(conn)
  {
    fprintf(stderr, "[%d]: %s\n", __LINE__, __func__);
    copy = (struct sr_nat_connection *)calloc(1, sizeof(struct sr_nat_connection));
    memcpy(copy, conn, sizeof(struct sr_nat_connection));
  }
  return copy;
}

/* Update new connection and its state 
  return NULL if this is not SYN packet*/
int sr_update_nat_tcpconnection(struct sr_nat_connection *conn, sr_ip_hdr_t *ip_hdr, pkt_direct_t direct)
{
  srand(time(NULL));
  tcphdr_t *tcp_hdr = (tcphdr_t*)((uint8_t *)ip_hdr + IP_HDR_SIZE);
  time_t curtime = time(NULL);
  if(tcp_hdr->th_flags & TH_SYN)
  {
    if(direct == inbound)
    {
      /*inbound*/
      if(tcp_hdr->th_flags & TH_ACK)
      {
        #ifdef DEBUG_PRINT
        fprintf(stderr, "[%d]:%s - SYN-ACK pkt and this is a inbound pkt\n", __LINE__, __func__);
        #endif
        /*SYN-ACK */
        if(ntohl(tcp_hdr->th_ack) == (ntohl(conn->init_int_seqno) + 1))
        {
          /* valid ack no */
          conn->receive_SYN_ACK_ext = true;
          conn->init_ext_seqno = tcp_hdr->th_seq;
        }
        else
        {
          fprintf(stderr, "[ERROR]: when receiving SYN-ACK pkt, no matching ackno with previous seqno in previous SYN packets\n \ttcp_hdr->th_ack:%u - (conn->init_int_seqno + 1): %u \n", ntohl(tcp_hdr->th_ack), (ntohl(conn->init_int_seqno) + 1));
          return -1;
        }
      }
      else 
      { /* only SYN */
        #ifdef DEBUG_PRINT
        fprintf(stderr, "[%d]:%s - SYN pkt and this is a inbound pkt\n", __LINE__, __func__);
        #endif
        conn->receive_SYN_ext = true;
        conn->init_ext_seqno = tcp_hdr->th_seq;
      }
    }
    else
    {
      /*outbound */
      if(tcp_hdr->th_flags & TH_ACK)
      {
        /* SYN-ACK pkt*/
        #ifdef DEBUG_PRINT
        fprintf(stderr, "[%d]:%s - SYN-ACK pkt and this is a outbound pkt\n", __LINE__, __func__);
        #endif
        if(ntohl(tcp_hdr->th_ack) == (ntohl(conn->init_int_seqno) + 1))
        {
          /* valid ack no */
          conn->receive_SYN_ACK_int = true;
          conn->init_int_seqno = tcp_hdr->th_seq;
        }
        else
        {
        fprintf(stderr, "[ERROR]: when receiving SYN-ACK pkt, no matching ackno with previous seqno in previous SYN packets\n \ttcp_hdr->th_ack:%u - (conn->init_int_seqno + 1): %u \n", ntohl(tcp_hdr->th_ack), (ntohl(conn->init_int_seqno) + 1));
          return -1;
        }
      }
      else
      { /* SYN pkt */
        #ifdef DEBUG_PRINT
        fprintf(stderr, "[%d]:%s - SYN pkt and this is a outbound pkt\n", __LINE__, __func__);
        #endif
        conn->receive_SYN_int = true;
        if((difftime(curtime, conn->last_transitory) < TCP_TRANSITORY_UNSOLICITED) && 
        (conn->receive_SYN_ext == true) && (conn->state == state_transitory))
        {
          conn->receive_SYN_ext = false; /*discard outbound SYN */
          #ifdef DEBUG_PRINT
          fprintf(stderr, "[%d]:%s - discard outbound SYN \n", __LINE__, __func__);
          #endif
          return 0;
        }
      }
    }

    if(conn->state == state_established)
    {
      /* this SYN pkt use for inspect if the host die or not */
      conn->last_established = time(NULL); /* reset established idle timeout */
    }
    else
    {
      if(conn->receive_SYN_int && conn->receive_SYN_ext && 
      conn->receive_SYN_ACK_ext && conn->receive_SYN_ACK_int)
      {
        conn->state = state_established;
        conn->simultaneous_open = true;
        conn->last_established = time(NULL);
        #ifdef DEBUG_PRINT
        fprintf(stderr, "simultenous open !\n");
        #endif
        conn->simultaneous_open = true;
      }
    }
  }
  if(tcp_hdr->th_flags & TH_ACK)
  {
    if(direct == inbound)
    {
      /* inbound */
      if(conn->state == state_established)
      {
        /* this SYN pkt use for inspect if the host die or not */
        conn->last_established = time(NULL); /* reset established idle timeout */
        #ifdef DEBUG_PRINT
        fprintf(stderr, "[%d]:%s - Inbound pkt: reset established idle timeout \n", __LINE__, __func__);
        #endif
      }
      else
      {
        if(conn->receive_SYN_ext && conn->receive_SYN_ACK_int)
        {
          conn->state = state_established;
          conn->last_established = time(NULL); /* start established idle timeout */
          #ifdef DEBUG_PRINT
          fprintf(stderr, "[%d]:%s - Inbound pkt: start established idle timeout \n", __LINE__, __func__);
          #endif
        }
      }
    }
    else
    {
      /* outbound */
      if(conn->state == state_established)
      {
        /* this SYN pkt use for inspect if the host die or not */
        conn->last_established = time(NULL); /* reset established idle timeout */
        #ifdef DEBUG_PRINT
        fprintf(stderr, "[%d]:%s - Outbound pkt: reset established idle timeout \n", __LINE__, __func__);
        #endif
      }
      else
      {
        if(conn->receive_SYN_int && conn->receive_SYN_ACK_ext)
        {
          conn->state = state_established;
          conn->last_established = time(NULL); /* start established idle timeout */
          #ifdef DEBUG_PRINT
          fprintf(stderr, "[%d]:%s - Outbound pkt: start established idle timeout \n", __LINE__, __func__);
          #endif
        }
      }
    }
  }
  return 0;
}

/* Destroy the specific connection */
int sr_destroy_nat_tcpconnection(struct sr_nat *nat, struct sr_nat_mapping *mapping, struct sr_nat_connection *conn)
{
  pthread_mutex_lock(&(nat->lock));
  struct sr_nat_connection *conn_entry = NULL, *next_conn_entry = NULL, *prev_conn_entry = NULL;
  if((conn_entry = mapping->conns) == NULL)
  {
    fprintf(stderr, "no entry! - cannot delete\n");
    pthread_mutex_unlock(&(nat->lock));
    return 0;
  }
  if(conn)
  {
    for(conn_entry = mapping->conns; conn_entry != NULL; conn_entry = conn_entry->next)
    {
      if((conn_entry->int_conn_ip == conn->int_conn_ip) && (conn_entry->int_conn_port == conn->int_conn_port)
      && (conn_entry->ext_conn_ip == conn->ext_conn_ip) && (conn_entry->ext_conn_port == conn->ext_conn_port))
      {
        if(prev_conn_entry){
          next_conn_entry = conn_entry->next;
          prev_conn_entry->next = next_conn_entry;
        }
        else{
          next_conn_entry = conn_entry->next;
          mapping->conns = next_conn_entry;
        }
        break;
      }
    }
    free(conn_entry);
  }
  pthread_mutex_unlock(&(nat->lock));
  return 0;
}