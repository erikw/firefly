// Must be the first include to get the XSI-compliant version of the strerror_r
// function.
#ifdef _GNU_SOURCE
#error "Something turned it on!"
#undef _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE (200112L)
#include <pthread.h>
#include <string.h>

#include <transport/firefly_transport_udp_posix.h>
#include "firefly_transport_udp_posix_private.h"

#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <signal.h>

#include <transport/firefly_transport.h>
#include <utils/firefly_errors.h>
#include <utils/firefly_event_queue.h>

#include <utils/firefly_resend_posix.h>
#include "utils/firefly_event_queue_private.h"
#include "protocol/firefly_protocol_private.h"
#include "transport/firefly_transport_private.h"
#include "utils/cppmacros.h"

#define ERROR_STR_MAX_LEN      (256)
#define SCALE_BACK_NBR_DEFAULT (32)

struct firefly_transport_llp *firefly_transport_llp_udp_posix_new(
	unsigned short local_udp_port,
	firefly_on_conn_recv_pudp on_conn_recv,
	struct firefly_event_queue *event_queue)
{
	struct transport_llp_udp_posix *llp_udp;

	llp_udp = malloc(sizeof(*llp_udp));
	llp_udp->local_addr = calloc(1, sizeof(struct sockaddr_in));
	if (llp_udp == NULL || llp_udp->local_addr == NULL) {
		firefly_error(FIREFLY_ERROR_ALLOC, 2,
					  "Failed in %s().\n", __FUNCTION__);
	}

	llp_udp->local_addr->sin_family = AF_INET;
	llp_udp->local_addr->sin_port = htons(local_udp_port);
	llp_udp->local_addr->sin_addr.s_addr = htonl(INADDR_ANY);

	llp_udp->local_udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (llp_udp->local_udp_socket == -1) {
		char err_buf[ERROR_STR_MAX_LEN];
		strerror_r(errno, err_buf, ERROR_STR_MAX_LEN);
		firefly_error(FIREFLY_ERROR_SOCKET, 3,
				"Failed in %s().\n%s\n", __FUNCTION__, err_buf);
	}
	int res = bind(llp_udp->local_udp_socket,
			(struct sockaddr *) llp_udp->local_addr,
			sizeof(struct sockaddr_in));
	if (res == -1) {
		char err_buf[ERROR_STR_MAX_LEN];
		strerror_r(errno, err_buf, ERROR_STR_MAX_LEN);
		firefly_error(FIREFLY_ERROR_LLP_BIND, 3,
				"Failed in %s().\n%s\n", __FUNCTION__, err_buf);
	}
	llp_udp->on_conn_recv = on_conn_recv;
	llp_udp->event_queue = event_queue;
	llp_udp->resend_queue = firefly_resend_queue_new();

	struct firefly_transport_llp *llp = malloc(sizeof(*llp));

	if (llp == NULL) {
		firefly_error(FIREFLY_ERROR_ALLOC, 2,
					  "Failed in %s().\n", __FUNCTION__);
	}

	llp->llp_platspec = llp_udp;
	llp->conn_list = NULL;
	llp->protocol_data_received_cb = protocol_data_received;

	return llp;
}

void firefly_transport_llp_udp_posix_free(struct firefly_transport_llp *llp)
{
	struct transport_llp_udp_posix *llp_udp =
		(struct transport_llp_udp_posix *) llp->llp_platspec;

	int ret = llp_udp->event_queue->offer_event_cb(llp_udp->event_queue,
			FIREFLY_PRIORITY_LOW, firefly_transport_llp_udp_posix_free_event,
			llp);
	if (ret) {
		firefly_error(FIREFLY_ERROR_ALLOC, 1,
					  "could not add event to queue");
	}
}

int firefly_transport_llp_udp_posix_free_event(void *event_arg)
{
	struct firefly_transport_llp *llp =
		(struct firefly_transport_llp *) event_arg;
	struct transport_llp_udp_posix *llp_udp =
		(struct transport_llp_udp_posix *) llp->llp_platspec;

	bool empty = true;
	// Close all connections.
	struct llp_connection_list_node *head = llp->conn_list;
	while (head != NULL) {
		empty = false;
		firefly_connection_close(head->conn);
		head = head->next;
	}
	if (empty) {
		close(llp_udp->local_udp_socket);
		free(llp_udp->local_addr);
		firefly_resend_queue_free(llp_udp->resend_queue);
		free(llp_udp);
		free(llp);
	} else {
		firefly_transport_llp_udp_posix_free(llp);
	}
	return 0;
}

struct firefly_connection *firefly_connection_udp_posix_new(
		struct firefly_transport_llp *llp,
		struct sockaddr_in *remote_addr,
		unsigned int timeout,
		struct firefly_connection_actions *actions)
{
	struct transport_llp_udp_posix *llp_udp;
	struct firefly_connection *conn;
	struct protocol_connection_udp_posix *conn_udp;

	llp_udp = llp->llp_platspec;
	conn_udp = malloc(sizeof(*conn_udp));

	conn = firefly_connection_new(actions,
			firefly_transport_udp_posix_write,
			firefly_transport_udp_posix_ack,
			NULL,
			llp_udp->event_queue, conn_udp,
			firefly_transport_connection_udp_posix_free);

	if (conn == NULL || conn_udp == NULL) {
		firefly_error(FIREFLY_ERROR_ALLOC, 3,
				"Failed in %s() on line %d.\n", __FUNCTION__,
				__LINE__);
		free(conn);
		free(conn_udp);
		return NULL;
	}

	conn_udp->remote_addr = remote_addr;
	conn_udp->socket = llp_udp->local_udp_socket;
	conn_udp->llp = llp;
	conn_udp->timeout = timeout;

	return conn;
}

void firefly_transport_connection_udp_posix_free(
		struct firefly_connection *conn)
{
	struct protocol_connection_udp_posix *conn_udp;
	conn_udp =
		(struct protocol_connection_udp_posix *) conn->transport_conn_platspec;

	remove_connection_from_llp(conn_udp->llp, conn,
			firefly_connection_eq_ptr);
	free(conn_udp->remote_addr);
	free(conn_udp);
}

struct firefly_connection *firefly_transport_connection_udp_posix_open(
		struct firefly_transport_llp *llp,
		const char *remote_ip_addr,
		unsigned short remote_port,
		unsigned int timeout,
		struct firefly_connection_actions *actions)
{
	struct firefly_connection *conn;
	struct sockaddr_in *remote_addr;
	int res;

	remote_addr = calloc(1, sizeof(struct sockaddr_in));
	if (remote_addr == NULL) {
		firefly_error(FIREFLY_ERROR_ALLOC, 3,
				"Failed in %s() on line %d.\n", __FUNCTION__,
				__LINE__);
		return NULL;
	}
	remote_addr->sin_family = AF_INET;
	remote_addr->sin_port = htons(remote_port);
	res = inet_pton(AF_INET, remote_ip_addr, &remote_addr->sin_addr);
	if (res == 0) {
		firefly_error(FIREFLY_ERROR_IP_PARSE, 3,
				"Failed to convert IP address in %s() on line"
				"%d.\n", __FUNCTION__, __LINE__);
		free(remote_addr);
		return NULL;
	}

	conn = firefly_connection_udp_posix_new(llp, remote_addr, timeout, actions);

	if (conn != NULL) {
		add_connection_to_llp(conn, llp);
	}

	return conn;
}

void firefly_transport_udp_posix_ack(unsigned char pkt_id,
		struct firefly_connection *conn)
{
	struct protocol_connection_udp_posix *conn_udp =
		(struct protocol_connection_udp_posix *)
		conn->transport_conn_platspec;
	firefly_resend_remove(((struct transport_llp_udp_posix *)
					conn_udp->llp->llp_platspec)->resend_queue, pkt_id);
}

void firefly_transport_udp_posix_write(unsigned char *data, size_t data_size,
		struct firefly_connection *conn, bool important, unsigned char *id)
{
	struct protocol_connection_udp_posix *conn_udp =
		(struct protocol_connection_udp_posix *)
		conn->transport_conn_platspec;
	int res = sendto(conn_udp->socket, data, data_size, 0,
			 (struct sockaddr *) conn_udp->remote_addr,
			 sizeof(*conn_udp->remote_addr));
	if (res == -1) {
		firefly_error(FIREFLY_ERROR_TRANS_WRITE, 2, "Failed in %s().\n",
			__FUNCTION__);
	}
	if (important) {
		if (id != NULL) {
			unsigned char *new_data = malloc(data_size);
			memcpy(new_data, data, data_size);
			*id = firefly_resend_add(((struct transport_llp_udp_posix *)
					conn_udp->llp->llp_platspec)->resend_queue,
					new_data, data_size, conn_udp->timeout,
					FIREFLY_TRANSPORT_UDP_POSIX_DEFAULT_RETRIES, conn);
		} else {
			firefly_error(FIREFLY_ERROR_TRANS_WRITE, 1,
					"Parameter id was NULL.\n");
		}
	}
}

void *firefly_transport_udp_posix_resend(void *args)
{
	struct resend_queue *rq = (struct resend_queue *) args;
	unsigned char *data;
	size_t size;
	struct firefly_connection *conn;
	unsigned char id;
	int res;
	while (true) { // Change to some finite value
		int prev_state;
		res = firefly_resend_wait(rq, &data, &size, &conn, &id);
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &prev_state);
		if (res < 0) {
			firefly_connection_close(conn);
		} else {
			// TODO fix better way to resend, imprtant = false and id = NULL
			// might break inthe future
			firefly_transport_udp_posix_write(data, size, conn,
					false, NULL);
			free(data);
			firefly_resend_readd(rq, id,
					((struct protocol_connection_udp_posix *)
					conn->transport_conn_platspec)->timeout);
		}
		pthread_setcancelstate(prev_state, NULL);
	}
	return NULL;
}

void *firefly_transport_udp_posix_read_run(void *args)
{
	struct firefly_transport_llp *llp = (struct firefly_transport_llp *) args;
	while (true)
		firefly_transport_udp_posix_read(llp);
	return NULL;
}

int firefly_transport_udp_posix_run(struct firefly_transport_llp *llp,
		pthread_t *reader, pthread_t *resend)
{
	int res;
	struct transport_llp_udp_posix *llp_udp =
		(struct transport_llp_udp_posix *) llp->llp_platspec;
	if (reader != NULL) {
		res = pthread_create(reader, NULL, firefly_transport_udp_posix_read_run,
				llp);
		if (res < 0) {
			return res;
		}
	}
	if (resend != NULL) {
		res = pthread_create(resend, NULL, firefly_transport_udp_posix_resend,
				llp_udp->resend_queue);
		if (res < 0) {
			if (reader != NULL) {
				pthread_cancel(*reader);
			}
			return res;
		}
	}
	return 0;
}

int firefly_transport_udp_posix_stop(struct firefly_transport_llp *llp,
		pthread_t *reader, pthread_t *resend)
{
	UNUSED_VAR(llp);
	if (reader != NULL) {
		pthread_cancel(*reader);
		pthread_join(*reader, NULL);
	}
	if (resend != NULL) {
		pthread_cancel(*resend);
		pthread_join(*resend, NULL);
	}
	return 0;
}

void firefly_transport_udp_posix_read(struct firefly_transport_llp *llp)
{
	struct transport_llp_udp_posix *llp_udp =
		(struct transport_llp_udp_posix *) llp->llp_platspec;
	int res;

	fd_set fs;
	FD_ZERO(&fs);
	FD_SET(llp_udp->local_udp_socket, &fs);
	res = select(llp_udp->local_udp_socket + 1, &fs, NULL, NULL, NULL);
	if (res == -1) {
		firefly_error(FIREFLY_ERROR_SOCKET, 3,
				"Failed in %s() on line %d.\n", __FUNCTION__,
				__LINE__);
	}

	// Read data from socket, = 0 is crucial due to ioctl only sets the
	// first 32 bits of pkg_len
	size_t pkg_len = 0;
	res = ioctl(llp_udp->local_udp_socket, FIONREAD, &pkg_len);
	if (res == -1) {
		firefly_error(FIREFLY_ERROR_SOCKET, 3,
				"Failed in %s() on line %d.\n", __FUNCTION__,
				__LINE__);
		pkg_len = 0;
	}
	socklen_t len = sizeof(struct sockaddr_in);
	struct sockaddr_in *remote_addr = malloc(len);
	if (remote_addr == NULL) {
			firefly_error(FIREFLY_ERROR_ALLOC, 3,
					"Failed in %s() on line %d.\n",
					__FUNCTION__, __LINE__);
	}
	unsigned char *data = malloc(pkg_len);
	if (data == NULL) {
			firefly_error(FIREFLY_ERROR_ALLOC, 3,
					"Failed in %s() on line %d.\n",
					__FUNCTION__, __LINE__);
	}
	res = recvfrom(llp_udp->local_udp_socket, data, pkg_len, 0,
			(struct sockaddr *) remote_addr, &len);
	if (res == -1) {
		char err_buf[ERROR_STR_MAX_LEN];
		strerror_r(errno, err_buf, ERROR_STR_MAX_LEN);
		firefly_error(FIREFLY_ERROR_SOCKET, 3,
				"Failed in %s.\n%s()\n", __FUNCTION__, err_buf);
	}

	struct firefly_event_llp_read_udp_posix *ev_arg =
		malloc(sizeof(struct firefly_event_llp_read_udp_posix));
	if (ev_arg == NULL) {
			firefly_error(FIREFLY_ERROR_ALLOC, 3,
					"Failed in %s() on line %d.\n",
					__FUNCTION__, __LINE__);
	}
	ev_arg->llp = llp;
	ev_arg->addr = remote_addr;
	ev_arg->data = data;
	ev_arg->len = pkg_len;

	llp_udp->event_queue->offer_event_cb(llp_udp->event_queue,
			FIREFLY_PRIORITY_HIGH, firefly_transport_udp_posix_read_event,
			ev_arg);
}

int firefly_transport_udp_posix_read_event(void *event_arg)
{
	struct firefly_event_llp_read_udp_posix *ev_arg =
		(struct firefly_event_llp_read_udp_posix *) event_arg;
	struct transport_llp_udp_posix *llp_udp =
		(struct transport_llp_udp_posix *) ev_arg->llp->llp_platspec;

	// Find existing connection or create new.
	struct firefly_connection *conn;
	conn = find_connection(ev_arg->llp, ev_arg->addr, connection_eq_inaddr);
	if (conn == NULL) {
		if (llp_udp->on_conn_recv != NULL) {
			char ip_addr[INET_ADDRSTRLEN];
			sockaddr_in_ipaddr(ev_arg->addr, ip_addr);
			conn = llp_udp->on_conn_recv(ev_arg->llp, ip_addr,
					sockaddr_in_port(ev_arg->addr));
		} else {
			firefly_error(FIREFLY_ERROR_MISSING_CALLBACK, 4,
				      "Cannot accept incoming connection "
				      "on port %d.\n"
				      "Callback 'on_conn_recv' not set"
				      "on llp.\n (in %s() at %s:%d)",
				      ntohs(llp_udp->local_addr->sin_port),
				      __FUNCTION__, __FILE__, __LINE__);
		}
	}

	// Existing or newly created conn. Passing data to procol layer.
	if (conn != NULL) {
		ev_arg->llp->protocol_data_received_cb(conn, ev_arg->data, ev_arg->len);
	}

	free(ev_arg->data);
	free(ev_arg->addr);
	free(ev_arg);
	return 0;
}

bool sockaddr_in_eq(struct sockaddr_in *one, struct sockaddr_in *other)
{
	return memcmp(&one->sin_port, &other->sin_port,
			sizeof(one->sin_port)) == 0 &&
		memcmp(&one->sin_addr, &other->sin_addr,
			sizeof(one->sin_addr)) == 0;

}

bool connection_eq_inaddr(struct firefly_connection *conn, void *context)
{
	return sockaddr_in_eq(((struct protocol_connection_udp_posix *) conn->transport_conn_platspec)->remote_addr, (struct sockaddr_in *) context);
}

void sockaddr_in_ipaddr(struct sockaddr_in *addr, char *ip_addr)
{
	inet_ntop(AF_INET, &addr->sin_addr.s_addr, ip_addr, INET_ADDRSTRLEN);
}

unsigned short sockaddr_in_port(struct sockaddr_in *addr)
{
	return ntohs(addr->sin_port);
}
