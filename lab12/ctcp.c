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

#undef DEBUG_PRINT

/* state associated to list of transmit segment (unacked) */
typedef struct tx_state{
  bool read_EOF; /* read EOF from STDIN -> send FIN segment*/
  uint32_t last_ackno_rx; /* pointer to ack number whose segment is the last sent and received ack. 
                             (Thus, pointer to segment that sent but have not received ack = last_ackno_rx+1)*/
  uint32_t last_seqno_tx; /* pointer to the last byte have been sent
                              Thus, pointer to next sequence number whose segment = last_seqno_tx + 1*/
  uint32_t last_seqno_read; /* pointer to the last byte have been read */
  /* As we can see from this value:
  - last_ackno_rx + 1 = base number of send window
  - last_seqno_tx + 1 = next sequence number that needs to be sent
  - last_seqno_read < base number of send window + window size;
  */

  linked_list_t *unacked_send_segment;

}tx_state_t;

/* state associated to list of receive segment (unacked) */
typedef struct rx_state{
  // bool write_eof; /* write EOF to STDOUT-> send FIN segment*/
  bool receive_FIN_Seg; /* receive FIN segment from other hosts -> write EOF to STDOUT */
  uint32_t last_seqno_rep_acked;/* Use it to track the last byte have been acked*/
  uint32_t checksum_failed_segment_count;
  uint32_t truncated_segment_count;
  uint32_t out_of_window_segment_count; /* can't handle this and need to apply a mothod to inform sender know
                            receiver window */
  linked_list_t *segment; /* Linked list of segments receive from this connection */
}rx_state_t;

typedef struct unacked_send_segment{
  uint32_t retramsmit_segment; /* counter associated to the number of segment has been sent*/
  long time_lastsegment_sent; /* time that last segment sent*/
  linked_list_t *segment; /* Linked list of segments send to this connection */

}unacked_send_segment_t;
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
  state->tx_state.last_ackno_rx = 0;
  state->tx_state.last_seqno_read = 0;
  state->tx_state.last_seqno_tx = 0;

  /* Initialize the state of receiving attributes and linked list of data that have been receive and not yet acked (or acked failed) */
  state->rx_state.receive_FIN_Seg = false;
  state->rx_state.last_seqno_rep_acked = 0;
  state->rx_state.checksum_failed_segment_count = 0;
  state->rx_state.truncated_segment_count = 0;
  state->rx_state.out_of_window_segment_count = 0;

  return state;
}

void ctcp_destroy(ctcp_state_t *state) {
  /* Update linked list. */
  if (state->next)
    state->next->prev = state->prev;

  *state->prev = state->next;
  conn_remove(state->conn);

  /* FIXME: Do any other cleanup here. */

  free(state);
  end_client();
}

void ctcp_read(ctcp_state_t *state) {
  /* FIXME */
}

void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
  /* FIXME */
}

void ctcp_output(ctcp_state_t *state) {
  /* FIXME */
}

void ctcp_timer() {
  /* FIXME */
}
