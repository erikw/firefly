
#include <stdbool.h>

#include "CUnit/Basic.h"
#include <limits.h>
#include <labcomm.h>
#include <test/labcomm_mem_writer.h>
#include <test/labcomm_mem_reader.h>

#include <utils/firefly_event_queue.h>
#include <utils/cppmacros.h>
#include <protocol/firefly_protocol.h>
#include <gen/test.h>

#include <protocol/firefly_protocol_private.h>
#include "test/event_helper.h"
#include "test/proto_helper.h"

#define TEST_IMPORTANT_ID 1


extern struct labcomm_decoder *test_dec;
extern labcomm_mem_reader_context_t *test_dec_ctx;

extern struct labcomm_encoder *test_enc;
extern labcomm_mem_writer_context_t *test_enc_ctx;

extern firefly_protocol_data_sample data_sample;
extern firefly_protocol_ack ack;
extern firefly_protocol_channel_request channel_request;
extern firefly_protocol_channel_response channel_response;
extern firefly_protocol_channel_ack channel_ack;
extern bool received_ack;

struct firefly_event_queue *eq;

struct test_conn_platspec {
	bool important;
};

int init_suit_proto_important()
{
	init_labcomm_test_enc_dec();
	eq = firefly_event_queue_new(firefly_event_add, NULL);
	if (eq == NULL) {
		return 1;
	}
	return 0;
}

int clean_suit_proto_important()
{
	clean_labcomm_test_enc_dec();
	firefly_event_queue_free(&eq);
	return 0;
}

bool mock_transport_written = false;
void mock_transport_write_important(unsigned char *data, size_t size,
		struct firefly_connection *conn, bool important, unsigned char *id)
{
	UNUSED_VAR(data);
	UNUSED_VAR(size);
	mock_transport_written = true;
	struct test_conn_platspec *ps =
		(struct test_conn_platspec *) conn->transport_conn_platspec;
	CU_ASSERT_EQUAL(ps->important, important);
	if (important) {
		CU_ASSERT_PTR_NOT_NULL(id);
		*id = TEST_IMPORTANT_ID;
	}
	transport_write_test_decoder(data, size, conn, important, id);
}

bool mock_transport_acked = false;
void mock_transport_ack(unsigned char id, struct firefly_connection *conn)
{
	UNUSED_VAR(conn);
	CU_ASSERT_EQUAL(id, TEST_IMPORTANT_ID);
	mock_transport_acked = true;
}

void test_important_signature()
{
	struct test_conn_platspec ps;
	ps.important = true;
	struct firefly_connection *conn = firefly_connection_new(NULL, NULL, NULL,
			mock_transport_write_important, NULL, eq, &ps, NULL);
	struct firefly_channel *chan = firefly_channel_new(conn);
	add_channel_to_connection(chan, conn);

	CU_ASSERT_EQUAL(chan->current_seqno, 0);
	labcomm_encoder_register_test_test_var(
			firefly_protocol_get_output_stream(chan));
	event_execute_test(eq, 1);
	CU_ASSERT_TRUE(mock_transport_written);
	CU_ASSERT_TRUE(data_sample.important);
	CU_ASSERT_EQUAL(chan->important_id, TEST_IMPORTANT_ID);
	CU_ASSERT_EQUAL(chan->current_seqno, 1);

	mock_transport_written = false;
	firefly_connection_free(&conn);
}

void test_important_recv_ack()
{
	struct test_conn_platspec ps;
	ps.important = true;
	struct firefly_connection *conn = firefly_connection_new(NULL, NULL, NULL,
			mock_transport_write_important, mock_transport_ack, eq, &ps, NULL);
	struct firefly_channel *chan = firefly_channel_new(conn);
	add_channel_to_connection(chan, conn);

	chan->important_id = TEST_IMPORTANT_ID;
	chan->current_seqno = 1;

	firefly_protocol_ack ack_pkt;
	ack_pkt.dest_chan_id = chan->local_id;
	ack_pkt.src_chan_id = chan->remote_id;
	ack_pkt.seqno = 1;
	labcomm_encode_firefly_protocol_ack(test_enc, &ack_pkt);
	protocol_data_received(conn, test_enc_ctx->buf, test_enc_ctx->write_pos);
	test_enc_ctx->write_pos = 0;
	CU_ASSERT_TRUE(mock_transport_acked);
	CU_ASSERT_EQUAL(chan->important_id, 0);
	CU_ASSERT_EQUAL(chan->current_seqno, 1);

	mock_transport_written = false;
	mock_transport_acked = false;
	firefly_connection_free(&conn);
}

void test_important_signatures_mult()
{
	struct test_conn_platspec ps;
	ps.important = true;
	struct firefly_connection *conn = firefly_connection_new(NULL, NULL, NULL,
			mock_transport_write_important, NULL, eq, &ps, NULL);
	struct firefly_channel *chan = firefly_channel_new(conn);
	add_channel_to_connection(chan, conn);

	CU_ASSERT_EQUAL(chan->current_seqno, 0);
	labcomm_encoder_register_test_test_var(
			firefly_protocol_get_output_stream(chan));
	event_execute_test(eq, 1);
	CU_ASSERT_TRUE(mock_transport_written);
	CU_ASSERT_TRUE(data_sample.important);
	CU_ASSERT_EQUAL(chan->important_id, TEST_IMPORTANT_ID);
	CU_ASSERT_EQUAL(chan->current_seqno, 1);

	// simulate ack received
	chan->important_id = 0;

	labcomm_encoder_register_test_test_var_2(
			firefly_protocol_get_output_stream(chan));

	event_execute_test(eq, 1);
	CU_ASSERT_TRUE(mock_transport_written);
	CU_ASSERT_TRUE(data_sample.important);
	CU_ASSERT_EQUAL(chan->important_id, TEST_IMPORTANT_ID);
	CU_ASSERT_EQUAL(chan->current_seqno, 2);

	mock_transport_written = false;
	firefly_connection_free(&conn);
}

void test_important_seqno_overflow()
{
	struct test_conn_platspec ps;
	ps.important = true;
	struct firefly_connection *conn = firefly_connection_new(NULL, NULL, NULL,
			mock_transport_write_important, NULL, eq, &ps, NULL);
	struct firefly_channel *chan = firefly_channel_new(conn);
	add_channel_to_connection(chan, conn);

	chan->current_seqno = INT_MAX;
	labcomm_encoder_register_test_test_var(
			firefly_protocol_get_output_stream(chan));
	event_execute_test(eq, 1);
	CU_ASSERT_TRUE(mock_transport_written);
	CU_ASSERT_TRUE(data_sample.important);
	CU_ASSERT_EQUAL(chan->important_id, TEST_IMPORTANT_ID);
	CU_ASSERT_EQUAL(chan->current_seqno, 1);

	mock_transport_written = false;
	firefly_connection_free(&conn);
}

void test_important_send_ack()
{
	struct test_conn_platspec ps;
	ps.important = true;
	struct firefly_connection *conn = firefly_connection_new(NULL, NULL, NULL,
			transport_write_test_decoder, NULL, eq, &ps, NULL);
	struct firefly_channel *chan = firefly_channel_new(conn);
	add_channel_to_connection(chan, conn);

	firefly_protocol_data_sample sample_pkt;
	sample_pkt.dest_chan_id = chan->local_id;
	sample_pkt.src_chan_id = chan->remote_id;
	sample_pkt.seqno = 1;
	sample_pkt.important = true;
	sample_pkt.app_enc_data.a = NULL;
	sample_pkt.app_enc_data.n_0 = 0;
	labcomm_encode_firefly_protocol_data_sample(test_enc, &sample_pkt);
	protocol_data_received(conn, test_enc_ctx->buf, test_enc_ctx->write_pos);
	test_enc_ctx->write_pos = 0;
	event_execute_test(eq, 1);

	CU_ASSERT_TRUE(received_ack);
	CU_ASSERT_EQUAL(ack.seqno, 1);
	CU_ASSERT_EQUAL(chan->remote_seqno, 1);

	received_ack = false;
	firefly_connection_free(&conn);
}

void test_not_important_not_send_ack()
{
	struct test_conn_platspec ps;
	ps.important = true;
	struct firefly_connection *conn = firefly_connection_new(NULL, NULL, NULL,
			transport_write_test_decoder, NULL, eq, &ps, NULL);
	struct firefly_channel *chan = firefly_channel_new(conn);
	add_channel_to_connection(chan, conn);

	firefly_protocol_data_sample sample_pkt;
	sample_pkt.dest_chan_id = chan->local_id;
	sample_pkt.src_chan_id = chan->remote_id;
	sample_pkt.seqno = 0;
	sample_pkt.important = false;
	sample_pkt.app_enc_data.a = NULL;
	sample_pkt.app_enc_data.n_0 = 0;
	labcomm_encode_firefly_protocol_data_sample(test_enc, &sample_pkt);
	protocol_data_received(conn, test_enc_ctx->buf, test_enc_ctx->write_pos);
	test_enc_ctx->write_pos = 0;
	event_execute_test(eq, 1);

	CU_ASSERT_FALSE(received_ack);

	received_ack = false;
	firefly_connection_free(&conn);
}

void test_important_mult_simultaneously()
{
	struct test_conn_platspec ps;
	ps.important = true;
	struct firefly_connection *conn = firefly_connection_new(NULL, NULL, NULL,
			mock_transport_write_important, mock_transport_ack, eq, &ps, NULL);
	struct firefly_channel *chan = firefly_channel_new(conn);
	add_channel_to_connection(chan, conn);

	firefly_protocol_ack ack_pkt;
	ack_pkt.dest_chan_id = chan->local_id;
	ack_pkt.src_chan_id = chan->remote_id;

	CU_ASSERT_EQUAL(chan->current_seqno, 0);
	labcomm_encoder_register_test_test_var(
			firefly_protocol_get_output_stream(chan));

	labcomm_encoder_register_test_test_var_2(
			firefly_protocol_get_output_stream(chan));

	labcomm_encoder_register_test_test_var_3(
			firefly_protocol_get_output_stream(chan));

	event_execute_all_test(eq);
	CU_ASSERT_TRUE(mock_transport_written);
	CU_ASSERT_TRUE(data_sample.important);
	CU_ASSERT_EQUAL(chan->important_id, TEST_IMPORTANT_ID);
	CU_ASSERT_EQUAL(chan->current_seqno, 1);
	mock_transport_written = false;

	ack_pkt.seqno = 1;
	labcomm_encode_firefly_protocol_ack(test_enc, &ack_pkt);
	protocol_data_received(conn, test_enc_ctx->buf, test_enc_ctx->write_pos);
	test_enc_ctx->write_pos = 0;

	event_execute_all_test(eq);

	CU_ASSERT_TRUE(mock_transport_written);
	CU_ASSERT_TRUE(data_sample.important);
	CU_ASSERT_EQUAL(chan->important_id, TEST_IMPORTANT_ID);
	CU_ASSERT_EQUAL(chan->current_seqno, 2);
	mock_transport_written = false;

	ack_pkt.seqno = 2;
	labcomm_encode_firefly_protocol_ack(test_enc, &ack_pkt);
	protocol_data_received(conn, test_enc_ctx->buf, test_enc_ctx->write_pos);
	test_enc_ctx->write_pos = 0;

	event_execute_all_test(eq);

	CU_ASSERT_TRUE(mock_transport_written);
	CU_ASSERT_TRUE(data_sample.important);
	CU_ASSERT_EQUAL(chan->important_id, TEST_IMPORTANT_ID);
	CU_ASSERT_EQUAL(chan->current_seqno, 3);
	mock_transport_written = false;

	ack_pkt.seqno = 3;
	labcomm_encode_firefly_protocol_ack(test_enc, &ack_pkt);
	protocol_data_received(conn, test_enc_ctx->buf, test_enc_ctx->write_pos);
	test_enc_ctx->write_pos = 0;

	event_execute_all_test(eq);

	CU_ASSERT_FALSE(mock_transport_written);
	CU_ASSERT_EQUAL(chan->important_id, 0);
	CU_ASSERT_EQUAL(chan->current_seqno, 3);

	mock_transport_written = false;
	firefly_connection_free(&conn);

}

void test_errorneous_ack()
{
	struct test_conn_platspec ps;
	ps.important = false;
	struct firefly_connection *conn = firefly_connection_new(NULL, NULL, NULL,
			transport_write_test_decoder, NULL, eq, &ps, NULL);
	struct firefly_channel *chan = firefly_channel_new(conn);
	add_channel_to_connection(chan, conn);

	chan->current_seqno = 5;
	chan->important_id = TEST_IMPORTANT_ID;

	firefly_protocol_ack ack_pkt;
	ack_pkt.dest_chan_id = chan->local_id;
	ack_pkt.src_chan_id = chan->remote_id;
	ack_pkt.seqno = 4;
	labcomm_encode_firefly_protocol_ack(test_enc, &ack_pkt);
	protocol_data_received(conn, test_enc_ctx->buf, test_enc_ctx->write_pos);
	test_enc_ctx->write_pos = 0;
	CU_ASSERT_FALSE(mock_transport_acked);
	CU_ASSERT_EQUAL(chan->important_id, TEST_IMPORTANT_ID);
	CU_ASSERT_EQUAL(chan->current_seqno, 5);

	ack_pkt.seqno = 6;
	labcomm_encode_firefly_protocol_ack(test_enc, &ack_pkt);
	protocol_data_received(conn, test_enc_ctx->buf, test_enc_ctx->write_pos);
	test_enc_ctx->write_pos = 0;
	CU_ASSERT_FALSE(mock_transport_acked);
	CU_ASSERT_EQUAL(chan->important_id, TEST_IMPORTANT_ID);
	CU_ASSERT_EQUAL(chan->current_seqno, 5);

	mock_transport_written = false;
	mock_transport_acked = false;
	firefly_connection_free(&conn);
}

void handle_test_var_error(test_test_var *d, void *context)
{
	UNUSED_VAR(d);
	UNUSED_VAR(context);
	CU_FAIL("Received test var data but should not have");
}

void test_important_recv_duplicate()
{
	struct test_conn_platspec ps;
	ps.important = false;
	struct firefly_connection *conn = firefly_connection_new(NULL, NULL, NULL,
			transport_write_test_decoder, NULL, eq, &ps, NULL);
	struct firefly_channel *chan = firefly_channel_new(conn);
	add_channel_to_connection(chan, conn);

	CU_ASSERT_EQUAL(chan->remote_seqno, 0);

	labcomm_decoder_register_test_test_var(
			firefly_protocol_get_input_stream(chan), handle_test_var_error, NULL);
	labcomm_encoder_register_test_test_var(test_enc);
	firefly_protocol_data_sample sample_pkt;
	sample_pkt.dest_chan_id = chan->local_id;
	sample_pkt.src_chan_id = chan->remote_id;
	sample_pkt.seqno = 1;
	sample_pkt.important = true;
	sample_pkt.app_enc_data.a = malloc(test_enc_ctx->write_pos);
	memcpy(sample_pkt.app_enc_data.a, test_enc_ctx->buf,
			test_enc_ctx->write_pos);
	sample_pkt.app_enc_data.n_0 = test_enc_ctx->write_pos;
	test_enc_ctx->write_pos = 0;
	labcomm_encode_firefly_protocol_data_sample(test_enc, &sample_pkt);
	protocol_data_received(conn, test_enc_ctx->buf, test_enc_ctx->write_pos);
	test_enc_ctx->write_pos = 0;
	event_execute_test(eq, 1);

	CU_ASSERT_TRUE(received_ack);
	CU_ASSERT_EQUAL(ack.seqno, 1);
	CU_ASSERT_EQUAL(chan->remote_seqno, 1);

	free(sample_pkt.app_enc_data.a);

	test_test_var app_data = 1;
	labcomm_encode_test_test_var(test_enc, &app_data);
	sample_pkt.dest_chan_id = chan->local_id;
	sample_pkt.src_chan_id = chan->remote_id;
	sample_pkt.seqno = 1;
	sample_pkt.important = true;
	sample_pkt.app_enc_data.a = malloc(test_enc_ctx->write_pos);
	memcpy(sample_pkt.app_enc_data.a, test_enc_ctx->buf,
			test_enc_ctx->write_pos);
	sample_pkt.app_enc_data.n_0 = test_enc_ctx->write_pos;
	test_enc_ctx->write_pos = 0;
	labcomm_encode_firefly_protocol_data_sample(test_enc, &sample_pkt);

	protocol_data_received(conn, test_enc_ctx->buf, test_enc_ctx->write_pos);
	test_enc_ctx->write_pos = 0;
	event_execute_test(eq, 1);

	CU_ASSERT_TRUE(received_ack);
	CU_ASSERT_EQUAL(ack.seqno, 1);
	CU_ASSERT_EQUAL(chan->remote_seqno, 1);

	free(sample_pkt.app_enc_data.a);

	mock_transport_written = false;
	mock_transport_acked = false;
	firefly_connection_free(&conn);
}

bool handshake_chan_open_called = false;
void important_handshake_chan_open(struct firefly_channel *chan)
{
	CU_ASSERT_EQUAL(chan->important_id, 0);
	handshake_chan_open_called = true;
}

bool important_handshake_chan_acc(struct firefly_channel *chan)
{
	UNUSED_VAR(chan);
	return true;
}

void test_important_handshake_recv()
{
	struct test_conn_platspec ps;
	ps.important = true;
	struct firefly_connection *conn = firefly_connection_new(
			important_handshake_chan_open, NULL, important_handshake_chan_acc,
			mock_transport_write_important, mock_transport_ack, eq, &ps, NULL);

	firefly_protocol_channel_request req_pkt;
	req_pkt.source_chan_id = 1;
	req_pkt.dest_chan_id = CHANNEL_ID_NOT_SET;
	test_enc_ctx->write_pos = 0;
	labcomm_encode_firefly_protocol_channel_request(test_enc, &req_pkt);
	protocol_data_received(conn, test_enc_ctx->buf, test_enc_ctx->write_pos);
	test_enc_ctx->write_pos = 0;
	event_execute_test(eq, 1);

	CU_ASSERT_TRUE(mock_transport_written);
	mock_transport_written = false;

	firefly_protocol_channel_ack ack_pkt;
	ack_pkt.source_chan_id = 1;
	ack_pkt.dest_chan_id = channel_response.source_chan_id;
	labcomm_encode_firefly_protocol_channel_ack(test_enc, &ack_pkt);
	protocol_data_received(conn, test_enc_ctx->buf, test_enc_ctx->write_pos);
	test_enc_ctx->write_pos = 0;
	event_execute_test(eq, 1);
	CU_ASSERT_FALSE(mock_transport_written);
	CU_ASSERT_TRUE(mock_transport_acked);
	CU_ASSERT_TRUE(handshake_chan_open_called);


	handshake_chan_open_called = false;
	mock_transport_acked = false;
	mock_transport_written = false;
	firefly_connection_free(&conn);
}

void test_important_handshake_open()
{
	struct test_conn_platspec ps;
	ps.important = true;
	struct firefly_connection *conn = firefly_connection_new(
			important_handshake_chan_open, NULL, important_handshake_chan_acc,
			mock_transport_write_important, mock_transport_ack, eq, &ps, NULL);

	firefly_channel_open(conn, NULL);
	event_execute_test(eq, 1);

	CU_ASSERT_TRUE(mock_transport_written);
	mock_transport_written = false;

	firefly_protocol_channel_response res_pkt;
	res_pkt.source_chan_id = 1;
	res_pkt.dest_chan_id = channel_request.source_chan_id;
	res_pkt.ack = true;
	test_enc_ctx->write_pos = 0;
	labcomm_encode_firefly_protocol_channel_response(test_enc, &res_pkt);
	protocol_data_received(conn, test_enc_ctx->buf, test_enc_ctx->write_pos);
	test_enc_ctx->write_pos = 0;
	ps.important = false;
	event_execute_test(eq, 1);

	CU_ASSERT_TRUE(mock_transport_written);
	CU_ASSERT_TRUE(mock_transport_acked);
	mock_transport_written = false;
	CU_ASSERT_TRUE(handshake_chan_open_called);

	handshake_chan_open_called = false;
	mock_transport_acked = false;
	mock_transport_written = false;
	firefly_connection_free(&conn);
}
