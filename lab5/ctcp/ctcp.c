/******************************************************************************
 * ctcp.c
 * ------
 * Implementation of cTCP done here. This is the only file you need to change.
 * Look at the following files for references and useful functions:
 *   - ctcp.h: Headers for this file.
 *   - ctcp_iinked_list.h: Linked list functions for managing a linked list.
 *   - ctcp_sys.h: Connection-related structs and functions, cTCP segment
 *                 definition.
 *   - ctcp_utils.h: Checksum computation, getting the current time.
 *
 *****************************************************************************/

#include "ctcp.h"
#include "ctcp_linked_list.h"
#include "ctcp_sys.h"
#include "ctcp_utils.h"

/* MACRO for debug printf */
// #define DEBUG_PRINT
// #define DEBUG_PRINT2
/* state associated to list of transmit segment (unacked) */
typedef struct tx_state{
  bool read_EOF; /* read EOF from STDIN -> send FIN segment*/
  bool send_FIN_acked; /*sent FIN and receive ACK segment */
  uint32_t last_seqno_acked; /* pointer to ack number whose segment is the last sent and received ack. 
                             (Thus, pointer to segment that sent but have not received ack = last_ackno_rx+1)*/
  uint32_t last_seqno_tx; /* pointer to the last byte have been sent
                              Thus, pointer to next sequence number whose segment = last_seqno_tx + 1*/
  uint32_t last_seqno_read; /* pointer to the last byte have been read */
  /* As we can see from this value:
  - last_seqno_acked + 1 = base number of send window
  - last_seqno_tx + 1 = next sequence number that needs to be sent
  - last_seqno_read < base number of send window + window size;
  */
  linked_list_t *tx_segment;

}tx_state_t;

/* state associated to list of receive segment */
typedef struct rx_state{
  // bool write_eof; /* write EOF to STDOUT-> send FIN segment*/
  bool receive_FIN_seg; /* receive FIN segment from other hosts -> write EOF to STDOUT */
  uint32_t last_seqno_rep_acked;/* Use it to track the last byte that we acked it to the sender*/
  uint32_t checksum_failed_segment_count;
  uint32_t truncated_segment_count;
  uint32_t out_of_window_segment_count; /* can't handle this and need to apply a mothod to inform sender know
                                        receiver window */
  linked_list_t *rx_segment; /* Linked list of segments receive from this connection */
}rx_state_t;

typedef struct ctcp_sending_segment{
  int retransmit_segment; /* counter associated to the number of segment has been sent*/
  int time_lastsegment_sent; /* time that last segment sent*/
  ctcp_segment_t segment; /* Segment */

}ctcp_sending_segment_t;

/**
 * Connection state.
 *
 * Stores per-connection information such as the current sequence number,
 * unacknowledged packets, etc.
 *
 * You should add to this to store other fields you might need.
 */
struct ctcp_state {
  struct ctcp_state *next;  /* Next in linked list */
  struct ctcp_state **prev; /* Prev in linked list */

  conn_t *conn;             /* Connection object -- needed in order to figure
                               out destination when sending */

  /* FIXME: Add other needed fields. */
  rx_state_t rx_state;
  tx_state_t tx_state;
  ctcp_config_t ctcp_cfg; /* store configuration for this connection */
  long timeout_connection; /*Use it when we send the last ACK segment and turn into TIME_WAIT state */

};

/**
 * Linked list of connection states. Go through this in ctcp_timer() to
 * resubmit segments and tear down connections.
 */
static ctcp_state_t *state_list;

/* FIXME: Feel free to add as many helper functions as needed. Don't repeat
          code! Helper functions make the code clearer and cleaner. */


ctcp_state_t *ctcp_init(conn_t *conn, ctcp_config_t *cfg) {
  /* Connection could not be established. */
  if (conn == NULL) {
    return NULL;
  }

  /* Established a connection. Create a new state and update the linked list
     of connection states. */
  // Push to the top ( Add the latest linked list)
  ctcp_state_t *state = calloc(sizeof(ctcp_state_t), 1);
  state->next = state_list;
  state->prev = &state_list;
  if (state_list)
    state_list->prev = &state->next;
  state_list = state;

  /* Set fields. */
  state->conn = conn;
  state->timeout_connection = 0;
  /* FIXME: Do any other initialization here. */

  /* Initialize configuration for this connection*/
  state->ctcp_cfg.recv_window = cfg->recv_window;
  state->ctcp_cfg.send_window = cfg->send_window;
  state->ctcp_cfg.timer = cfg->timer;
  state->ctcp_cfg.rt_timeout = cfg->rt_timeout;

  /* Initialize the state of transmission attributes and linked list of data that needs to be sent */
  state->tx_state.read_EOF = false;
  state->tx_state.last_seqno_acked = 0;
  state->tx_state.last_seqno_read = 0;
  state->tx_state.last_seqno_tx = 0;
  state->tx_state.tx_segment = ll_create();

  /* Initialize the state of receiving attributes and linked list of data that have been receive and not yet acked (or acked failed) */
  state->rx_state.receive_FIN_seg = false;
  state->rx_state.last_seqno_rep_acked = 0;
  state->rx_state.checksum_failed_segment_count = 0;
  state->rx_state.truncated_segment_count = 0;
  state->rx_state.out_of_window_segment_count = 0;
  state->rx_state.rx_segment = ll_create();

  #ifdef DEBUG_PRINT
  fprintf(stderr, "state->ctcp_cfg.recv_window  : %d\n", state->ctcp_cfg.recv_window );
  fprintf(stderr, "state->ctcp_cfg.send_window  : %d\n", state->ctcp_cfg.send_window );
  fprintf(stderr, "state->ctcp_cfg.timer        : %d\n", state->ctcp_cfg.timer );
  fprintf(stderr, "state->ctcp_cfg.rt_timeout   : %d\n", state->ctcp_cfg.rt_timeout );
  #endif
  if(cfg)
    free(cfg);
  return state;
}

void ctcp_destroy(ctcp_state_t *state) {
  /* Update linked list -stack. */
  // Pop from the top (the latest state)
  #ifdef DEBUG_PRINT
  fprintf(stderr, "%d - %s\n", __LINE__,__func__);
  #endif
  if (state->next)
    state->next->prev = state->prev;

  *state->prev = state->next;
  conn_remove(state->conn);

  /* FIXME: Do any other cleanup here. */
  /* REMEMBER: Destroy linked list of the sending and receiving segments*/

  ll_node_t *current_node = NULL;
  ll_node_t *next_node = NULL;
  linked_list_t *list = NULL;

  /* Remove linked list of sending segments: tx_state_t tx_state.unacked_send_segment
     - Firstly, we need to destroy object in node
     - Secondly, delete list or remove node respectively associated to object
     and remove after that
  */ 
  list = state->tx_state.tx_segment;
  if(list == NULL)
    goto end_state;
  for((current_node = list->head); current_node != NULL; current_node = next_node)
  {
    /* free memory of object in node */
    if(current_node->object)
      free(current_node->object);
    next_node = current_node->next; // may be use list->head instead
    ll_remove(list,current_node);
  }
  ll_destroy(list);

  /* Remove linked list of receiving segments: rx_state_t rx_state.segment
    - Firstly, we need to destroy object in node
    - Secondly, delete list or remove node respectively associated to object
    and remove after that
  */

  list = state->rx_state.rx_segment;
  if(list == NULL)
    goto end_state;
  for((current_node = list->head); current_node != NULL; current_node = next_node)
  {
    /* free memory of object in node */
    if(current_node->object)
      free(current_node->object);
    next_node = current_node->next; // may be use list->head instead
    ll_remove(list,current_node);
  }
  ll_destroy(list);

end_state:
  if(state)
    free(state);
  end_client();
}

////////////////////////////HELPER FUNCTION//////////////////////////////

/* sending multiple segment to the connected host */
void ctcp_send_multiple_segment(ctcp_state_t *state);

/* sending segment to the connected host */
inline int ctcp_send_segment(ctcp_state_t *state, ctcp_sending_segment_t *sending_segment, size_t len);

/* inform sender the receiver window size */
inline void ctcp_send_ACK_segment(ctcp_state_t *state); 

/* Function associated to read, send, receive, output and timer */
void ctcp_read(ctcp_state_t *state) {
  /* FIXME */
  char tmp_buf[MAX_SEG_DATA_SIZE];/* statically allocate in stack/ maybe use with dynamic allocate */
  int32_t byte_read;
  ctcp_sending_segment_t *new_send_segment;/* include headers and data (variable string) */

  if (state == NULL)
    return;

  while( (byte_read = conn_input(state->conn, tmp_buf, MAX_SEG_DATA_SIZE )) > 0)
  {
    /* Read it until data is still available for reading (byte_read > 0)*/
    #ifdef DEBUG_PRINT
    fprintf(stderr, "%d - %s\n", __LINE__,__func__);
    #endif

    new_send_segment = (ctcp_sending_segment_t *)calloc(1, sizeof(ctcp_sending_segment_t) + byte_read);
    if(new_send_segment == NULL)
    {
      #ifdef DEBUG_PRINT
      // perror("Allocate memory\n");
      fprintf(stderr,"%d-%s: Allocate memory\n",__LINE__,__func__);
      #endif
      exit(EXIT_FAILURE);
    }
    /* Initialize the new send segment  */
    /* Init retransmit count*/
    new_send_segment->retransmit_segment = 0;
    /* init sequence number */
    new_send_segment->segment.seqno = htonl(state->tx_state.last_seqno_read + 1);
    new_send_segment->segment.flags = TH_ACK; /* fix correct header , BUG: 7->10, 12*/
    /* init length */
    new_send_segment->segment.len = htons((uint16_t)(sizeof(ctcp_segment_t) + (int16_t)byte_read)); //2 bytes
    #ifdef DEBUG_PRINT2
    fprintf(stderr, "%d - %s, len:%ld - byte_read:%d \n", __LINE__, __func__, (long)(new_send_segment->segment.len), (int32_t)byte_read);
    #endif
    /* copy from tmp_buf to segment buf */
    memcpy(new_send_segment->segment.data, tmp_buf, byte_read);
    /* update pointer to the last byte read */
    state->tx_state.last_seqno_read += byte_read;
    /* Adding this new send segment into linked list */
    ll_add(state->tx_state.tx_segment, new_send_segment);
  }

  /* Reading an EOF -> send FIN to the other side*/
  if(-1 == byte_read)
  {
    #ifdef DEBUG_PRINT2
    fprintf(stderr, "%d - %s - byte_read:%d \n", __LINE__,__func__, (int32_t)byte_read);
    #endif
    state->tx_state.read_EOF = true;
    /* Create FIN segment*/
    new_send_segment = (ctcp_sending_segment_t *)calloc(1, sizeof(ctcp_sending_segment_t));
    /* init retransmit segment */
    new_send_segment->retransmit_segment = 0;
    /* init length */
    new_send_segment->segment.len = htons((uint16_t)(sizeof(ctcp_segment_t)));
    /* init seq_number */
    new_send_segment->segment.seqno = htonl(state->tx_state.last_seqno_read + 1);
    new_send_segment->segment.flags = TH_FIN | TH_ACK; /* fix correct header , BUG: 7->10, 12*/

    ll_add(state->tx_state.tx_segment, new_send_segment);
  }
  /* sending segment */
  #ifdef DEBUG_PRINT
    fprintf(stderr, "%d - %s\n", __LINE__,__func__);
    #endif

  ctcp_send_multiple_segment(state);
}

void ctcp_send_multiple_segment(ctcp_state_t *state) {
  ll_node_t *current_node = NULL;
  ll_node_t *next_node = NULL;
  linked_list_t *sender_list = NULL;
  ctcp_sending_segment_t *send_segment = NULL;
  int ret = 0;
  // uint32_t send_seqno;
  uint16_t send_window;

  if (state == NULL)
    return;

  sender_list = state->tx_state.tx_segment;
  if(ll_length(sender_list) == 0)
  {
    #ifdef DEBUG_PRINT
    fprintf(stderr, "%d - %s\n", __LINE__,__func__);
    #endif
    return;
  }
  current_node = ll_front(sender_list);//init
  next_node = current_node->next;//init
  send_segment = (ctcp_sending_segment_t *)current_node->object;

  for((current_node = ll_front(sender_list)); ntohl(send_segment->segment.seqno) <= (state->tx_state.last_seqno_read + 1); current_node = next_node)
  {

    if(((send_segment = (ctcp_sending_segment_t *)current_node->object)->segment.flags) & TH_FIN)
    {
      /* FIN segment */
      #ifdef DEBUG_PRINT
      fprintf(stderr, "%d - %s\n", __LINE__,__func__);
      #endif
      /* set up sending segment */
      send_segment->retransmit_segment = 0;
      send_segment->segment.ackno = htonl(state->rx_state.last_seqno_rep_acked + 1);
      send_segment->segment.window = htons(state->ctcp_cfg.recv_window);
      // send_segment->segment.flags |= TH_ACK; /* fix correct header , BUG: 7->10, 12*/
      send_segment->segment.cksum = 0;
      send_segment->segment.cksum = cksum(&send_segment->segment,ntohs(send_segment->segment.len));
      send_segment->time_lastsegment_sent = (int)current_time();

      #ifdef DEBUG_PRINT2
      fprintf(stderr," %d - %s --------------: Sending FIN\n\n\n",__LINE__, __func__);
      print_hdr_ctcp(&send_segment->segment);
      #endif
      /*sending ACK segments*/
      ret = ctcp_send_segment(state,send_segment,send_segment->segment.len);
      if(0 == ret)
      {
        /*update the last seq sent pointer */
        state->tx_state.last_seqno_tx += 1;/* add datalen (1 byte vs FIN)*/
      }
      else if (ret < (int)ntohs(send_segment->segment.len))
      {
        return;
      }
      state->tx_state.send_FIN_acked = true;
    }
    else{
      /* Getting sending window size */
      #ifdef DEBUG_PRINT2
      fprintf(stderr, "%d - %s, last_seqno_tx:%lu , last_seq_read:%lu, last_seqno_acked:%lu, send_window:%hu\n", __LINE__,__func__, 
      (long unsigned int )state->tx_state.last_seqno_tx, (long unsigned int)state->tx_state.last_seqno_read, (long unsigned int )state->tx_state.last_seqno_acked,(unsigned short int)send_window);
      #endif
      send_segment->retransmit_segment = 0;
      send_window = state->ctcp_cfg.send_window;
      if(state->tx_state.last_seqno_tx > (state->tx_state.last_seqno_acked + send_window ))
      {
        if((send_segment = (ctcp_sending_segment_t *)current_node->object)->segment.seqno > (state->tx_state.last_seqno_acked + send_window ))
          break;
      }
      /* set up sending segment */
      send_segment->segment.window = htons(state->ctcp_cfg.recv_window);
      send_segment->segment.ackno = htonl(state->rx_state.last_seqno_rep_acked + 1);
      // send_segment->segment.flags |= TH_ACK;
      send_segment->segment.cksum = 0;
      send_segment->segment.cksum = cksum(&send_segment->segment,ntohs(send_segment->segment.len));
      send_segment->time_lastsegment_sent = 0;
      send_segment->time_lastsegment_sent = (int)current_time();

      #ifdef DEBUG_PRINT2
      fprintf(stderr,"%d - %s ++++++++++++++: Sending data segment (time_lastsegment_send = %d)!!!\n\n\n",__LINE__,__func__, (int)send_segment->time_lastsegment_sent);
      print_hdr_ctcp(&send_segment->segment);
      #endif

      /*sending*/
      ret = ctcp_send_segment(state,send_segment,send_segment->segment.len);
      if(0 == ret)
      {
        #ifdef DEBUG_PRINT2
        fprintf(stderr, "%d - %s, last_seqno_tx:%lu , last_seq_read:%lu, last_seqno_acked:%lu, send_window:%hu, segment.len:%hu, sizeof(ctcp_segent_t):%hu\n", __LINE__,__func__, 
        (long unsigned int )state->tx_state.last_seqno_tx, (long unsigned int)state->tx_state.last_seqno_read, (long unsigned int )state->tx_state.last_seqno_acked,(unsigned short int)send_window,
        send_segment->segment.len, (unsigned short int)(sizeof(ctcp_segment_t)));
        #endif
        /*update the last seq sent pointer */
        state->tx_state.last_seqno_tx += (uint32_t)(ntohs(send_segment->segment.len) - (uint16_t)(sizeof(ctcp_segment_t)));/* add datalen */
        #ifdef DEBUG_PRINT2
        fprintf(stderr, "%d - %s, last_seqno_tx:%lu , last_seq_read:%lu, last_seqno_acked:%lu, send_window:%hu , segment.len:%hu, sizeof(ctcp_segent_t):%hu\n", __LINE__,__func__, 
        (long unsigned int )state->tx_state.last_seqno_tx, (long unsigned int)state->tx_state.last_seqno_read, (long unsigned int )state->tx_state.last_seqno_acked,(unsigned short int)send_window,
        send_segment->segment.len, (unsigned short int)(sizeof(ctcp_segment_t)));
        #endif
      }
      else if (ret < (int)ntohs(send_segment->segment.len))
        return;
    }
    next_node = current_node->next;
    if(next_node == NULL)
      break;
    send_segment = (ctcp_sending_segment_t *)next_node->object;
  }
}
int ctcp_send_segment(ctcp_state_t *state, ctcp_sending_segment_t *sending_segment, size_t len)
{
  conn_t *conn = state->conn;
  int32_t byte_send = 0;
  int32_t tot_left = (int32_t)len;
  const char *tmp_buf;

  tmp_buf = sending_segment->segment.data;

  while(tot_left > 0)
  {
    byte_send = conn_send(conn,&sending_segment->segment,ntohs(tot_left));

    if(-1 == byte_send)
    {
      #ifdef DEBUG_PRINT
      fprintf(stderr,"%d - %s: conn_send() failed\n",__LINE__,__func__);
      #endif
      if(state)
        ctcp_destroy(state);
      return -1;
      // exit(EXIT_FAILURE);
    }
    if(((int32_t)ntohs(len)) == byte_send)
    {
      #ifdef DEBUG_PRINT2
      fprintf(stderr,"%d - %s: send successfully at the first time\n",__LINE__,__func__);
      #endif
      tot_left = 0;
      return 0;
    }
    #ifdef DEBUG_PRINT2
    fprintf(stderr,"%d - %s: send_continue: byte_send: %d\n",__LINE__,__func__, (int32_t)byte_send);
    #endif
    sending_segment->segment.seqno = htonl((uint32_t)(ntohl(sending_segment->segment.seqno)) + byte_send);
    sending_segment->segment.len = htons((uint16_t)(ntohs(sending_segment->segment.len)) - (int16_t)byte_send);
    tmp_buf += byte_send;
    tot_left = htons((int32_t)ntohs(len) - byte_send);
    memcpy(sending_segment->segment.data, tmp_buf, ntohs(tot_left));
    sending_segment->segment.cksum = 0;
    sending_segment->segment.cksum = cksum(&sending_segment->segment,ntohs(sending_segment->segment.len));
  }
  return ntohs(tot_left);
}
/* sending ACK packet */
void ctcp_send_ACK_segment(ctcp_state_t *state){
  /* Do this when there is no longer available space on receiver */
  conn_t *conn = state->conn;
  ctcp_segment_t segment;

  segment.seqno = htonl(0);// ignore sequence number 
  segment.ackno = htonl(state->rx_state.last_seqno_rep_acked + 1);
  segment.len = htons((uint16_t)(sizeof(ctcp_segment_t)));
  segment.flags |= TH_ACK;
  segment.window = (uint16_t)htons(state->ctcp_cfg.recv_window);
  segment.cksum = 0;
  segment.cksum = cksum(&segment,ntohs(segment.len));
  conn_send(conn, &segment, ntohs(segment.len));
  #ifdef DEBUG_PRINT2
  fprintf(stderr,"%d - %s ^^^^^^^^^^^^^^: Sending ACK segment !!!\n\n\n",__LINE__,__func__);
  #endif
}     

void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
  /* FIXME */
  /* REMEMBER: Free segment after using it */
  /* Receiving segment */
  uint16_t rcv_segment_len = ntohs(segment->len);
  uint16_t correct_rcv_segment_cksum = 0;
  uint16_t computed_rc_segment_cksum = 0;
  uint32_t rcv_segment_seqno = ntohl(segment->seqno);
  uint32_t rcv_segment_ackno = ntohl(segment->ackno);
  uint32_t rcv_segment_flag = segment->flags;
  uint32_t upperbound_recv_window = state->rx_state.last_seqno_rep_acked + state->ctcp_cfg.recv_window;
  uint16_t datalen = rcv_segment_len - (uint16_t)(sizeof(ctcp_segment_t));

  ll_node_t *current_node = NULL; // pointer node use for loop
  ll_node_t *next_node = NULL;//pointer node use for loop

  linked_list_t *send_list = NULL;// Sending list
  ctcp_sending_segment_t *send_segment = NULL;// Sending segment in sending list

  linked_list_t *receive_list = NULL; // Receving list
  unsigned int ll_len = 0; // Length of receiving list
  ctcp_segment_t *segment_ptr = NULL; // segment pointer to receving segment
  ll_node_t *first_node = NULL; // first receiving node in receiving list
  ctcp_segment_t *first_segment = NULL;// first receiving segment in receiving list
  if (state == NULL)
    return;
  /*----------------------------------------------------------------------------------------------------*/
  /* Checking whether this segment is truncated segment */
  if(len < rcv_segment_len)
  {
    #ifdef DEBUG_PRINT2
    fprintf(stderr,"%d - %s: discard truncated segments - len:%d, rcv_segment_len:%d\n",__LINE__,__func__, (int)len, (int)rcv_segment_len);
    #endif
    state->rx_state.truncated_segment_count++;
    if(segment)
      free(segment);
    return;
  }
  /*----------------------------------------------------------------------------------------------------*/
  /* Checking if checksum value of segment is correct */
  correct_rcv_segment_cksum = segment->cksum;
  segment->cksum = 0;
  computed_rc_segment_cksum = cksum(segment,rcv_segment_len);
  segment->cksum = correct_rcv_segment_cksum;
  if (computed_rc_segment_cksum != correct_rcv_segment_cksum)
  {
    #ifdef DEBUG_PRINT2
    fprintf(stderr,"%d - %s: discard wrong segments when we checksum, computed_segment_cksum:%x, correct_recv_cksum:%x\n",__LINE__,__func__,(int)computed_rc_segment_cksum,(int)correct_rcv_segment_cksum);
    #endif
    state->rx_state.checksum_failed_segment_count++;
    if(segment)
      free(segment);
    return;
  }
  /*----------------------------------------------------------------------------------------------------*/
  /* Checking if this segment is out of receiving window bound */
  if(rcv_segment_seqno > upperbound_recv_window)
  {
    #ifdef DEBUG_PRINT2
    fprintf(stderr,"%d - %s: discard segments when we reach the limmit of receving window\n",__LINE__,__func__);
    #endif
    state->rx_state.out_of_window_segment_count++;
    /* sending control message to inform sender know the current receiver window */
    ctcp_send_ACK_segment(state);
    if(segment)
      free(segment);
    return;
  }
  /*----------------------------------------------------------------------------------------------------*/
  /* Print valid message */
  #ifdef DEBUG_PRINT2

  if (rcv_segment_flag & TH_FIN)
    fprintf(stderr,"%d - %s --------------: Receiving FIN (Valid message)!!!\n\n\n",__LINE__,__func__);
  else if((rcv_segment_flag & TH_ACK) && !datalen)
    fprintf(stderr,"%d - %s ^^^^^^^^^^^^^^: Receiving ACK (Valid message)!!!\n\n\n",__LINE__,__func__);
  else
    fprintf(stderr,"%d - %s ++++++++++++++: Receiving data segment (Valid message)!!!\n\n\n",__LINE__,__func__);
  print_hdr_ctcp(segment);
  #endif
  /*----------------------------------------------------------------------------------------------------*/  
  /* Checking if FIN flag is set */
  if((rcv_segment_flag & TH_FIN)&&(rcv_segment_flag & TH_ACK))
  {
    #ifdef DEBUG_PRINT2
    fprintf(stderr,"%d - %s: FIN-ACK flag is set\n",__LINE__,__func__);
    #endif
    /* Proccessing FIN/ACK. To do:
    - Remove all segment which has seqno < ackno in sending list
    - increase last_seqno_rep_acked by 1 bytes (do in ctcp_output())
    - Send ACK with ackno greater than 1 bytes of the FIN segment (we have already increased last_seqno_rep_acked in step 1) (do in ctcp_output())
    */
    send_list = state->tx_state.tx_segment;
    if(ll_front(send_list) == NULL)
    {
      goto add_FIN_ACK_seg;
    }
    current_node = ll_front(send_list);
    send_segment = (ctcp_sending_segment_t *)(current_node->object);
    /* Remove tx_segments have been acked */
    for(current_node = ll_front(send_list); ntohl(send_segment->segment.seqno) < rcv_segment_ackno; current_node = next_node ){
      next_node = current_node->next;
      state->tx_state.last_seqno_acked += (uint32_t)(ntohs(send_segment->segment.len) - (uint16_t)sizeof(ctcp_segment_t));
      if(send_segment)
        free(send_segment);
      ll_remove(send_list, current_node);
      if(next_node == NULL)
        break;
      send_segment = (ctcp_sending_segment_t *)(next_node->object);
    }
  add_FIN_ACK_seg:
    /* Update FIN state */
    state->rx_state.receive_FIN_seg = true;
    /* Add to receiving list */
    ll_add(state->rx_state.rx_segment,segment); /* Using conn_ouput with length = 0 in ctcp_output() to process this segment(send EOF) to STDOUT)*/
    /* Only FIN/ ACK, no data */
    goto end;
  }
  else if (rcv_segment_flag & TH_FIN){
    /* Processing receiving FIN segment. To do:
      - Increase last_seqno_rep_acked by 1 bytes (do in ctcp_output)
      - Send ACK with ackno greater than 1 bytes of the FIN segment (we have already increased last_seqno_rep_acked in step 1)
    */
    #ifdef DEBUG_PRINT2
    fprintf(stderr,"%d - %s: FIN flag is set\n",__LINE__,__func__);
    #endif
    /* Update FIN state */
    state->rx_state.receive_FIN_seg = true;
    /* Add to receiving list */
    ll_add(state->rx_state.rx_segment,segment); /* Using conn_ouput with length = 0 in ctcp_output() to process this segment(send EOF) to STDOUT)*/
    goto end;
  }
  
  /* Checking if only ACK flag is set */
  if(rcv_segment_flag & TH_ACK)
  {
    #ifdef DEBUG_PRINT2
    fprintf(stderr,"%d - %s: ACK flag is set\n",__LINE__,__func__);
    #endif
    /* Proccessing normal ACK. To do:
    - increase last_seqno_ackno ->increase base of sending window 
    - Remove the segment with the specific sequence number from the sending list 
    , provided that the sequence number < acknowlegde number of this acknowlegde segment
    */
    // Check and remove the segment acked from the sending list
    send_list = state->tx_state.tx_segment;
    if(ll_front(send_list) == NULL)
    {
      #ifdef DEBUG_PRINT2
      fprintf(stderr,"%d - %s: ACK flag is set\n",__LINE__,__func__);
      #endif
      goto check_len_ack;
    }
    current_node = ll_front(send_list);
    send_segment = (ctcp_sending_segment_t *)(current_node->object);
    /* Remove tx_segments have been acked */

    for(current_node = ll_front(send_list); ntohl(send_segment->segment.seqno) < rcv_segment_ackno; current_node = next_node ){
      #ifdef DEBUG_PRINT2
      fprintf(stderr,"%d - %s: ACK flag is set\n",__LINE__,__func__);
      #endif
      state->tx_state.last_seqno_acked += (uint32_t)(ntohs(send_segment->segment.len) - (uint16_t)(sizeof(ctcp_segment_t)));
      next_node = current_node->next;
      if(send_segment)
        free(send_segment);
      ll_remove(send_list, current_node);
      if(next_node == NULL)
        break;
      send_segment = (ctcp_sending_segment_t *)(next_node->object);
    }
  check_len_ack:
    if((datalen) == 0)
    {
      /* Only ACK, no data */
      #ifdef DEBUG_PRINT2
      fprintf(stderr,"%d - %s: ACK flag is set, last_seqno_acked:%lu, send_window:%u \n",__LINE__,__func__, (long unsigned int)state->tx_state.last_seqno_acked, (unsigned int)state->ctcp_cfg.send_window);
      #endif
      if(segment)
        free(segment);
      return;
    }
    /* Piggybagged: Having data (rcv_segment_len =ntohs(segment->len)) -> continue*/
  }
// data_process:
  receive_list = state->rx_state.rx_segment;
  ll_len = ll_length(receive_list);
  
  if(rcv_segment_seqno <= state->rx_state.last_seqno_rep_acked)
  {
    #ifdef DEBUG_PRINT2
    fprintf(stderr, "%d - %s\n", __LINE__,__func__);
    #endif
    if(segment)
      free(segment);/*duplicate data, discard this segment */
    return;
  }
  if(ll_len == 0)
  {
    #ifdef DEBUG_PRINT2
    fprintf(stderr, "%d - %s\n", __LINE__,__func__);
    #endif
    ll_add(receive_list, segment);
    /* Update receive window */
    state->ctcp_cfg.recv_window -= datalen;
    goto end;
  }
  //ll_len > 0
  first_node = ll_front(receive_list);
  first_segment = (ctcp_segment_t *)(first_node->object);
 
  if(rcv_segment_seqno == (state->rx_state.last_seqno_rep_acked + 1))
  {
    /* Adding this segment behind the right ordered acked segment in receiving */
    if(ntohl(first_segment->seqno) == (state->rx_state.last_seqno_rep_acked + 1))
    {
      #ifdef DEBUG_PRINT2
      fprintf(stderr, "%d - %s\n", __LINE__,__func__);
      #endif
      //discard segment because first segment has the seqno = state->rx_state.last_seqno_rep_acked + 1, the first segment is similar to the receiving segment
      if(segment)
        free(segment);/*duplicate data, discard this segment */
      return;
    }
    else
    {
      #ifdef DEBUG_PRINT2
      fprintf(stderr, "%d - %s\n", __LINE__,__func__);
      #endif
      current_node = NULL;
      next_node = NULL;
      for(current_node = first_node; ntohl((segment_ptr = (ctcp_segment_t *)(current_node->object))->seqno) >= (state->rx_state.last_seqno_rep_acked+1); current_node = next_node)
      {
        #ifdef DEBUG_PRINT2
        fprintf(stderr, "%d - %s\n", __LINE__,__func__);
        #endif
        next_node = current_node->next;
        if(next_node == NULL)
          break;
      }
      segment_ptr = (ctcp_segment_t *)(current_node->object);
      if(ntohl(segment_ptr->seqno) == (state->rx_state.last_seqno_rep_acked + 1))
      {
        //discard segment
        #ifdef DEBUG_PRINT2
        fprintf(stderr, "%d - %s\n", __LINE__,__func__);
        #endif
        if(segment)
          free(segment);/*duplicate data, discard this segment */
        return;
      }
      else{
        if(current_node == first_node)
        {
          #ifdef DEBUG_PRINT2
          fprintf(stderr, "%d - %s\n", __LINE__,__func__);
          #endif
          ll_add_front(receive_list, segment);
          state->ctcp_cfg.recv_window -= datalen;
          goto end;
        }
        else{
          #ifdef DEBUG_PRINT2
          fprintf(stderr, "%d - %s\n", __LINE__,__func__);
          #endif
          current_node = current_node->prev;
          ll_add_after(receive_list, current_node, segment);
          state->ctcp_cfg.recv_window -= datalen;
          goto end;
        }
      }
    }
  }
  else
  {
    /* Adding this segment behind the right ordered acked segment in receiving */
    current_node = NULL;
    next_node = NULL;
    for(current_node = first_node; ntohl((segment_ptr = (ctcp_segment_t *)(current_node->object))->seqno) >= (rcv_segment_seqno); current_node = next_node)
    {
      #ifdef DEBUG_PRINT2
      fprintf(stderr, "%d - %s\n", __LINE__,__func__);
      #endif
      next_node = current_node->next;
      if(next_node == NULL)
        break;
    }
    segment_ptr = (ctcp_segment_t *)(current_node->object);
    if(ntohl(segment_ptr->seqno) == (rcv_segment_seqno))
    {
      //discard segment
      #ifdef DEBUG_PRINT2
      fprintf(stderr, "%d - %s\n", __LINE__,__func__);
      #endif
      if(segment)
        free(segment);
      return;
    }
    else
    {
      if(current_node == first_node)
      {
        #ifdef DEBUG_PRINT2
        fprintf(stderr, "%d - %s\n", __LINE__,__func__);
        #endif
        ll_add_front(receive_list, segment);
        /* Update receive window */
        state->ctcp_cfg.recv_window -= datalen;
        goto end;
      }
      else{
        #ifdef DEBUG_PRINT2
        fprintf(stderr, "%d - %s\n", __LINE__,__func__);
        #endif
      }
      current_node = current_node->prev;
      ll_add_after(receive_list, current_node, segment);
      /* Update receive window */
      state->ctcp_cfg.recv_window -= datalen;
    }
    // return;
  }
end:
  ctcp_output(state);
}

void ctcp_output(ctcp_state_t *state) {
  /* FIXME */
  ll_node_t* first_node;
  ctcp_segment_t* segment_ptr;
  size_t bufspace;
  int32_t datalen; // same as int (signed)
  int32_t byte_output;// same as int (singed)
  int num_segments_output = 0;

  // ctcp_sending_segment_t *final_FIN_segment = NULL;
  if (state == NULL)
    return;

  while (ll_length(state->rx_state.rx_segment) != 0) {
    #ifdef DEBUG_PRINT
    fprintf(stderr, "%d - %s\n", __LINE__,__func__);
    #endif
    // Get the segment we're going to try to output.
    first_node = ll_front(state->rx_state.rx_segment);
    segment_ptr = (ctcp_segment_t*) first_node->object;
    datalen = (int32_t)(ntohs(segment_ptr->len) - (uint16_t)(sizeof(ctcp_segment_t)));
    // Output any data in this segment.
    if (datalen) {

      /*Check the segment's sequence number. There might be a hole in
      segments_to_output, in which case we should give up. */
      if ( ntohl(segment_ptr->seqno) != (state->rx_state.last_seqno_rep_acked + 1))
      {
        #ifdef DEBUG_PRINT
        fprintf(stderr, "%d - %s, segment_ptr->seqno: %ld, state->rx_state.last_seqno_rep_acked: %ld\n", __LINE__,__func__, (long)ntohl(segment_ptr->seqno), (long)state->rx_state.last_seqno_rep_acked);
        #endif
        return;
      }

      /* Check if there is enough bufspace right now to output.*/
      bufspace = conn_bufspace(state->conn);
      if (((int32_t)bufspace) < datalen) {
        /*can't send right now, send control segment to inform sender know the situation of receiver buffer.*/
        #ifdef DEBUG_PRINT2
        fprintf(stderr,"%d - %s\n",__LINE__,__func__);
        #endif
        goto end;
      }

      byte_output = conn_output(state->conn, segment_ptr->data, (size_t)datalen);
      #ifdef DEBUG_PRINT2
      fprintf(stderr,"%d - %s >>>>>>>>>>>>>>>>>>>>:Output segments (byte_output =%d)\n",__LINE__,__func__, byte_output);
      #endif
      if (byte_output == -1) {
        #ifdef DEBUG_PRINT
        fprintf(stderr, "conn_output() returned -1\n");
        #endif
        if(state)
          ctcp_destroy(state);
        return;
      }
      assert(byte_output == datalen);
      num_segments_output++;
      /* update rx_state.last_seqno_rep_acked */
      state->rx_state.last_seqno_rep_acked += datalen;
      /* Update receive window */
      state->ctcp_cfg.recv_window += datalen;
    }
    /*
    * If you receive a FIN segment, you should output an EOF by calling
    * conn_output() with a length of 0. Then, you will need to destroy any
    * connection state once the conditions are satisfied (see ctcp_destroy()).
    */
    if (state->rx_state.receive_FIN_seg) 
    {
      #ifdef DEBUG_PRINT2
      fprintf(stderr, "received FIN, incrementing state->rx_state.last_seqno_rep_acked\n");
      #endif
      state->rx_state.last_seqno_rep_acked++;
      conn_output(state->conn, segment_ptr->data, 0);
      num_segments_output++;
    }
    /* remove it from the linked list after successfully output*/
    if(segment_ptr)
      free(segment_ptr); 
    ll_remove(state->rx_state.rx_segment, first_node);
  }
end:

  if (num_segments_output) {
    // Send an ack. Acking here (instead of in ctcp_receive) flow controls the
    // sender until buffer space is available.
    ctcp_send_ACK_segment(state);
  }
  return;

}

void ctcp_timer() {
  /* FIXME */
  /* Task of this function:
  - Inspect segments and retransmit ones that have not been acked. Check timestamp and rt_timeout. If timeout
  occurs, increase retransmission counter in segments
  - After 5 retransmission attempts (a total of 6 times ) for segments -> call ctcp_destroy().

  Remember to retrieve segment data from state_list pointer (current state) and check retransmission count
  */
  if (state_list == NULL) return;

  // ctcp_state_t *state = state_list; /* current state */
  linked_list_t *sending_list = NULL;/* sending list */
  linked_list_t *receiving_list = NULL;/* receiving list */
  ll_node_t *current_node = NULL;
  ll_node_t *next_node = NULL;
  ctcp_sending_segment_t *send_segment = NULL;

  ctcp_state_t *current_state = NULL;
  ctcp_state_t *next_state = NULL;

  for(current_state = state_list; current_state != NULL; current_state = next_state)
  {
    sending_list = current_state->tx_state.tx_segment;/* sending list */
    receiving_list = current_state->rx_state.rx_segment;/* receiving list */
    /* Work with tear down */
    ctcp_read(current_state);
    ctcp_output(current_state);

    /**
    * Destroys connection state for a connection. You should call this when all of
    * the following hold:
    *    - You have received a FIN from the other side.
    *    - You have read an EOF or error from your input (conn_input returned -1)
    *      and have sent a FIN to the other side.
    *    - All sent segments (including the FIN) have been acknowledged.
    *    - All received segments have been outputted.
    * Or:
    *    - The other side is unresponsive (after retransmitting the same segment 5
    *      times and still receiving no response).
    */
    if((current_state->tx_state.send_FIN_acked || current_state->tx_state.read_EOF) 
    && (current_state->rx_state.receive_FIN_seg) && (ll_length(sending_list) == 0) && (ll_length(receiving_list) == 0))
    {
        /* Checking some condition related to the termination of connection */
        #ifdef DEBUG_PRINT
         fprintf(stderr, "%d - %s\n", __LINE__,__func__);
        #endif
        if(current_state->timeout_connection == 0)
        {
          current_state->timeout_connection = current_time();
        }
        if((current_time() - current_state->timeout_connection) > (2 * MAX_SEG_LIFETIME))
        {
          #ifdef DEBUG_PRINT2
          fprintf(stderr,"%d - %s: Destroy connection\n",__LINE__, __func__);
          #endif
          if(current_state)
            ctcp_destroy(current_state);
          return;
        }
    }
    #ifdef DEBUG_PRINT
     fprintf(stderr, "%d - %s\n", __LINE__,__func__);
    #endif
    /* checking timestamp in each sending node and retransmit it if retransmit < 5. */
    if((current_node = ll_front(sending_list)) == NULL)
      return;

    for((current_node = ll_front(sending_list)); current_node != NULL; current_node = next_node)
    {
      #ifdef DEBUG_PRINT
      fprintf(stderr, "%d - %s\n", __LINE__,__func__);
      #endif
      send_segment = (ctcp_sending_segment_t *)(current_node->object);
      if(send_segment->segment.flags & TH_ACK)
      {
        goto nextnode;
      }
      if(((int)current_time() - (send_segment->time_lastsegment_sent)) > (current_state->ctcp_cfg.rt_timeout))
      {
        #ifdef DEBUG_PRINT2
        fprintf(stderr,"%d - %s: retransmit a segment, time:%d , time_lastsegment_sent:%d\n",__LINE__, __func__, ((int)current_time() - (send_segment->time_lastsegment_sent)), (int)(send_segment->time_lastsegment_sent));
        #endif
        send_segment->retransmit_segment++;
        /* retransmit a missing segment */
        ctcp_send_segment(current_state, current_node->object, send_segment->segment.len);
      }
      if(send_segment->retransmit_segment > 5)
      {
        #ifdef DEBUG_PRINT2
        fprintf(stderr,"%d - %s: destroy connection (retransmit_segment > 5)\n",__LINE__, __func__);
        #endif
        if(current_state)
          ctcp_destroy(current_state);
        return;
      }
nextnode:
      next_node = current_node->next;
      if(next_node == NULL)
        break;
    }
    next_state = current_state->next;
    if(next_state == NULL)
      break;
  }
}
