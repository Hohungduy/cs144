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
#define CTCP_DEBUG_PRINT

/* state associated to list of transmit segment (unacked) */
typedef struct tx_state{
  bool read_EOF; /* read EOF from STDIN -> send FIN segment*/
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
  bool receive_FIN_Seg; /* receive FIN segment from other hosts -> write EOF to STDOUT */
  uint32_t last_seqno_rep_acked;/* Use it to track the last byte have been acked*/
  uint32_t checksum_failed_segment_count;
  uint32_t truncated_segment_count;
  uint32_t out_of_window_segment_count; /* can't handle this and need to apply a mothod to inform sender know
                            receiver window */
  linked_list_t *rx_segment; /* Linked list of segments receive from this connection */
}rx_state_t;

typedef struct ctcp_sending_segment{
  uint32_t retramsmit_segment; /* counter associated to the number of segment has been sent*/
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

  /* Initialize the state of receiving attributes and linked list of data that have been receive and not yet acked (or acked failed) */
  state->rx_state.receive_FIN_Seg = false;
  state->rx_state.last_seqno_rep_acked = 0;
  state->rx_state.checksum_failed_segment_count = 0;
  state->rx_state.truncated_segment_count = 0;
  state->rx_state.out_of_window_segment_count = 0;

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
    free(current_node);
  }
  free(list);

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
    free(current_node);
  }
  free(list);

end_state:
  free(state);
  end_client();
}

////////////////////////////HELPER FUNCTION//////////////////////////////

/* sending multiple segment to the connected host */
void ctcp_send_multiple_segment(ctcp_state_t *state);

/* sending segment to the connected host */
void ctcp_send_segment(ctcp_state_t *state, ctcp_sending_segment_t *sending_segment, size_t len);

/* inform sender the receiver window size */
void ctcp_send_control_segment(ctcp_state_t *state); 

/* Function associated to read, send, receive, output and timer */
void ctcp_read(ctcp_state_t *state) {
  /* FIXME */
  char tmp_buf[MAX_SEG_DATA_SIZE];/* statically allocate in stack/ maybe use with dynamic allocate */
  uint16_t byte_read;
  ctcp_sending_segment_t *new_send_segment;/* include headers and data (variable string) */
  while((byte_read = conn_input(state->conn, &tmp_buf, MAX_SEG_DATA_SIZE )) > 0)
  {
    /* Read it until data is still available for reading (byte_read > 0)*/

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
    new_send_segment->retramsmit_segment = 0;
    /* init sequence number */
    new_send_segment->segment.seqno = htonl(state->tx_state.last_seqno_read + 1);
    /* init length */
    new_send_segment->segment.len = htons((uint16_t)(sizeof(ctcp_sending_segment_t) + byte_read)); //2 bytes
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
    state->tx_state.read_EOF = true;
    /* Create FIN segment*/
    new_send_segment = (ctcp_sending_segment_t *)calloc(1, sizeof(ctcp_sending_segment_t));
    /* init retransmit segment */
    new_send_segment->retramsmit_segment = 0;
    /* init length */
    new_send_segment->segment.len = htons((uint16_t)(sizeof(ctcp_sending_segment_t)));
    /* init seq_number */
    new_send_segment->segment.seqno = htonl(state->tx_state.last_seqno_read + 1);
    /* update pointer to the last byte read (assume 1 byte data) */
    state->tx_state.last_seqno_read += 1;
    /* Adding this new send segment into linked list */
    ll_add(state->tx_state.tx_segment, new_send_segment);
  }
  /* sending segment */
  ctcp_send_multiple_segment(state);
}

void ctcp_send_multiple_segment(ctcp_state_t *state) {
  ll_node_t *current_node = NULL;
  ll_node_t *next_node = NULL;
  linked_list_t *sender_list = NULL;
  ctcp_sending_segment_t *send_segment = NULL;
  uint16_t send_window;

  sender_list = state->tx_state.tx_segment;
  current_node = sender_list->head;//init
  next_node = sender_list->head->next;//init
  for((current_node = sender_list->head); (send_segment = (ctcp_sending_segment_t *)current_node->object)->segment.seqno < state->tx_state.last_seqno_read + 1;
  current_node = next_node)
  {
    if(((send_segment = (ctcp_sending_segment_t *)current_node->object)->segment.flags) & TH_ACK)
    {
      /* REMEMBER: Or flag TH_ACK in code section that processes ACK segment in receive function */
      /* set up sending segment */
      send_segment->segment.seqno = state->tx_state.last_seqno_tx + 1;
      send_segment->segment.ackno = state->rx_state.last_seqno_rep_acked + 1;
      send_segment->segment.window = state->ctcp_cfg.recv_window;
      send_segment->segment.len = sizeof(ctcp_segment_t);
      send_segment->segment.cksum = cksum(&send_segment->segment,send_segment->segment.len);

      /* No need to set rt_timeout because of using accumalative acknowlege */
      /* No need to update last_seqno_tx because of ACK data length = 0 */
      #ifdef DEBUG_PRINT
      print_hdr_tcp(send_segment->segment);
      #endif

      /*sending ACK segments*/
      ctcp_send_segment(state,send_segment,send_segment->segment.len);
      next_node = current_node->next;
    }
    else{
      /* Getting sending window size */
      send_window = state->ctcp_cfg.send_window;
      if(state->tx_state.last_seqno_read > (state->tx_state.last_seqno_acked + send_window ))
      {
        if((send_segment = (ctcp_sending_segment_t *)current_node->object)->segment.seqno > (state->tx_state.last_seqno_acked + send_window ))
          break;
      }

      /* set up sending segment */
      send_segment->segment.window = state->ctcp_cfg.recv_window;
      send_segment->segment.ackno = state->rx_state.last_seqno_rep_acked + 1;
      send_segment->segment.cksum = cksum(&send_segment->segment,send_segment->segment.len);
      send_segment->time_lastsegment_sent = current_time();

      #ifdef DEBUG_PRINT
      print_hdr_tcp(send_segment->segment);
      #endif
      /*sending*/
      ctcp_send_segment(state,send_segment,send_segment->segment.len);

      /*update the last seq sent pointer */
      state->tx_state.last_seqno_tx += (send_segment->segment.len - sizeof(ctcp_segment_t));/* add datalen */

      next_node = current_node->next;
    }
  }
}
void ctcp_send_segment(ctcp_state_t *state, ctcp_sending_segment_t *sending_segment, size_t len)
{
  conn_t *conn = state->conn;
  size_t byte_send = 0;
  size_t tot_left = len;
  const char *tmp_buf;

  tmp_buf = sending_segment->segment.data;

  while(tot_left > 0)
  {
    byte_send = conn_send(conn,&sending_segment->segment,tot_left);
    if(-1 == byte_send)
    {
      #ifdef DEBUG_PRINT
      fprintf(stderr,"%d - %d: conn_send() failed",__LINE__,__func__);
      #endif
      ctcp_destroy(state);
      exit(EXIT_FAILURE);
    }
    sending_segment->segment.seqno += byte_send;
    sending_segment->segment.len -= byte_send;
    tmp_buf += byte_send;
    tot_left = len - byte_send;
    memcpy(sending_segment->segment.data, tmp_buf, tot_left);
    sending_segment->segment.cksum = cksum(&sending_segment->segment,sending_segment->segment.len);
  }
}
void ctcp_send_control_segment(ctcp_state_t *state){
  /* Do this when no longer available space on receiver */
  conn_t *conn = state->conn;
  ctcp_segment_t segment;
  segment.seqno = htonl(1);
  segment.ackno = htonl(state->rx_state.last_seqno_rep_acked+1);
  segment.len =sizeof(ctcp_segment_t);
  segment.window = (uint16_t)htons(state->ctcp_cfg.recv_window);
  segment.cksum =cksum(&segment,segment.len);
  conn_send(conn, &segment, segment.len);
}

void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
  /* FIXME */
  /* REMEMBER: Free segment after using it */
}

void ctcp_output(ctcp_state_t *state) {
  /* FIXME */
}

void ctcp_timer() {
  /* FIXME */
  /* Task of this function:
  - Inspect segments and retransmit ones that have not been acked. Check timestamp and rt_timeout. If timeout
  occurs, increase retransmission counter in segments
  - After 5 retransmission attempts (a total of 6 times ) for segments -> call ctcp_destroy().

  Remember to retrieve segment data from state_list pointer (current state) and check retransmission count
  */

}
