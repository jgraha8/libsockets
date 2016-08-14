#include <assert.h>
#include <netdb.h>

#include "global.h"
#include "sock.h"

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
static void write_socket( int fd_, void *data_, size_t n_ );

static void buffer_ctor( buffer_t *this_, size_t *size_ );
static void buffer_dtor( buffer_t *this_ );
static void buffer_resize( buffer_t *this_, size_t size_ );
static void buffer_clear( buffer_t *this_ );
static void buffer_read( buffer_t *this_, int fd_ );
static void buffer_write( const buffer_t *this_, int fd_ );
	
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
//
//------------------------------------------------------------------------------
static void write_socket( int fd_, void *data_, size_t n_ )
{
	size_t n;

	n = write( fd_, data_, n_ );
	if (n < 0) sys_error("ERROR writing to socket");
	if( n != n_ ) error("ERROR writing to socket");
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
static void buffer_resize( buffer_t *this_, size_t size_ )
{
	this_->data = realloc( this_->data, size_ );
	assert( this_->data );
	
	// Zero the new block
	if( size_ > this_->size ) 
		memset( this_->data + this_->size, 0, size_ - this_->size );
	
	this_->size = size_;	
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
static void buffer_read( buffer_t *this_, int fd_ )
{
	size_t n;
	size_t read_size;

	buffer_clear( this_ );
	
	while(1) {

		while( this_->size <= this_->n ) {
			buffer_resize( this_, 2*this_->size );
		}

		read_size = this_->size - this_->n;	
		n = read(fd_, this_->data + this_->n, read_size );

		if (n < 0) sys_error("ERROR reading from socket");

		this_->n += n;
		if( n < read_size ) break;
	}
}


//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void buffer_write( const buffer_t *this_, int fd_ )
{
	write_socket( fd_, this_->data, this_->n );
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
	this_->addr.sin_addr.s_addr = INADDR_ANY;
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
void sock_server_read( sock_server_t *this_, size_t *n_, void **data_ )
{
	server_client_t *c = this_->client;
	buffer_t *b = &c->buffer;

	buffer_read( b, c->fd );

	*n_ = b->n;
	*data_ = b->data;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_server_write( const sock_server_t *this_, size_t n_, void *data_ )
{	
	server_client_t *c = this_->client;
	buffer_t *b = &c->buffer;

	if( data_ == NULL ) {
		buffer_write( b, c->fd );
	} else {
		write_socket( c->fd, data_, n_ );
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
void sock_client_write( const sock_client_t *this_, size_t size_, const void *data_ )
{
	if( write(this_->fd, data_, size_) < 0 )
		sys_error("ERROR writing to socket");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sock_client_read( const sock_client_t *this_, size_t size_, void *data_ )
{
	if( read(this_->fd, data_, size_ ) < 0 )
		sys_error("ERROR reading from socket");
}
