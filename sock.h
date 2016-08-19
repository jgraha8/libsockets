#ifndef __SOCK_H__
#define __SOCK_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdint.h>

#define SOCK_OPTS_REQ_WPORT 0b0001

#define SOCK_SF_PARENT  0b0001
#define SOCK_SF_MASTER  0b0010
#define SOCK_SF_WORKER  0b0100
#define SOCK_SF_CPARENT 0b1000

// Forward declarations
typedef struct comm_channel_s comm_channel_t;

typedef struct sock_tcp_header_s {
	uint32_t msg_len; // Length of message (limited to 4GB)
	unsigned char opts; // Bit vector of options
} sock_tcp_header_t;

typedef struct sock_server_s {
	unsigned char flags;
	int fd;
	struct sockaddr_in addr;
	comm_channel_t *cc_client;
	size_t ntrans;
	struct sock_server_s *worker;
} sock_server_t;

typedef struct sock_client_s {
	char *server_name;
	struct hostent *server_host;
	comm_channel_t *cc_master;
	comm_channel_t *cc_worker;
	size_t ntrans;
} sock_client_t;

extern int sock_errno;

//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
/// sock_server_t
//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_server_ctor( sock_server_t *this_, unsigned short port_, sock_server_t *worker_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_server_dtor( sock_server_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_server_bind( const sock_server_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_server_listen( const sock_server_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_server_accept( sock_server_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_server_fork( sock_server_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ssize_t sock_server_send( sock_server_t *this_, const void *msg_, size_t len_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ssize_t sock_server_recv( sock_server_t *this_, void **msg_, size_t *len_ );

/* //------------------------------------------------------------------------------ */
/* // */
/* //------------------------------------------------------------------------------ */
/* int sock_server_close( sock_server_t *this_ ); */

////////////////////////////////////////////////////////////////////////////////
/// sock_client_t
////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_client_ctor( sock_client_t *this_, const char *server_host_, unsigned short server_port_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_client_dtor( sock_client_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_client_connect( const sock_client_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_client_reconnect( sock_client_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_client_worker_addr( sock_client_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ssize_t sock_client_send( sock_client_t *this_, const void *msg_, size_t len_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ssize_t sock_client_recv( sock_client_t *this_, void **msg_, size_t *len_ );

#endif // __SOCK_H__
