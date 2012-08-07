/**
 * @file
 * @brief TODO describe events here!
 */

#ifndef FIREFLY_EVENT_QUEUE_H
#define FIREFLY_EVENT_QUEUE_H

#include <stdlib.h>

/* TODO: refac. */
/* TODO: comment data types and functions. */

#include "gen/firefly_protocol.h"
#include "protocol/firefly_protocol_private.h"

enum firefly_event_type {
	EVENT_CHAN_OPEN,
	EVENT_CHAN_CLOSED,
	EVENT_CHAN_CLOSE,
	EVENT_CHAN_REQ_RECV,
	EVENT_CHAN_RES_RECV,
	EVENT_CHAN_ACK_RECV,
	EVENT_SEND_SAMPLE,
	EVENT_RECV_SAMPLE
};

/**
 * @brief A generic event.
 *
 * The largest context of an event is a single firefly_connection. A larger
 * context may imply concurrency problems.
 */
struct firefly_event_base {
	enum firefly_event_type type;
	unsigned char prio;
};

// TODO this event wrapper is probably not needed anymore.
struct firefly_event {
	struct firefly_event_base base;
};

/* Concrete events */
struct firefly_event_chan_open {
	struct firefly_event_base base;
	struct firefly_connection *conn;
	firefly_channel_rejected_f rejected_cb;
};

struct firefly_event_chan_closed {
	struct firefly_event_base base;
	struct firefly_channel *chan;
	struct firefly_connection *conn; /* prev has back ref. weird
					though... HUH???*/
};

struct firefly_event_chan_close {
	struct firefly_event_base base;
	struct firefly_connection *conn;
	firefly_protocol_channel_close *chan_close;
};

/* Event queue */
struct firefly_event_chan_req_recv {
	struct firefly_event_base base;
	struct firefly_connection *conn;
	firefly_protocol_channel_request *chan_req;
};

struct firefly_event_chan_res_recv {
	struct firefly_event_base base;
	struct firefly_connection *conn;
	firefly_protocol_channel_response *chan_res;
};

struct firefly_event_chan_ack_recv {
	struct firefly_event_base base;
	struct firefly_connection *conn;
	firefly_protocol_channel_ack *chan_ack;
};

struct firefly_event_send_sample {
	struct firefly_event_base base;
	struct firefly_connection *conn;
	firefly_protocol_data_sample *pkt;
};

struct firefly_event_recv_sample {
	struct firefly_event_base base;
	struct firefly_connection *conn;
	firefly_protocol_data_sample *pkt;
};

struct firefly_event_queue;

typedef int (*firefly_offer_event)(struct firefly_event_queue *queue,
						   struct firefly_event *event);

struct firefly_event_queue {
	struct firefly_eq_node *head;
	struct firefly_eq_node *tail;
	firefly_offer_event offer_event_cb; /* The callback used for adding
					       	       	       events. */
	void *context;			/* Possibly a mutex...  */
};

struct firefly_eq_node {
	struct firefly_eq_node *next;
	struct firefly_event *event;
};

struct firefly_event_queue *firefly_event_queue_new();

void firefly_event_queue_free(struct firefly_event_queue **eq);

struct firefly_event *firefly_event_new(enum firefly_event_type t,
					unsigned char prio);

void firefly_event_free(struct firefly_event **ev);

int firefly_event_add(struct firefly_event_queue *eq, struct firefly_event *ev);

struct firefly_event *firefly_event_pop(struct firefly_event_queue *eq);

int firefly_event_execute(struct firefly_event *ev);

size_t firefly_event_queue_length(struct firefly_event_queue *eq);

#endif
