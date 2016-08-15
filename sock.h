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

typedef struct server_client_s server_client_t;

typedef struct sock_server_s {
	bool parent;
	int fd;
	struct sockaddr_in addr;
	server_client_t *client;
	size_t ntrans;
} sock_server_t;

typedef struct sock_client_s {
	int fd;
	char *server_host;
	struct sockaddr_in server_addr;
	struct hostent *server;
	size_t ntrans;
} sock_client_t;


//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
/// sock_server_t
//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_ctor( sock_server_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_dtor( sock_server_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_bind( const sock_server_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_listen( const sock_server_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_accept( sock_server_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_fork( sock_server_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_recv( sock_server_t *this_, void **data_, size_t *n_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_send( sock_server_t *this_, const void *data_, size_t n_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_close( sock_server_t *this_ );

////////////////////////////////////////////////////////////////////////////////
/// sock_client_t
////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_client_ctor( sock_client_t *this_, const char *server_host_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_client_dtor( sock_client_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_client_connect( const sock_client_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
size_t sock_client_send( sock_client_t *this_, const void *data_, size_t size_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
size_t sock_client_recv( sock_client_t *this_, void *data_, size_t size_ );

#endif // __SOCK_H__
