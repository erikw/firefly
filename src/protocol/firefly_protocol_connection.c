/**
 * @file
 * @brief Connection related functions for the protocol layer.
 */

#include <protocol/firefly_protocol.h>
#include <utils/firefly_errors.h>

#include "protocol/firefly_protocol_private.h"
#include "utils/firefly_event_queue_private.h"

struct firefly_connection *firefly_connection_new_register(
		firefly_channel_is_open_f on_channel_opened,
		firefly_channel_closed_f on_channel_closed,
		firefly_channel_accept_f on_channel_recv,
		transport_write_f transport_write,
		struct firefly_event_queue *event_queue, void *plat_spec,
		transport_connection_free plat_spec_free, bool reg)
{
	struct firefly_connection *conn =
		malloc(sizeof(struct firefly_connection));
	if (conn == NULL) {
		firefly_error(FIREFLY_ERROR_ALLOC, 1, "malloc failed\n");
	}
	conn->event_queue = event_queue;
	// Init writer data
	struct ff_transport_data *writer_data =
				malloc(sizeof(struct ff_transport_data));
	if (writer_data == NULL) {
		firefly_error(FIREFLY_ERROR_ALLOC, 1, "malloc failed\n");
	}
	writer_data->data = malloc(BUFFER_SIZE);
	if (writer_data->data == NULL) {
		firefly_error(FIREFLY_ERROR_ALLOC, 1, "malloc failed\n");
	}
	writer_data->data_size = BUFFER_SIZE;
	writer_data->pos = 0;
	conn->writer_data = writer_data;

	// Init reader data
	struct ff_transport_data *reader_data =
				malloc(sizeof(struct ff_transport_data));
	if (reader_data == NULL) {
		firefly_error(FIREFLY_ERROR_ALLOC, 1, "malloc failed\n");
	}
	reader_data->data = NULL;
	reader_data->data_size = 0;
	reader_data->pos = 0;
	conn->reader_data = reader_data;

	conn->on_channel_opened = on_channel_opened;
	conn->on_channel_closed = on_channel_closed;
	conn->on_channel_recv = on_channel_recv;
	conn->chan_list = NULL;
	conn->channel_id_counter = 0;
	conn->transport_encoder =
		labcomm_encoder_new(ff_transport_writer, conn);
	if (conn->transport_encoder == NULL) {
		firefly_error(FIREFLY_ERROR_ALLOC, 1, "malloc failed\n");
	}
	labcomm_register_error_handler_encoder(conn->transport_encoder,
			labcomm_error_to_ff_error);

	conn->transport_decoder =
		labcomm_decoder_new(ff_transport_reader, conn);
	if (conn->transport_decoder == NULL) {
		firefly_error(FIREFLY_ERROR_ALLOC, 1, "malloc failed\n");
	}
	labcomm_register_error_handler_decoder(conn->transport_decoder,
			labcomm_error_to_ff_error);

	conn->transport_write = transport_write;
	conn->transport_conn_platspec = plat_spec;
	conn->transport_conn_platspec_free = plat_spec_free;

	if (reg) {
	reg_proto_sigs(conn->transport_encoder,
				   conn->transport_decoder,
				   conn);
	}

	conn->open = FIREFLY_CONNECTION_OPEN;

	return conn;
}

// TODO do register better
struct firefly_connection *firefly_connection_new(
		firefly_channel_is_open_f on_channel_opened,
		firefly_channel_closed_f on_channel_closed,
		firefly_channel_accept_f on_channel_recv,
		transport_write_f transport_write,
		struct firefly_event_queue *event_queue,
		void *plat_spec, transport_connection_free plat_spec_free)
{
	return firefly_connection_new_register(on_channel_opened,
										   on_channel_closed,
										   on_channel_recv,
										   transport_write,
										   event_queue,
										   plat_spec, plat_spec_free,
										   false);
}

void firefly_connection_close(struct firefly_connection *conn)
{
	conn->open = FIREFLY_CONNECTION_CLOSED;
	struct firefly_event *ev = firefly_event_new(FIREFLY_PRIORITY_MEDIUM,
			firefly_connection_close_event, conn);

	int ret = conn->event_queue->offer_event_cb(conn->event_queue, ev);
	if (ret) {
		firefly_error(FIREFLY_ERROR_ALLOC, 1,
					  "could not add event to queue");
	}
}

int firefly_connection_close_event(void *event_arg)
{
	struct firefly_connection *conn = (struct firefly_connection *) event_arg;

	struct channel_list_node *head = conn->chan_list;
	bool empty = true;
	while (head != NULL) {
		empty = false;
		firefly_channel_close(head->chan);
		head = head->next;
	}
	if (empty) {
		firefly_connection_free(&conn);
	} else {
		firefly_connection_close(conn);
	}
	return 0;
}

void firefly_connection_free(struct firefly_connection **conn)
{
	while ((*conn)->chan_list != NULL) {
		firefly_channel_closed_event((*conn)->chan_list->chan);
	}
	free((*conn)->chan_list);
	if ((*conn)->transport_conn_platspec_free != NULL) {
		(*conn)->transport_conn_platspec_free(*conn);
	}
	if ((*conn)->transport_encoder != NULL) {
		labcomm_encoder_free((*conn)->transport_encoder);
	}
	if ((*conn)->transport_decoder != NULL) {
		labcomm_decoder_free((*conn)->transport_decoder);
	}
	free((*conn)->reader_data);
	free((*conn)->writer_data->data);
	free((*conn)->writer_data);
	free((*conn));
	*conn = NULL;
}

struct firefly_channel *remove_channel_from_connection(
		struct firefly_channel *chan, struct firefly_connection *conn)
{
	struct firefly_channel *ret = NULL;
	struct channel_list_node **head = &conn->chan_list;
	while (head !=NULL && (*head) != NULL) {
		if ((*head)->chan->local_id == chan->local_id) {
			ret = chan;
			struct channel_list_node *tmp = (*head)->next;
			free(*head);
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
