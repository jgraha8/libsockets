#include <assert.h>
#include <math.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

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

int sock_errno = 0;

//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
// LOCAL PROTOTYPES
//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

static void sys_error(const char *msg);
static void error(const char *msg);

static ssize_t trans_stream_block( ssize_t (*method_)( int fd_, void *data_, size_t n_, int flags_ ),
				  int fd_, void *data_, size_t n_, size_t *ntrans_ );
static ssize_t trans_socket( ssize_t (*method_)( int fd_, void *data_, size_t n_, int flags_ ),
			     int fd_, void *data_, size_t n_, size_t *ntrans_ );
static inline ssize_t __send( int fd_, void *data_, size_t n_, int flags_ );
static inline ssize_t __recv( int fd_, void *data_, size_t n_, int flags_ );

static void buffer_ctor( buffer_t *this_, size_t *size_ );
static void buffer_dtor( buffer_t *this_ );
static void buffer_resize( buffer_t *this_, size_t size_ );
static void buffer_clear( buffer_t *this_ );
static ssize_t buffer_recv( buffer_t *this_, int fd_, size_t *ntrans_ );
static ssize_t buffer_send( buffer_t *this_, int fd_, size_t *ntrans_ );
	
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
static ssize_t trans_stream_block( ssize_t (*method_)(int fd_, void *data_, size_t n_, int flags_ ),
				   int fd_, void *data_, size_t n_, size_t *ntrans_ )
{
	ssize_t n;
	ssize_t len;
	size_t nt;

	ssize_t rc = 0;

	nt=0;
	len = 0;
	while(1) {
		nt++;
		n = method_(fd_, (char *)data_ + len, n_ - len, 0 );

		if( n < 0 ) { // Error occurred
			rc = n;
			sock_errno = errno;
			goto fini;
		} else if( n == 0 ) { // Peer disconnect (set as error)
			rc = -1;
			sock_errno = -1;
			goto fini;
		}
		assert( n > 0 );
		len += n;
		if( len == n_ ) {
			rc = len;
			goto fini;
		}
	}

 fini:
	if( len != n_ ) rc = -1;
	if( ntrans_ ) *ntrans_ = nt;
	return rc;
}

//------------------------------------------------------------------------------
// 
//------------------------------------------------------------------------------
static ssize_t trans_socket( ssize_t (*method_)(int fd_, void *data_, size_t n_, int flags_ ),
			     int fd_, void *data_, size_t n_, size_t *ntrans_ )
{
	ssize_t n=0;
	size_t nt=0;
	ssize_t len=0;

	n = trans_stream_block( method_, fd_, &n_, sizeof(n_), &nt );
	if( ntrans_ ) *ntrans_ = nt;	
	if( n < 0 ) return n;
	len = n;
	
	n = trans_stream_block( method_, fd_, data_, n_, &nt );
	if( ntrans_ ) *ntrans_ += nt;	
	if( n < 0 ) return n;
	len += n;

	return len;	
}

//------------------------------------------------------------------------------
// Local send wrapper procedure to have common send/recv prototypes
//------------------------------------------------------------------------------
static inline ssize_t __send( int fd_, void *data_, size_t n_, int flags_ )
{
	return write(fd_, data_, n_ ); //send(fd_, data_, n_, flags_ );
}

static inline ssize_t __recv( int fd_, void *data_, size_t n_, int flags_ )
{
	ssize_t n;
	n = recv(fd_, data_, n_, flags_ );
	// n = read( fd_, data_, n_ );

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
static ssize_t buffer_recv( buffer_t *this_, int fd_, size_t *ntrans_ )
{
	ssize_t n;
	size_t len;
	size_t r1,r2;
	ssize_t nrecv;

	buffer_clear( this_ );

	n = trans_stream_block( __recv, fd_, &len, sizeof(len), &r1 );
	if( ntrans_ ) *ntrans_ = r1;
	if( n < 0 ) return n;
	nrecv = n;
	
	buffer_resize( this_, len );
	this_->n = len;

	n = trans_stream_block( __recv, fd_, this_->data, this_->n, &r2 );
	if( ntrans_ ) *ntrans_ += r2;	
	if( n < 0 ) return n;
	nrecv += n;

	return nrecv;
}


//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static ssize_t buffer_send( buffer_t *this_, int fd_, size_t *ntrans_ )
{
	return trans_socket( __send, fd_, this_->data, this_->n, ntrans_ );
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

	this_->client = server_client_alloc( NULL ); // Use default buffer size
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
ssize_t sock_server_recv( sock_server_t *this_, void **data_, size_t *n_  )
{
	ssize_t n;
	server_client_t *c = this_->client;
	buffer_t *b = &c->buffer;

	*data_ = NULL;
	*n_    = 0;

	n = buffer_recv( b, c->fd, &this_->ntrans );

	if( n < 0 ) return n; // Error occured

	*n_ = b->n;
	*data_ = b->data;
	
	return n;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ssize_t sock_server_send( sock_server_t *this_, const void *data_, size_t n_ )
{
	ssize_t n;
	server_client_t *c = this_->client;
	buffer_t *b = &c->buffer;

	if( data_ == NULL ) {
		n = buffer_send( b, c->fd, &this_->ntrans );
	} else {
		n = trans_socket( __send, c->fd, (void *)data_, n_, &this_->ntrans );
	}
	return n;
}

//------------------------------------------------------------------------------
// 
//------------------------------------------------------------------------------
void sock_server_close( sock_server_t *this_ )
{
	server_client_t *c = this_->client;
	
	close(c->fd);
	memset(c, 0, sizeof(*c));

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
	memset(this_,0,sizeof(*this_));
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
void sock_client_reconnect( sock_client_t *this_ )
{
	close(this_->fd);

	this_->fd = socket(AF_INET, SOCK_STREAM, 0);
	if( this_->fd < 0 )
		sys_error("ERROR opening socket");
	sock_client_connect(this_);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ssize_t sock_client_send( sock_client_t *this_, const void *data_, size_t size_ )
{
	return trans_socket( __send, this_->fd, (void *)data_, size_, &this_->ntrans );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ssize_t sock_client_recv( sock_client_t *this_, void *data_, size_t size_ )
{
	return trans_socket( __recv, this_->fd, data_, size_, &this_->ntrans );
}
