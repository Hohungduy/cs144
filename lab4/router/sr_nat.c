
#include <signal.h>
#include <assert.h>
#include "sr_nat.h"
#include "sr_utils.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
static char *ext_ip_eth2 = "184.72.104.217";
static char *int_ip_eth1 = "10.0.1.1";
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
  struct sr_nat_mapping* mapping = NULL, *next_mapping =NULL;
  for(mapping = nat->mappings; mapping != NULL; mapping = next_mapping)
  {
    next_mapping = mapping->next;
    if(mapping)
      free(mapping);
  }
  pthread_kill(nat->thread, SIGKILL);
  return pthread_mutex_destroy(&(nat->lock)) &&
    pthread_mutexattr_destroy(&(nat->attr));

}

void *sr_nat_timeout(void *nat_ptr) {  /* Periodic Timout handling */
  struct sr_nat *nat = (struct sr_nat *)nat_ptr;
  while (1) {
    sleep(1.0);
    pthread_mutex_lock(&(nat->lock));

    time_t curtime = time(NULL);

    /* handle periodic tasks here */
    struct sr_nat_mapping *mapping_entry = NULL;
    if(!(mapping_entry = nat->mappings))
      return NULL; /* no entry in mapping table */
    for(mapping_entry = nat->mappings; mapping_entry != NULL; mapping_entry = mapping_entry->next)
    {
      if((mapping_entry->type == nat_mapping_icmp) && (mapping_entry->valid)&& (difftime(curtime, mapping_entry->last_updated) > ICMP_MAPPING_TIMEOUT))
      {
        mapping_entry->valid = false;
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
    return NULL; /* no entry */
  for(mapping_entry = nat->mappings; mapping_entry != NULL; mapping_entry = mapping_entry->next)
  {
    if((mapping_entry->aux_int == aux_ext) && (mapping_entry->valid = true))
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
    return NULL; /* no entry */
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
      fprintf(stderr,"[Error]: cannot convert %s to valid IP\n", ext_ip_eth2);
      exit(EXIT_FAILURE); 
    }
    mapping_entry->ip_ext = htonl(ext_addr.s_addr);/* network byte ordered */
    mapping_entry->aux_ext = htons(generate_random(1024, 65535));/* network byte ordered */
    mapping_entry->valid = true;
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
