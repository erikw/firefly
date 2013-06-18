/**
 * @file
 * @brief Connection related functions for the protocol layer.
 */

#include <protocol/firefly_protocol.h>
#include <utils/firefly_errors.h>

#include "protocol/firefly_protocol_private.h"
#include "utils/firefly_event_queue_private.h"

struct firefly_connection *firefly_connection_new(
		struct firefly_connection_actions *actions,
		transport_write_f transport_write,
		transport_ack_f transport_ack,
		struct firefly_memory_funcs *memory_replacements,
		struct firefly_event_queue *event_queue,
		void *plat_spec, transport_connection_free plat_spec_free)
{
	struct firefly_connection *conn;
	struct labcomm_encoder *transport_encoder;
	struct labcomm_decoder *transport_decoder;
	struct labcomm_reader  *reader;
	struct labcomm_writer  *writer;

	conn = FIREFLY_MALLOC(sizeof(*conn));
	if (conn == NULL) {
		firefly_error(FIREFLY_ERROR_ALLOC, 3,
			      "memory allocation failed %s:%d",
			      __FUNCTION__, __LINE__);
		FIREFLY_FREE(conn);
		return NULL;
	}
	conn->actions = actions;
	reader = transport_labcomm_reader_new(conn);
	writer = transport_labcomm_writer_new(conn);
	if (reader == NULL || writer == NULL) {
		firefly_error(FIREFLY_ERROR_ALLOC, 3,
			      "memory allocation failed %s:%d",
			      __FUNCTION__, __LINE__);
		transport_labcomm_reader_free(reader);
		transport_labcomm_writer_free(writer);
		FIREFLY_FREE(conn);
		return NULL;
	}
	transport_encoder = labcomm_encoder_new(writer, NULL);
	transport_decoder = labcomm_decoder_new(reader, NULL);

	if (transport_encoder == NULL || transport_decoder == NULL) {
		firefly_error(FIREFLY_ERROR_ALLOC, 3,
			      "memory allocation failed %s:%d",
			      __FUNCTION__, __LINE__);
		if (transport_encoder)
			labcomm_encoder_free(transport_encoder);
		else
			transport_labcomm_writer_free(writer);
		if (transport_decoder)
			labcomm_decoder_free(transport_decoder);
		else
			transport_labcomm_reader_free(reader);
		FIREFLY_FREE(conn);
		return NULL;
	}
	conn->event_queue		= event_queue;
	conn->chan_list			= NULL;
	conn->channel_id_counter	= 0;
	conn->transport_encoder		= transport_encoder;
	conn->transport_decoder		= transport_decoder;
	conn->transport_write		= transport_write;
	conn->transport_ack		= transport_ack;

	labcomm_register_error_handler_encoder(conn->transport_encoder,
			labcomm_error_to_ff_error);

	labcomm_register_error_handler_decoder(conn->transport_decoder,
			labcomm_error_to_ff_error);

	if (memory_replacements) {
		conn->memory_replacements.alloc_replacement =
			memory_replacements->alloc_replacement;
		conn->memory_replacements.free_replacement =
			memory_replacements->free_replacement;
	} else {
		conn->memory_replacements.alloc_replacement = NULL;
		conn->memory_replacements.free_replacement = NULL;
	}
	conn->transport_conn_platspec = plat_spec;
	conn->transport_conn_platspec_free = plat_spec_free;
	conn->open = FIREFLY_CONNECTION_OPEN;

	reg_proto_sigs(conn->transport_encoder,
		       conn->transport_decoder,
		       conn);

	return conn;
}

void firefly_connection_close(struct firefly_connection *conn)
{
	int ret;

	ret = conn->event_queue->offer_event_cb(conn->event_queue,
			FIREFLY_PRIORITY_MEDIUM, firefly_connection_close_event,
			conn);
	if (ret) {
		firefly_error(FIREFLY_ERROR_ALLOC, 1,
			      "could not add event to queue");
	}
}

int firefly_connection_close_event(void *event_arg)
{
	struct firefly_connection *conn;
	struct channel_list_node *head;
	bool empty = true;

	conn = event_arg;
	conn->open = FIREFLY_CONNECTION_CLOSED;

	head = conn->chan_list;
	while (head != NULL) {
		empty = false;
		firefly_channel_close(head->chan);
		head = head->next;
	}
	if (empty) {
		int ret;

		if (conn->transport_conn_platspec_free != NULL) {
			conn->transport_conn_platspec_free(conn);
		}
		ret = conn->event_queue->offer_event_cb(conn->event_queue,
				FIREFLY_PRIORITY_LOW,
				firefly_connection_free_event, conn);
		if (ret) {
			firefly_error(FIREFLY_ERROR_ALLOC, 1,
				      "could not add event to queue");
		}
	} else {
		firefly_connection_close(conn);
	}

	return 0;
}

int firefly_connection_free_event(void *event_arg)
{
	struct firefly_connection *conn;

	conn = event_arg;
	firefly_connection_free(&conn);

	return 0;
}

void firefly_connection_free(struct firefly_connection **conn)
{
	while ((*conn)->chan_list != NULL) {
		firefly_channel_closed_event((*conn)->chan_list->chan);
	}
	FIREFLY_FREE((*conn)->chan_list);
	if ((*conn)->transport_encoder != NULL) {
		labcomm_encoder_free((*conn)->transport_encoder);
	}
	if ((*conn)->transport_decoder != NULL) {
		labcomm_decoder_free((*conn)->transport_decoder);
	}
	FIREFLY_FREE(*conn);
	*conn = NULL;
}

struct firefly_channel *remove_channel_from_connection(
		struct firefly_channel *chan, struct firefly_connection *conn)
{
	struct firefly_channel *ret;
	struct channel_list_node **head;

	ret = NULL;
	head = &conn->chan_list;
	while (head != NULL && (*head) != NULL) {
		if ((*head)->chan->local_id == chan->local_id) {
			struct channel_list_node *tmp;

			tmp = (*head)->next;
			ret = chan;
			FIREFLY_FREE(*head);
			*head = tmp;
			head = NULL;
		} else {
			*head = (*head)->next;
		}
	}
	return ret;
}

int next_channel_id(struct firefly_connection *conn)
{
	return conn->channel_id_counter++;
}

void *firefly_connection_get_context(struct firefly_connection *conn)
{
	return conn->context;
}

/* TODO: At some point this might be done when creating a new conn. */
void firefly_connection_set_context(struct firefly_connection * const conn,
				    void * const context)
{
	conn->context = context;
}

struct firefly_event_queue *firefly_connection_get_event_queue(
		struct firefly_connection *conn)
{
	return conn->event_queue;
}
