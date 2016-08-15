#include <assert.h>
#include <math.h>
#include <netdb.h>

#include "global.h"
#include "sock.h"

#define log2( a ) ( log((double)(a)) / log(2.0) )

typedef struct buffer_s {
	size_t size;
	size_t n;
	void *data;
} buffer_t;

typedef struct server_client_s {
	int fd;
	socklen_t len;
	struct sockaddr_in addr;
	buffer_t buffer;
} server_client_t;

//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
// LOCAL PROTOTYPES
//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

static void sys_error(const char *msg);
static void error(const char *msg);

static size_t recv_stream_block( int fd_, void *data_, size_t n_, size_t *ntrans_ );
static size_t send_stream_block( int fd_, const void *data_, size_t n_, size_t *ntrans_ );

static size_t recv_socket( int fd_, void *data_, size_t n_, size_t *ntrans_ );
static size_t send_socket( int fd_, const void *data_, size_t n_, size_t *ntrans_ );

static void buffer_ctor( buffer_t *this_, size_t *size_ );
static void buffer_dtor( buffer_t *this_ );
static void buffer_resize( buffer_t *this_, size_t size_ );
static void buffer_clear( buffer_t *this_ );
static void buffer_recv( buffer_t *this_, int fd_, size_t *ntrans_ );
static void buffer_send( buffer_t *this_, int fd_, size_t *ntrans_ );
	
static server_client_t *server_client_alloc( size_t *buffer_size_ );
static void server_client_free( server_client_t *this_ );

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void sys_error(const char *msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void error(const char *msg)
{
	fprintf(stderr,msg);
	exit(EXIT_FAILURE);
}

//------------------------------------------------------------------------------
// Performs consecutive recvs to recv the entire stream block into the buffer.
// Ensures that the entire stream block is recv.
//------------------------------------------------------------------------------
static size_t recv_stream_block( int fd_, void *data_, size_t n_, size_t *ntrans_ )
{
	ssize_t n;
	size_t nrecv;
	size_t r;

	r=0;
	nrecv = 0;
	while(1) {
		r++;
		// n = recv(fd_, (char *)data_ + nrecv, n_ - nrecv );
		n = recv(fd_, (char *)data_ + nrecv, n_ - nrecv, 0 );		
		if (n < 0) sys_error("ERROR recving from socket");
		nrecv += n;
		//printf("sock::recv_stream_block: recv %zd bytes\n", nrecv);
		if( nrecv == n_ ) break;
	}
	if( ntrans_ ) *ntrans_ = r;
	return nrecv;
}

//------------------------------------------------------------------------------
// Performs consecutive sends to send the entire stream block into the buffer.
// Ensures that the entire stream block is written.
//------------------------------------------------------------------------------
static size_t send_stream_block( int fd_, const void *data_, size_t n_, size_t *ntrans_ )
{
	ssize_t n;
	size_t nsend;
	size_t w;

	w=0;
	nsend = 0;
	while(1) {
		w++;
		// n = send(fd_, (char *)data_ + nsend, n_ - nsend );
		n = send(fd_, (char *)data_ + nsend, n_ - nsend, 0 );
		if (n < 0) sys_error("ERROR writing to socket");
		nsend += n;
		//printf("sock::send_stream_block: wrote %zd bytes\n", nsend);
		if( nsend == n_ ) break;
	}
	if( ntrans_ ) *ntrans_ = w;
	
	return nsend;
}

//------------------------------------------------------------------------------
// Recvs a socket message where a message is composed of
//
//   [ size of payload : payload ]
//
// The recv is performed up to the buffer size n_
//------------------------------------------------------------------------------
static size_t recv_socket( int fd_, void *data_, size_t n_, size_t *ntrans_ )
{
	size_t len;
	size_t r1, r2;
	size_t n;

	n = recv_stream_block( fd_, &len, sizeof(len), &r1 );

	// Recv the maximum allowable into the data buffer
	len = ( n_ < len ? n_ : len );

	n += recv_stream_block( fd_, data_, len, &r2 );
	
	if( ntrans_ ) *ntrans_ = r1 + r2;
	
	return n;
}
	
//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static size_t send_socket( int fd_, const void *data_, size_t n_, size_t *ntrans_ )
{
	size_t n;
	size_t w1,w2;

	n = send_stream_block( fd_, &n_, sizeof(n_), &w1 );
	n += send_stream_block( fd_, data_, n_, &w2 );

	if( ntrans_ ) *ntrans_ = w1 + w2;
	
	return n;
}


//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
/// buffer_t
//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void buffer_ctor( buffer_t *this_, size_t *size_ )
{
	size_t size;
	if( size_ ) {
		size = *size_;
	} else {
		size = 32; // default to 32 bytes
	}
	this_->size = size;
	this_->n    = 0;
	this_->data = calloc(size,1);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void buffer_dtor( buffer_t *this_ )
{
	this_->size = 0;
	this_->n    = 0;
	if( this_->data ) free( this_->data );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void buffer_resize( buffer_t *this_, size_t min_size_ )
{
	// Compute the new size as size = 2^n*this_->size >= min_size_
	int n = (int)ceil(log2( ((double)min_size_) / this_->size ));
	size_t size = this_->size << n;
	
	this_->data = realloc( this_->data, size );
	assert( this_->data );
	
	// Zero the new block
	if( size > this_->size ) 
		memset( this_->data + this_->size, 0, size - this_->size );

	this_->size = size;	
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void buffer_clear( buffer_t *this_ )
{
	this_->n = 0;
	memset( this_->data, 0, this_->size );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void buffer_recv( buffer_t *this_, int fd_, size_t *ntrans_ )
{
	size_t len;
	size_t r1,r2;

	buffer_clear( this_ );

	recv_stream_block( fd_, &len, sizeof(len), &r1 );
	
	buffer_resize( this_, len );
	this_->n = len;

	recv_stream_block( fd_, this_->data, this_->n, &r2 );

	if( ntrans_ ) *ntrans_ = r1 + r2;
}


//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void buffer_send( buffer_t *this_, int fd_, size_t *ntrans_ )
{
	send_socket( fd_, this_->data, this_->n, ntrans_ );
}
	

////////////////////////////////////////////////////////////////////////////////
/// server_client_t
////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static server_client_t *server_client_alloc( size_t *buffer_size_ )
{
	server_client_t *this_ = calloc( 1, sizeof(*this_));
	buffer_ctor( &this_->buffer, buffer_size_ );

	return this_;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void server_client_free( server_client_t *this_ )
{
	if( this_ ) {
		buffer_dtor( &this_->buffer );
		free(this_);
	}
}


//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
/// sock_server_t
//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_ctor( sock_server_t *this_ )
{
	memset(this_, 0, sizeof(*this_));

	this_->parent = true;
	
	this_->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (this_->fd < 0) 
		sys_error("ERROR opening socket");

	this_->addr.sin_family      = AF_INET;
	this_->addr.sin_addr.s_addr = htonl(INADDR_ANY);
	this_->addr.sin_port        = htons(PORTNO);

	this_->client = server_client_alloc( NULL );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_dtor( sock_server_t *this_ )
{
	server_client_free( this_->client );
	this_->client = NULL;

	// Only the parent can close the socket file descriptor
	if( this_->parent ) 
		close(this_->fd);
	
	memset( this_, 0, sizeof(*this_) );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_bind( const sock_server_t *this_ )
{
	if (bind(this_->fd, (struct sockaddr *) &this_->addr,
		 sizeof(this_->addr)) < 0) 
		sys_error("ERROR on binding");
	
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_listen( const sock_server_t *this_ )
{
	listen(this_->fd, 5);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_accept( sock_server_t *this_ )
{
	server_client_t *c = this_->client;
	
	c->len = sizeof(c->addr);
	c->fd = accept(this_->fd, 
		       (struct sockaddr *) &c->addr, 
		       &c->len);
	if (c->fd < 0) 
		sys_error("ERROR on accept");

}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_fork( sock_server_t *this_ )
{
	pid_t fpid;

	fpid = fork();
	if( fpid < 0 ) sys_error("ERROR on fork");

	if( fpid == 0 ) this_->parent = false;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_recv( sock_server_t *this_, void **data_, size_t *n_  )
{
	server_client_t *c = this_->client;
	buffer_t *b = &c->buffer;

	buffer_recv( b, c->fd, &this_->ntrans );

	*n_ = b->n;
	*data_ = b->data;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_send( sock_server_t *this_, const void *data_, size_t n_ )
{	
	server_client_t *c = this_->client;
	buffer_t *b = &c->buffer;

	if( data_ == NULL ) {
		buffer_send( b, c->fd, &this_->ntrans );
	} else {
		send_socket( c->fd, data_, n_, &this_->ntrans );
	}
}

//------------------------------------------------------------------------------
// 
//------------------------------------------------------------------------------
void sock_server_close( sock_server_t *this_ )
{
	server_client_t *c = this_->client;
	
	close(c->fd);
	c->fd = 0;
}

////////////////////////////////////////////////////////////////////////////////
/// sock_client_t
////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_client_ctor( sock_client_t *this_, const char *server_host_ )
{
	this_->server_host = strdup( server_host_ );

	this_->server = gethostbyname(server_host_);
	if (this_->server == NULL) {
		fprintf(stderr,"ERROR, no such host\n");
		exit(0);
	}
	
	memset(&this_->server_addr, 0, sizeof(this_->server_addr));
	
	this_->server_addr.sin_family = AF_INET;
	memcpy((char *)&this_->server_addr.sin_addr.s_addr,
	       (char *)this_->server->h_addr, 
	       this_->server->h_length);
	this_->server_addr.sin_port = htons(PORTNO);

	this_->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (this_->fd < 0) 
		sys_error("ERROR opening socket");

}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_client_dtor( sock_client_t *this_ )
{
	free(this_->server_host);
	close(this_->fd);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_client_connect( const sock_client_t *this_ )
{
	if (connect(this_->fd,(struct sockaddr *) &this_->server_addr, sizeof(this_->server_addr)) < 0) 
		sys_error("ERROR connecting");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
size_t sock_client_send( sock_client_t *this_, const void *data_, size_t size_ )
{
	return send_socket( this_->fd, data_, size_, &this_->ntrans );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
size_t sock_client_recv( sock_client_t *this_, void *data_, size_t size_ )
{
	return recv_socket( this_->fd, data_, size_, &this_->ntrans );

}
