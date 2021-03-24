/*-----------------------------------------------------------------------------
 * file:  sr_rt.c
 * date:  Mon Oct 07 04:02:12 PDT 2002
 * Author:  casado@stanford.edu
 *
 * Description:
 *
 *---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>


#include <sys/socket.h>
#include <netinet/in.h>
#define __USE_MISC 1 /* force linux to show inet_aton */
#include <arpa/inet.h>

#include "sr_rt.h"
#include "sr_router.h"
#include "sr_utils.h"

uint32_t ip_mask_all = 0xFFFFFFFF;

#define min(a, b) ((a) > (b) ? (b) : (a))
/*---------------------------------------------------------------------
 * Method:
 *
 *---------------------------------------------------------------------*/

int sr_load_rt(struct sr_instance* sr,const char* filename)
{
    FILE* fp;
    char  line[BUFSIZ];
    char  dest[32];
    char  gw[32];
    char  mask[32];
    char  iface[32];
    struct in_addr dest_addr;
    struct in_addr gw_addr;
    struct in_addr mask_addr;
    int clear_routing_table = 0;

    /* -- REQUIRES -- */
    assert(filename);
    if( access(filename,R_OK) != 0)
    {
        perror("access");
        return -1;
    }

    fp = fopen(filename,"r");

    while( fgets(line,BUFSIZ,fp) != 0)
    {
        sscanf(line,"%s %s %s %s",dest,gw,mask,iface);
        if(inet_aton(dest,&dest_addr) == 0)
        { 
            fprintf(stderr,
                    "Error loading routing table, cannot convert %s to valid IP\n",
                    dest);
            return -1; 
        }
        if(inet_aton(gw,&gw_addr) == 0)
        { 
            fprintf(stderr,
                    "Error loading routing table, cannot convert %s to valid IP\n",
                    gw);
            return -1; 
        }
        if(inet_aton(mask,&mask_addr) == 0)
        { 
            fprintf(stderr,
                    "Error loading routing table, cannot convert %s to valid IP\n",
                    mask);
            return -1; 
        }
        if( clear_routing_table == 0 ){
            printf("Loading routing table from server, clear local routing table.\n");
            sr->routing_table = 0;
            clear_routing_table = 1;
        }
        sr_add_rt_entry(sr,dest_addr,gw_addr,mask_addr,iface);
    } /* -- while -- */

    return 0; /* -- success -- */
} /* -- sr_load_rt -- */

/*---------------------------------------------------------------------
 * Method:
 *
 *---------------------------------------------------------------------*/

void sr_add_rt_entry(struct sr_instance* sr, struct in_addr dest,
struct in_addr gw, struct in_addr mask,char* if_name)
{
    struct sr_rt* rt_walker = 0;

    /* -- REQUIRES -- */
    assert(if_name);
    assert(sr);

    /* -- empty list special case -- */
    if(sr->routing_table == 0)
    {
        sr->routing_table = (struct sr_rt*)malloc(sizeof(struct sr_rt));
        assert(sr->routing_table);
        sr->routing_table->next = 0;
        sr->routing_table->dest = dest;
        sr->routing_table->gw   = gw;
        sr->routing_table->mask = mask;
        strncpy(sr->routing_table->interface,if_name,sr_IFACE_NAMELEN);

        return;
    }

    /* -- find the end of the list -- */
    rt_walker = sr->routing_table;
    while(rt_walker->next){
      rt_walker = rt_walker->next; 
    }

    rt_walker->next = (struct sr_rt*)malloc(sizeof(struct sr_rt));
    assert(rt_walker->next);
    rt_walker = rt_walker->next;

    rt_walker->next = 0;
    rt_walker->dest = dest;
    rt_walker->gw   = gw;
    rt_walker->mask = mask;
    strncpy(rt_walker->interface,if_name,sr_IFACE_NAMELEN);

} /* -- sr_add_entry -- */

/*---------------------------------------------------------------------
 * Method:
 *
 *---------------------------------------------------------------------*/

void sr_print_routing_table(struct sr_instance* sr)
{
    struct sr_rt* rt_walker = 0;

    if(sr->routing_table == 0)
    {
        printf(" *warning* Routing table empty \n");
        return;
    }

    printf("Destination\tGateway\t\tMask\tIface\n");

    rt_walker = sr->routing_table;
    
    sr_print_routing_entry(rt_walker);
    while(rt_walker->next)
    {
        rt_walker = rt_walker->next; 
        sr_print_routing_entry(rt_walker);
    }

} /* -- sr_print_routing_table -- */

/*---------------------------------------------------------------------
 * Method:
 *
 *---------------------------------------------------------------------*/

void sr_print_routing_entry(struct sr_rt* entry)
{
    /* -- REQUIRES --*/
    assert(entry);
    assert(entry->interface);

    printf("%s\t\t",inet_ntoa(entry->dest));
    printf("%s\t",inet_ntoa(entry->gw));
    printf("%s\t",inet_ntoa(entry->mask));
    printf("%s\n",entry->interface);

} /* -- sr_print_routing_entry -- */

/*  This is just simple LPM , reference method using binary search in lpm: https://github.com/Hohungduy/liblpm.git 
    Notice about argument:
    - dst_ip: network byte ordered (long)
*/
sr_rt_tt* sr_longest_prefix_match(struct sr_instance* sr, uint32_t des_ip)
{
    sr_rt_tt *current_entry = NULL;
    sr_rt_tt *next_entry = NULL;
    sr_rt_tt *match_entry = NULL;
    sr_rt_tt *default_entry = NULL;
    // unsigned int max = 0;
    uint32_t min_diff = 0;
    uint32_t diff = 0;
    uint32_t count_match = 0;

    uint32_t ip_subnet = 0;
    uint32_t ip_subnet_broadcast = 0;

    if((current_entry = sr->routing_table) == NULL)
        return current_entry;
    /* init first ip */

    for(current_entry = sr->routing_table; current_entry != NULL; current_entry = next_entry)
    {
        next_entry = current_entry->next;
        if((ntohl(current_entry->mask.s_addr) - ip_mask_all) == 0)
        {
            if(ntohl(current_entry->dest.s_addr) == ntohl(des_ip))
            {
                match_entry = current_entry;
                break;
            }
        }
        if(ntohl(current_entry->mask.s_addr) == 0)
        {
            default_entry = current_entry;
        }
    }
    if(match_entry)
        return match_entry;
    
    for(current_entry = sr->routing_table; current_entry != NULL; current_entry = next_entry)
    {
        next_entry = current_entry->next;
        if((ntohl(current_entry->mask.s_addr) < ip_mask_all) && (ntohl(current_entry->mask.s_addr) > 0))
        {
            ip_subnet = ntohl(current_entry->dest.s_addr) & ntohl(current_entry->mask.s_addr);
            ip_subnet_broadcast = ntohl(current_entry->dest.s_addr) | (ip_mask_all - ntohl(current_entry->mask.s_addr));
            if((ntohl(des_ip) > ip_subnet) && (ntohl(des_ip) < ip_subnet_broadcast))
            {
                diff = min((ntohl(des_ip) - ip_subnet), (ip_subnet_broadcast - ntohl(des_ip)));
            }
        }
        if(!count_match)
        {
            min_diff = diff;
            match_entry = current_entry;
            count_match++;
        }
        else
        {
            if(diff < min_diff)
            {
                min_diff = diff;
                match_entry = current_entry;
            }
            count_match++;
        }
    }
    if(min_diff)
        return match_entry;

    if(default_entry == NULL)
        return NULL;
    else
        return default_entry;
}
