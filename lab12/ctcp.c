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
  uint32_t retransmit_segment; /* counter associated to the number of segment has been sent*/
  long time_lastsegment_sent; /* time that last segment sent*/
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
  // if(&state->ctcp_cfg)
  //   free(&state->ctcp_cfg);
  // if(state->conn)
  //   free(state->conn);
  end_client();
}

////////////////////////////HELPER FUNCTION//////////////////////////////

/* sending multiple segment to the connected host */
void ctcp_send_multiple_segment(ctcp_state_t *state);

/* sending segment to the connected host */
inline void ctcp_send_segment(ctcp_state_t *state, ctcp_sending_segment_t *sending_segment, size_t len);

/* inform sender the receiver window size */
inline void ctcp_send_control_segment(ctcp_state_t *state); 

/* Function associated to read, send, receive, output and timer */
void ctcp_read(ctcp_state_t *state) {
  /* FIXME */
  char tmp_buf[MAX_SEG_DATA_SIZE];/* statically allocate in stack/ maybe use with dynamic allocate */
  uint16_t byte_read;
  ctcp_sending_segment_t *new_send_segment;/* include headers and data (variable string) */
  if (state == NULL)
    return;
  #ifdef DEBUG_PRINT
  fprintf(stderr, "%d - %s\n", __LINE__,__func__);
  #endif
  while((byte_read = conn_input(state->conn, &tmp_buf, MAX_SEG_DATA_SIZE )) > 0)
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
    /* init length */
    new_send_segment->segment.len = htons((uint16_t)(sizeof(ctcp_segment_t) + byte_read)); //2 bytes
    #ifdef DEBUG_PRINT
    fprintf(stderr, "%d - %s, len:%ld - byte_read:%d \n", __LINE__, __func__, (long)(new_send_segment->segment.len), (int)byte_read);
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
    #ifdef DEBUG_PRINT
    fprintf(stderr, "%d - %s\n", __LINE__,__func__);
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
    new_send_segment->segment.flags |= TH_FIN;
    /* update pointer to the last byte read (assume 1 byte data) */
    state->tx_state.last_seqno_read += 1;
    /* Adding this new send segment into linked list */
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

  current_node = sender_list->head;//init
  next_node = sender_list->head->next;//init
  send_segment = (ctcp_sending_segment_t *)current_node->object;
  fprintf(stderr,"%d - %s: send_segment:%p\n",__LINE__,__func__,send_segment);
  // send_seqno = ntohl(send_segment->segment.seqno);
  // #ifdef DEBUG_PRINT
  // fprintf(stderr, "%d - %s, seqno:%ld, last_seqno_read:%ld\n", __LINE__,__func__,(long)send_seqno,(long)(state->tx_state.last_seqno_read));
  // #endif

  for((current_node = sender_list->head); ntohl(send_segment->segment.seqno) <= (state->tx_state.last_seqno_read + 1);
  current_node = next_node)
  {
    if(((send_segment = (ctcp_sending_segment_t *)current_node->object)->segment.flags) & TH_ACK)
    {
      #ifdef DEBUG_PRINT
      fprintf(stderr, "%d - %s\n", __LINE__,__func__);
      #endif
      /* REMEMBER: Or flag TH_ACK in code section that processes ACK segment in receive function */
      /* set up sending segment */
      send_segment->segment.seqno = htonl(state->tx_state.last_seqno_tx + 1);
      send_segment->segment.ackno = htonl(state->rx_state.last_seqno_rep_acked + 1);
      send_segment->segment.window = htons(state->ctcp_cfg.recv_window);
      send_segment->segment.len = htons(sizeof(ctcp_segment_t));
      send_segment->segment.cksum = cksum(&send_segment->segment,ntohs(send_segment->segment.len));

      /* No need to set rt_timeout because of using accumalative acknowlege */
      /* No need to update last_seqno_tx because of ACK data length = 0 */

      #ifdef DEBUG_PRINT
      print_hdr_ctcp(&send_segment->segment);
      #endif
      /*sending ACK segments*/
      ctcp_send_segment(state,send_segment,send_segment->segment.len);
      next_node = current_node->next;
    }
    else if(((send_segment = (ctcp_sending_segment_t *)current_node->object)->segment.flags) & TH_FIN)
    {
      #ifdef DEBUG_PRINT
      fprintf(stderr, "%d - %s\n", __LINE__,__func__);
      #endif
      /* REMEMBER: Or flag TH_ACK in code section that processes ACK segment in receive function */
      /* set up sending segment */
      send_segment->segment.ackno = htonl(state->rx_state.last_seqno_rep_acked + 1);
      send_segment->segment.window = htons(state->ctcp_cfg.recv_window);
      send_segment->segment.cksum = cksum(&send_segment->segment,ntohs(send_segment->segment.len));
      send_segment->time_lastsegment_sent = current_time();

      /* No need to set rt_timeout because of using accumalative acknowlege */
      /* No need to update last_seqno_tx because of ACK data length = 0 */
      // #ifdef DEBUG_PRINT
      // print_hdr_ctcp(&send_segment->segment);
      // #endif

      #ifdef DEBUG_PRINT
      print_hdr_ctcp(&send_segment->segment);
      #endif
      /*sending ACK segments*/
      ctcp_send_segment(state,send_segment,send_segment->segment.len);
      state->tx_state.last_seqno_tx += 1;
      state->tx_state.send_FIN_acked = true;
      next_node = current_node->next;
    }
    else{
      /* Getting sending window size */
      #ifdef DEBUG_PRINT
      fprintf(stderr, "%d - %s\n", __LINE__,__func__);
      #endif
      send_window = state->ctcp_cfg.send_window;
      if(state->tx_state.last_seqno_read > (state->tx_state.last_seqno_acked + send_window ))
      {
        if((send_segment = (ctcp_sending_segment_t *)current_node->object)->segment.seqno > (state->tx_state.last_seqno_acked + send_window ))
          break;
      }

      /* set up sending segment */
      send_segment->segment.window = htons(state->ctcp_cfg.recv_window);
      send_segment->segment.ackno = htonl(state->rx_state.last_seqno_rep_acked + 1);
      send_segment->segment.cksum = cksum(&send_segment->segment,ntohs(send_segment->segment.len));
      send_segment->time_lastsegment_sent = current_time();

      #ifdef DEBUG_PRINT
      print_hdr_ctcp(&send_segment->segment);
      #endif

      /*sending*/
      ctcp_send_segment(state,send_segment,send_segment->segment.len);

      /*update the last seq sent pointer */
      state->tx_state.last_seqno_tx += (send_segment->segment.len - sizeof(ctcp_segment_t));/* add datalen */
      next_node = current_node->next;
    }
    if(next_node == NULL)
      break;
    send_segment = (ctcp_sending_segment_t *)next_node->object;
  }
  #ifdef DEBUG_PRINT
  fprintf(stderr, "%d - %s\n", __LINE__,__func__);
  #endif
}
void ctcp_send_segment(ctcp_state_t *state, ctcp_sending_segment_t *sending_segment, size_t len)
{
  conn_t *conn = state->conn;
  size_t byte_send = 0;
  size_t tot_left = len;
  const char *tmp_buf;

  tmp_buf = sending_segment->segment.data;
  #ifdef DEBUG_PRINT
  fprintf(stderr, "%d - %s\n", __LINE__,__func__);
  #endif
  while(tot_left > 0)
  {
    byte_send = conn_send(conn,&sending_segment->segment,ntohs(tot_left));

    if(-1 == byte_send)
    {
      #ifdef DEBUG_PRINT
      fprintf(stderr,"%d - %s: conn_send() failed\n",__LINE__,__func__);
      #endif
      return;
      // ctcp_destroy(state);
      // exit(EXIT_FAILURE);
    }
    if(ntohs(len) == byte_send)
    {
      #ifdef DEBUG_PRINT
      fprintf(stderr,"%d - %s: send successfully at the first time\n",__LINE__,__func__);
      #endif
      tot_left = 0;
      return;
    }
    #ifdef DEBUG_PRINT
    fprintf(stderr,"%d - %s: send continue: byte_send:%d\n",__LINE__,__func__, (int)byte_send);
    #endif
    sending_segment->segment.seqno = htonl(ntohl(sending_segment->segment.seqno) + byte_send);
    sending_segment->segment.len = htonl(ntohl(sending_segment->segment.len) - byte_send);
    tmp_buf += byte_send;
    tot_left = len - byte_send;
    memcpy(sending_segment->segment.data, tmp_buf, tot_left);
    sending_segment->segment.cksum = cksum(&sending_segment->segment,ntohs(sending_segment->segment.len));
  }
}
void ctcp_send_control_segment(ctcp_state_t *state){
  /* Do this when there is no longer available space on receiver */
  conn_t *conn = state->conn;
  ctcp_segment_t segment;
  segment.seqno = htonl(1);
  segment.ackno = htonl(state->rx_state.last_seqno_rep_acked+1);
  segment.len = htons(sizeof(ctcp_segment_t));
  segment.window = htons((uint16_t)htons(state->ctcp_cfg.recv_window));
  segment.cksum =cksum(&segment,ntohs(segment.len));
  conn_send(conn, &segment, ntohs(segment.len));
}     

void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
  /* FIXME */
  /* REMEMBER: Free segment after using it */
  /* First checking condition first */

  uint16_t rcv_segment_len = ntohs(segment->len);

  uint16_t correct_rcv_segment_cksum = 0;
  uint32_t rcv_segment_seqno = ntohl(segment->seqno);
  uint32_t rcv_segment_ackno = ntohl(segment->ackno);
  uint32_t rcv_segment_flag = segment->flags;
  // ll_node_t *last_node_in_queue = ll_back(state->rx_state.rx_segment);
  // ctcp_segment_t *last_segment_in_queue = (ctcp_segment_t *)(last_node_in_queue->object);
  // uint32_t upperbound_recv_window = ntohl(last_segment_in_queue->seqno) + ((ntos(last_segment_in_queue->len) - sizeof(ctcp_segment_t)) + state->ctcp_cfg.recv_window;
  uint32_t upperbound_recv_window = state->rx_state.last_seqno_rep_acked + state->ctcp_cfg.recv_window;
  uint16_t datalen = rcv_segment_len - sizeof(ctcp_segment_t);

  unsigned int ll_len = 0;
  ll_node_t *current_node = NULL;
  ll_node_t *next_node = NULL;

  if (state == NULL)
    return;

  /* Checking whether this segment is truncated segment */
  if(len < rcv_segment_len)
  {
    #ifdef DEBUG_PRINT
    fprintf(stderr,"%d - %s: discard truncated segments - len:%d, rcv_segment_len:%d\n",__LINE__,__func__,(int)len,(int)rcv_segment_len);
    #endif
    state->rx_state.truncated_segment_count++;
    if(segment)
      free(segment);
    return;
  }

  /* Checking if checksum value of segment is correct */

  correct_rcv_segment_cksum = segment->cksum;
  segment->cksum = 0;
  uint16_t computed_rc_segment_cksum = cksum(segment,rcv_segment_len);
  segment->cksum = correct_rcv_segment_cksum;
  if (computed_rc_segment_cksum != correct_rcv_segment_cksum)
  {
    #ifdef DEBUG_PRINT
    fprintf(stderr,"%d - %s: discard wrong segments when we checksum, actual_rc_segment_cksum:%x, corect_recv_cksum:%x\n",__LINE__,__func__,(int)computed_rc_segment_cksum,(int)correct_rcv_segment_cksum);
    #endif
    state->rx_state.checksum_failed_segment_count++;
    if(segment)
      free(segment);
    return;
  }
  /* Checking if this segment is out of receiving window bound */
  if(rcv_segment_seqno > upperbound_recv_window)
  {
    #ifdef DEBUG_PRINT
    fprintf(stderr,"%d - %s: discard segments when we reach the limmit of receving window\n",__LINE__,__func__);
    #endif
    state->rx_state.out_of_window_segment_count++;
    /* sending control message to inform sender know the current receiver window */
    ctcp_send_control_segment(state);
    if(segment)
      free(segment);
    return;
  }

  #ifdef DEBUG_PRINT
  fprintf(stderr,"Valid message!!!\n");
  print_hdr_ctcp(segment);
  #endif

  /* Checking if FIN flag is set */
  if(rcv_segment_flag & TH_FIN)
  {

    /* Checking if ACK flag is set */
    if(rcv_segment_flag & TH_ACK)
    {
      #ifdef DEBUG_PRINT
      fprintf(stderr,"%d - %s: FIN-ACK flag is set\n",__LINE__,__func__);
      #endif
      /* 
        Proccessing FIN/ACK. To do:
      - increase last_seqno_rep_acked by 1 bytes
      - Send ACK with ackno greater than 1 bytes of the FIN segment (we have already increased last_seqno_rep_acked in step 1)
      */
      state->rx_state.receive_FIN_seg = true;
      
      /*Adding ACK into  Sending list */
      ctcp_sending_segment_t *final_ACK_segment = (ctcp_sending_segment_t *)calloc(1,sizeof(ctcp_sending_segment_t));
      final_ACK_segment->segment.flags |= TH_ACK;
      ll_add(state->tx_state.tx_segment, final_ACK_segment);

      /*Update the last byte that we receive it and reply an ack segment to the sender */
      state->rx_state.last_seqno_rep_acked +=1;
      if(datalen)
        goto data_process;
      goto end;
    
    }
    else{
    /* Processing FIN segment. To do:
      - Increase last_seqno_rep_acked by 1 bytes
      - Send ACK with ackno greater than 1 bytes of the FIN segment (we have already increased last_seqno_rep_acked in step 1)
    */
    #ifdef DEBUG_PRINT
    fprintf(stderr,"%d - %s: FIN flag is set\n",__LINE__,__func__);
    #endif

    state->rx_state.receive_FIN_seg = true;
    /* Add to receiving list */
    ll_add(state->rx_state.rx_segment,segment); /* Using conn_ouput with length = 0 in ctcp_output() to process this segment(send EOF) to STDOUT)*/
    
    /* Adding ACK into  Sending list */
    ctcp_sending_segment_t *ACK_segment = (ctcp_sending_segment_t *)calloc(1,sizeof(ctcp_sending_segment_t));
    ACK_segment->segment.flags |= TH_ACK;
    ll_add(state->tx_state.tx_segment, ACK_segment);

    /*Update the last byte that we receive it and reply an ack segment to the sender */
    state->rx_state.last_seqno_rep_acked +=1;
    if(datalen)
        goto data_process;
    goto end;
    }

  }
  /* Checking if only ACK flag is set */
  if(rcv_segment_flag & TH_ACK)
  {
    #ifdef DEBUG_PRINT
    fprintf(stderr,"%d - %s: ACK flag is set\n",__LINE__,__func__);
    #endif

    /* Proccessing normal ACK. To do:
    - increase last_seqno_ackno ->increase base of sending window
    - Remove the segment with the specific sequence number from the sending list 
      , provided that the sequence number < acknowlegde number of this acknowlegde segment
    */

    linked_list_t *send_list = state->tx_state.tx_segment;
    if(ll_front(send_list) == NULL)
    {
      goto end;
    }
    current_node = ll_front(send_list);
    ctcp_sending_segment_t *send_segment = (ctcp_sending_segment_t *)(current_node->object);
    /* Remove tx_segments have been acked */

    for(current_node = send_list->head; ntohl(send_segment->segment.seqno) < rcv_segment_ackno; current_node = next_node ){
      
      state->tx_state.last_seqno_acked += (ntohl(send_segment->segment.len) -sizeof(ctcp_segment_t));
      next_node = current_node->next;
      if(send_segment)
        free(send_segment);
      ll_remove(send_list, current_node);
      if(next_node == NULL)
        break;
      send_segment = (ctcp_sending_segment_t *)(next_node->object);
    }
    if((rcv_segment_len - sizeof(ctcp_segment_t)) == 0)
    {
      /* Only ACK, no data */
      goto end;
    }
    /* Piggybagged: Having data (rcv_segment_len =ntohs(segment->len)) */
  }
data_process:

  if(rcv_segment_seqno <= state->rx_state.last_seqno_rep_acked)
  {
    if(segment)
      free(segment);/*duplicate data */
    return;
  }
  linked_list_t *receive_list = state->rx_state.rx_segment;
  ll_len = ll_length(receive_list);
  ctcp_segment_t *segment_ptr = NULL;
  ll_node_t *first_node = NULL;
  ctcp_segment_t *first_segment;

  #ifdef DEBUG_PRINT
  fprintf(stderr, "%d - %s\n", __LINE__,__func__);
  #endif
  if(ll_len == 0)
  {
    ll_add(receive_list, segment);
        /* Update in output function:last_seqno_rep_acked in receiving list -> ackno used in sending segment will use this value*/
    // state->rx_state.last_seqno_rep_acked += datalen;
    /* Update receive window */
    state->ctcp_cfg.recv_window -= datalen;

    /* SENDING ACK to the right-ordered segment (adding to sending list) */
    ctcp_sending_segment_t *ACK_segment = (ctcp_sending_segment_t *)calloc(1,sizeof(ctcp_sending_segment_t));
    ACK_segment->segment.flags |= TH_ACK;
    /* Adding ACK into  Sending list */
    ll_add(state->tx_state.tx_segment, ACK_segment);
    goto end;
  }
  first_node = ll_front(receive_list);
  first_segment = (ctcp_segment_t *)(first_node->object);
  // ll_node_t *ll_node_ptr = NULL;
  // ll_node_t *last_node = ll_back(receive_list);
  // ctcp_segment_t *last_segment = (ctcp_segment_t *)(last_node->object);
  #ifdef DEBUG_PRINT
  fprintf(stderr, "%d - %s\n", __LINE__,__func__);
  #endif

  if(rcv_segment_seqno == (state->rx_state.last_seqno_rep_acked + 1))
  {
    #ifdef DEBUG_PRINT
    fprintf(stderr, "%d - %s\n", __LINE__,__func__);
    #endif
    /* Adding this segment behind the right ordered acked segment in receiving */
    if(ntohl(first_segment->seqno) == (state->rx_state.last_seqno_rep_acked + 1))
    {
      //discard segment
      if(segment)
       free(segment);
      return;
    }
    else
    {
      current_node = NULL;
      next_node = NULL;
      for(current_node = first_node; ntohl((segment_ptr = (ctcp_segment_t *)(current_node->object))->seqno) >= (state->rx_state.last_seqno_rep_acked+1); current_node = next_node)
      {
        next_node = current_node->next;
      }
      segment_ptr = (ctcp_segment_t *)(current_node->object);
      if(ntohl(segment_ptr->seqno) == (state->rx_state.last_seqno_rep_acked + 1))
      {
        //discard segment
        if(segment)
          free(segment);
        return;
      }
      else{
        current_node = current_node->prev;
        ll_add_after(receive_list, current_node, segment);
      }
    }
    /* Update last_seqno_rep_acked in receiving list -> ackno used in sending segment will use this value*/
    // state->rx_state.last_seqno_rep_acked += datalen;
    /* Update receive window */
    state->ctcp_cfg.recv_window -= datalen;

    /* SENDING ACK to the right-ordered segment (adding to sending list) */
    ctcp_sending_segment_t *ACK_segment = (ctcp_sending_segment_t *)calloc(1,sizeof(ctcp_sending_segment_t));
    ACK_segment->segment.flags |= TH_ACK;
    /* Adding ACK into  Sending list */
    ll_add(state->tx_state.tx_segment, ACK_segment);
  }
  else
  {
    #ifdef DEBUG_PRINT
    fprintf(stderr, "%d - %s\n", __LINE__,__func__);
    #endif
    /* Adding this segment behind the right ordered acked segment in receiving */
    current_node = NULL;
    next_node = NULL;
    for(current_node = first_node; ntohl((segment_ptr = (ctcp_segment_t *)(current_node->object))->seqno) >= (rcv_segment_seqno); current_node = next_node)
    {
      next_node = current_node->next;
    }
    segment_ptr = (ctcp_segment_t *)(current_node->object);
    if(ntohl(segment_ptr->seqno) == (rcv_segment_seqno))
    {
      //discard segment
      if(segment)
        free(segment);
      return;
    }
    else{
      current_node = current_node->prev;
      ll_add_after(receive_list, current_node, segment);
    }
    /* Update receive window */
    state->ctcp_cfg.recv_window -= datalen;
  }
end:
  ctcp_output(state);


}

void ctcp_output(ctcp_state_t *state) {
  /* FIXME */
  ll_node_t* first_node;
  ctcp_segment_t* segment_ptr;
  size_t bufspace;
  int datalen;
  int byte_output;
  int num_segments_output = 0;

  if (state == NULL)
    return;

  while (ll_length(state->rx_state.rx_segment) != 0) {
    #ifdef DEBUG_PRINT
    fprintf(stderr, "%d - %s\n", __LINE__,__func__);
    #endif
    // Grab the segment we're going to try to output.
    first_node = ll_front(state->rx_state.rx_segment);
    segment_ptr = (ctcp_segment_t*) first_node->object;

    datalen = ntohs(segment_ptr->len) - sizeof(ctcp_segment_t);
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

      /* See if there is enough bufspace right now to output.*/
      bufspace = conn_bufspace(state->conn);
      if (bufspace < datalen) {
        /*can't send right now, send control segment to inform sender know the situation of receiver buffer.*/
        goto end;
      }

      byte_output = conn_output(state->conn, segment_ptr->data, datalen);
      if (byte_output == -1) {
        #ifdef DEBUG_PRINT
        fprintf(stderr, "conn_output() returned -1\n");
        #endif
        // ctcp_destroy(state);
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
      #ifdef DEBUG_PRINT
      fprintf(stderr, "received FIN, incrementing state->rx_state.last_seqno_rep_acked\n");
      #endif
      state->rx_state.last_seqno_rep_acked++;
      conn_output(state->conn, segment_ptr->data, 0);
      num_segments_output++;
    }
    /* remove it from the linked list after successfully output*/
    if(segment_ptr)
      free(segment_ptr); /*bad using */
    ll_remove(state->rx_state.rx_segment, first_node);
  }
end:

  if (num_segments_output) {
    // Send an ack. Acking here (instead of in ctcp_receive) flow controls the
    // sender until buffer space is available.
    ctcp_send_control_segment(state);
  }

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
    #ifdef DEBUG_PRINT
     fprintf(stderr, "%d - %s\n", __LINE__,__func__);
    #endif
    ctcp_output(current_state);
        #ifdef DEBUG_PRINT
     fprintf(stderr, "%d - %s\n", __LINE__,__func__);
    #endif
    ctcp_read(current_state);
        #ifdef DEBUG_PRINT
     fprintf(stderr, "%d - %s\n", __LINE__,__func__);
    #endif

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
    if((current_state->tx_state.send_FIN_acked && current_state->tx_state.read_EOF) 
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
        if((current_state->timeout_connection -current_time()) > (2 * MAX_SEG_LIFETIME))
        {
          #ifdef DEBUG_PRINT
          fprintf(stderr,"%d - %s: Destroy connection\n",__LINE__, __func__);
          #endif
          ctcp_destroy(current_state);
          return;
        }
    }
    #ifdef DEBUG_PRINT
     fprintf(stderr, "%d - %s\n", __LINE__,__func__);
    #endif
    /* checking timestamp in each sending node and retransmit it if retransmit < 5. */
    if((current_node = current_state->tx_state.tx_segment->head) == NULL)
      return;
    // if((current_node = ll_front(current_state->tx_state.tx_segment)) == NULL)
    //   return;

    for((current_node = current_state->tx_state.tx_segment->head); current_node != NULL; current_node = next_node)
    {
      #ifdef DEBUG_PRINT
      fprintf(stderr, "%d - %s\n", __LINE__,__func__);
      #endif
      send_segment = (ctcp_sending_segment_t *)(current_node->object);
      if((current_time() - (send_segment->time_lastsegment_sent)) > (current_state->ctcp_cfg.rt_timeout))
      {
        send_segment->retransmit_segment++;
      }
      if(send_segment->retransmit_segment > 5)
      {
        #ifdef DEBUG_PRINT
        fprintf(stderr,"%d - %s: destroy connection (retransmit_segment > 5)\n",__LINE__, __func__);
        #endif
        ctcp_destroy(current_state);
        return;
      }
      else
      {
        /* retransmit a missing segment */
        #ifdef DEBUG_PRINT
         fprintf(stderr,"%d - %s: retransmit a segment \n",__LINE__, __func__);
        #endif
        ctcp_send_segment(current_state, current_node->object, send_segment->segment.len);
      }
      next_node = current_node->next;
      if(next_node == NULL)
        break;
    }
    next_state = current_state->next;
    if(next_state == NULL)
      break;
  }

}
