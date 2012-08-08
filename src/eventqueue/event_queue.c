#include "eventqueue/event_queue.h"

#include <stdlib.h>

#include "protocol/firefly_protocol_private.h"
#include "firefly_errors.h"

struct firefly_event_queue *firefly_event_queue_new(
		firefly_offer_event offer_cb)
{
	struct firefly_event_queue *q;

	if ((q = malloc(sizeof(struct firefly_event_queue))) != NULL) {
		q->head = NULL;
		q->offer_event_cb = offer_cb;
	}

	return q;
}

void firefly_event_queue_free(struct firefly_event_queue **q)
{
	struct firefly_event *ev;

	while ((ev = firefly_event_pop(*q)) != NULL)
		firefly_event_free(&ev);
	free(*q);
	*q = NULL;
}

struct firefly_event *firefly_event_new(enum firefly_event_type t,
		unsigned char prio)
{
	struct firefly_event *ev;

	if ((ev = malloc(sizeof(struct firefly_event))) != NULL) {
		ev->base.type = t;
		ev->base.prio = prio;
	}

	return ev;
}

void firefly_event_free(struct firefly_event **ev)
{
	free(*ev);
	*ev = NULL;
}

int firefly_event_add(struct firefly_event_queue *eq, struct firefly_event *ev)
{
	struct firefly_eq_node *n = eq->head;
	if (n != NULL && n->event->base.prio < ev->base.prio) {
		n = NULL;
	}

	while (n != NULL && n->next != NULL &&
			n->event->base.prio >= ev->base.prio) {
		n = n->next;
	}

	struct firefly_eq_node *node = malloc(sizeof(struct firefly_eq_node));
	if (node == NULL) {
		return -1;
	}
	node->event = ev;

	if (n != NULL) {
		node->next = n->next;
		n->next = node;
	} else {
		node->next = eq->head;
		eq->head = node;
	}

	return 0;
}

struct firefly_event *firefly_event_pop(struct firefly_event_queue *eq)
{
	// The node containing the event.
	struct firefly_eq_node *event_node; 

	// The actual event
	struct firefly_event *ev;

	if ((event_node = eq->head) == NULL) {
		return NULL;
	}
	eq->head = eq->head->next;
	ev = event_node->event;
	free(event_node);

	return ev;
}

int firefly_event_execute(struct firefly_event *ev)
{
	switch (ev->base.type) {
	case EVENT_CHAN_OPEN: {
		struct firefly_event_chan_open *ev_co =
				(struct firefly_event_chan_open *) ev;
		firefly_channel_open_event(ev_co->conn, ev_co->rejected_cb);
	} break;
	case EVENT_CHAN_CLOSED: {
		struct firefly_event_chan_closed *ev_cc =
				(struct firefly_event_chan_closed *) ev;
		firefly_channel_closed_event(ev_cc->chan, ev_cc->conn);
	} break;
	case EVENT_CHAN_CLOSE: {
		struct firefly_event_chan_close *ev_cc =
				(struct firefly_event_chan_close *) ev;
		firefly_channel_close_event(ev_cc->conn, ev_cc->chan_close);
		free(ev_cc->chan_close);
	} break;
	case EVENT_CHAN_REQ_RECV: {
		struct firefly_event_chan_req_recv *ev_crr =
				(struct firefly_event_chan_req_recv *) ev;
		handle_channel_request_event(ev_crr->chan_req, ev_crr->conn);
		free(ev_crr->chan_req);
	} break;
	case EVENT_CHAN_RES_RECV: {
		struct firefly_event_chan_res_recv *ev_crr =
				(struct firefly_event_chan_res_recv *) ev;
		handle_channel_response_event(ev_crr->chan_res, ev_crr->conn);
		free(ev_crr->chan_res);
	} break;
	case EVENT_CHAN_ACK_RECV: {
		struct firefly_event_chan_ack_recv *ev_car =
				(struct firefly_event_chan_ack_recv *) ev;
		handle_channel_ack_event(ev_car->chan_ack, ev_car->conn);
		free(ev_car->chan_ack);
	} break;
	case EVENT_SEND_SAMPLE: {
		struct firefly_event_send_sample *ev_ss =
				(struct firefly_event_send_sample *) ev;
		labcomm_encode_firefly_protocol_data_sample(
				ev_ss->conn->transport_encoder, ev_ss->pkt);
		free(ev_ss->pkt->app_enc_data.a);
		free(ev_ss->pkt);
	} break;
	case EVENT_RECV_SAMPLE: {
		struct firefly_event_recv_sample *ev_rs =
				(struct firefly_event_recv_sample *) ev;
		handle_data_sample_event(ev_rs->pkt, ev_rs->conn);
		free(ev_rs->pkt->app_enc_data.a);
		free(ev_rs->pkt);
	} break;
	default: {
		firefly_error(FIREFLY_ERROR_EVENT, 1, "Bad event type");
	} break;
	}
	firefly_event_free(&ev); /* It  makes no sense to keep it around... */

	return 0; // TODO no result status used, then delete it.
}

size_t firefly_event_queue_length(struct firefly_event_queue *eq)
{
	struct firefly_eq_node *ev;
	size_t n = 0;

	ev = eq->head;
	while (ev) {
		ev = ev->next;
		n++;
	}

	return n;
}
