#include <assert.h>
#include <math.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include "global.h"
#include "sock.h"

#define log2( a ) ( log((double)(a)) / log(2.0) )

// Syscall macros
#define s_socket(...) socket( __VA_ARGS__ ); sock_errno = errno;
#define s_connect(...) connect( __VA_ARGS__ ); sock_errno = errno;
#define s_bind(...) bind( __VA_ARGS__ ); sock_errno = errno;
#define s_listen(...) listen( __VA_ARGS__ ); sock_errno = errno;
#define s_accept(...) accept( __VA_ARGS__ ); sock_errno = errno;
#define s_fork(...) fork( __VA_ARGS__ ); sock_errno = errno;
#define s_close(...) close( __VA_ARGS__ ); sock_errno = errno;
#define s_send(...) send( __VA_ARGS__ ); sock_errno = errno;
#define s_recv(...) recv( __VA_ARGS__ ); sock_errno = errno;

typedef struct buffer_s {
	size_t len; // Length (in bytes) of data
	size_t n;   // Number of bytes used in data
	void *data; // Data
} buffer_t;

typedef struct comm_channel_s {
	int fd;                   // Socket file descriptor 
	socklen_t addr_len;       // Length of address
	struct sockaddr_in addr;  // Remote address
	buffer_t buf;             // Internal buffer
} comm_channel_t;

int sock_errno = 0;

//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
// LOCAL PROTOTYPES
//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

// static void sys_error(const char *msg);
// static void error(const char *msg);

static int __sock_server_bind( const sock_server_t *this_ );
static int __sock_server_listen( const sock_server_t *this_ );
static ssize_t __sock_server_send_wport( sock_server_t *this_ );

static int __sock_client_req_wport( sock_client_t *this_, uint16_t *wport_ );
static int __sock_client_connect_worker( sock_client_t *this_ );

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
	
static comm_channel_t *comm_channel_alloc( size_t buf_len_ );
static void comm_channel_free( comm_channel_t *this_ );
static int comm_channel_open( comm_channel_t *this_, const struct hostent *host_, uint16_t port_ );
static int comm_channel_close( comm_channel_t *this_ );

//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
/// sock_server_t
//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_server_ctor( sock_server_t *this_, uint16_t port_, sock_server_t *worker_ )
{
	memset(this_, 0, sizeof(*this_));

	this_->parent = true;
	
	this_->fd = s_socket(AF_INET, SOCK_STREAM, 0);
	if (this_->fd < 0) return -1;

	this_->addr.sin_family      = AF_INET;
	this_->addr.sin_addr.s_addr = htonl(INADDR_ANY);
	this_->addr.sin_port        = htons(port_);

	this_->cc_client = comm_channel_alloc( NULL ); // Use default buffer size

	n = __sock_server_bind( this_ );   if( n < 0 ) return n;
	n = __sock_server_listen( this_ ); if( n < 0 ) return n;

	// External reference to the worker server
	if( worker_ ) {
		this_->worker = worker_;
	} else {
		this_->worker = this_;
	}
	this_->is_worker = true;

	return 0;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_server_dtor( sock_server_t *this_ )
{
	int n;

	comm_channel_close( this_->cc_client );
	comm_channel_free( this_->cc_client );

	// Only the parent can close the socket file descriptor
	if( this_->parent ) {
		n = s_close(this_->fd);
		if( n < 0 ) return -1;
	}

	if( this_->worker != this_ ) {
		sock_server_dtor( this_->worker );
	}
	
	memset( this_, 0, sizeof(*this_) );

	return 0;
}



//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_server_accept( sock_server_t *this_ )
{
	int n;
	comm_channel_t *c = this_->cc_client;
	size_t hdr_len;
	sock_tcp_header_t *hdr;
	
	c->addr_len = sizeof(c->addr);
	c->fd = s_accept(this_->fd, 
			 (struct sockaddr *) &c->addr, 
			 &c->addr_len);
	if (c->fd < 0) return -1;

	if( this_->worker != this_ ) {
		n = sock_server_ctor( this_->worker, 0, NULL ); if( n < 0 ) return n;	// Set the worker port
	}
	this_->wport = ntohs( this_->worker->addr.sin_port);

	if( !this_->is_worker ) {
		// Accept the incomming wport request
		sock_server_recv( this_, &hdr, &hdr_len );
		assert( hdr->opts & SOCK_OPTS_REQ_WPORT );
		__sock_server_send_wport( this_ );
	}
	
	return 0;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
pid_t sock_server_fork( sock_server_t *this_ )
{
	pid_t fpid;

	fpid = s_fork();
	if( fpid < 0 ) return -1;

	if( fpid == 0 ) { // Child process
		this_->parent = false; // Child is not parent process of child
	} else {
		// Transfer owner ship to the worker process
		if( this_->worker != this_ ) {
			this_->worker->parent = false;
			sock_server_dtor( this_->worker );
		}
	}

	return fpid;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ssize_t sock_server_recv( sock_server_t *this_, void **data_, size_t *len_  )
{
	ssize_t n, _n;
	size_t ntrans;
	comm_channel_t *c = this_->cc_client;
	buffer_t *buf = &c->buf;
	tcp_header_t *hdr;
	void *msg;
	
	*data_ = NULL;
	*len_  = 0;

	// Read the header
	hdr = (tcp_header_t *)buf->data;
	_n = trans_socket( __recv, hdr, sizeof(sock_tcp_header_t), &ntrans );
	if( _n < 0 ) return _n;

	n = _n;
	this_->ntrans = ntrans;

	// Make sure that the buffer is large enough
	buffer_resize( buf, hdr->msg_len + sizeof(sock_tcp_header_t) );

	// Read the message
	msg = (void *)(hdr+1);
	_n = trans_socket( __recv, msg, hdr->msg_len, &ntrans );
	if( _n < 0 ) return _n;

	n += _n;
	this_->ntrans += ntrans;

	// Set the used buffer length
	assert( n == (hdr->msg_len + sizeof(tcp_header_t)) );
	buf->n = n;

	// Provide reference to internal data
	*data_ = buf->data;
	*len_  = buf->n;
	
	return n;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ssize_t sock_server_send( sock_server_t *this_, const void *data_, size_t len_ )
{
	ssize_t n;
	comm_channel_t *c = this_->cc_client;
	buffer_t *buf = &c->buf;

	void *data;
	size_t len;

	if( data_ == NULL ) { // Sending internal buffer
		data = buf->data;
		len  = buf->n;
	} else {
		data = (void *)data_;
		len  = len_;
	}
	n = trans_socket( __send, c->fd, data, len, &this_->ntrans );
	
	return n;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static int __sock_server_bind( const sock_server_t *this_ )
{
	int n = s_bind(this_->fd, (struct sockaddr *) &this_->addr,
		       sizeof(this_->addr));
	return n;
	
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static int __sock_server_listen( const sock_server_t *this_ )
{
	int n = s_listen(this_->fd, 5);
	return n;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static ssize_t __sock_server_send_wport( sock_server_t *this_ )
{
	ssize_t n;
	comm_channel_t *c = this_->cc_client;
	buffer_t *buf = &c->buf;
	sock_tcp_header_t *hdr = (sock_tcp_header_t *)buf->data;
	void *msg = (void *)(hdr+1);

	// Only the master server should have a non-zero wport value
	if( this_->wport = 0 ) return -1;
	
	buffer_clear( buf );

	hdr.msg_len = sizeof(this_->wport);
	
	buf->n = sizeof(*hdr) + hdr.msg_len;
	buffer_resize( buf, buf->n );

	memcpy(msg, &this_->wport, sizeof(this_->wport));

	n = sock_server_send( this_, NULL, 0 );

	return n;
}

////////////////////////////////////////////////////////////////////////////////
/// sock_client_t
////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_client_ctor( sock_client_t *this_, const char *server_name_, uint16_t server_port_ )
{
	int n;
	
	memset(this_,0,sizeof(*this_));
	
	this_->server_name = strdup( server_name_ );

	this_->server_host = gethostbyname(server_name_);
	if (this_->server_host == NULL) {
		fprintf(stderr,"ERROR, no such host\n");
		exit(0);
	}

	this_->cc_master = comm_channel_alloc(NULL);

	n = comm_channel_open( this_->cc_master, this_->server_host, server_port_ );
	
	return n;

}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_client_dtor( sock_client_t *this_ )
{
	int n;
	
	if( this_->cc_worker != this_->cc_master ) {
		n = s_close( this_->cc_worker->fd );
		if( n < 0 ) return -1;
		
		comm_channel_close( this_->cc_worker );
		comm_channel_free( this_->cc_worker );
	}
	
	n = s_close(this_->cc_master->fd);
	if( n < 0 ) return -1;

	comm_channel_close( this_->cc_master );
	comm_channel_free( this_->cc_master );

	free(this_->server_name);
	
	memset(this_,0,sizeof(*this_));
	
	return 0;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_client_connect( const sock_client_t *this_ )
{
	int n=0;
	n = s_connect(cc->fd,
		      (struct sockaddr *) &cc->addr,
		      sizeof(cc->addr));
	if( n < 0 ) return n;

	n = __sock_client_connect_worker( (sock_client_t *)this_ );

	return n;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_client_reconnect( sock_client_t *this_ )
{
	int n=0;

	n = comm_channel_reopen( this_->cc_worker );
	if( n < 0 ) return n;

	if( this_->cc_worker != this_->cc_master ) {
		n = comm_channel_reopen( this_->cc_master );
		if( n < 0 ) return n;		
	}

	return sock_client_connect( this_ );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ssize_t sock_client_send( sock_client_t *this_, const void *data_, size_t size_ )
{
	return trans_socket( __send, this_->cc_worker->fd, (void *)data_, size_, &this_->ntrans );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ssize_t sock_client_recv( sock_client_t *this_, enum sock_comm_channel cc_,, void *data_, size_t size_ )
{
	return trans_socket( __recv, this_->cc_worker->fd, data_, size_, &this_->ntrans );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static int __sock_client_req_wport( sock_client_t *this_, uint16_t *wport_ )
{

	ssize_t n;
	size_t ntrans;
	tcp_header_t *hdr;
	void *msg;
	
	// Set references for internal buffer
	hdr = (tcp_header_t *)&this_->cc_master->buf.data; 
	msg = (void *)(hdr+1);
	
	// Send a request to the master for a worker
	hdr->msg_len = 0;
	hdr->opts    = SOCK_OPTS_REQ_WPORT;

	n = trans_socket( __send, this_->cc_master->fd, hdr, sizeof(*hdr), &ntrans );
	if( n < 0 ) return (int)n;

	// Get reply for the port
	// Read header
	n = trans_socket( __recv, this_->cc_master->fd, hdr, sizeof(*hdr), &ntrans );
	if( n < 0 ) return (int)n;
	n = trans_socket( __recv, this_->cc_master->fd, msg, hdr->msg_len, &ntrans );
	if( n < 0 ) return (int)n;

	// Check expected message size
	assert( hdr->msg_len == sizeof(uint16_t) );
	*wport_ = *(uint16_t *)msg;

	return 0;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static int __sock_client_connect_worker( sock_client_t *this_ )
{
	int n;
	uint16_t wport;
	
	n = __sock_client_req_wport( this_, &wport );

	// Check if the master port was returned
	if( &wport == ntohs(this_->cc_master->addr.sin_port) ) {
		this_->cc_worker = this_->cc_master;
	} else {
		if( !this_->cc_worker )
			this_->cc_worker = comm_channel_alloc(NULL);

		n = comm_channel_open( this_->cc_worker, this_->server_host, wport )
		n = s_connect(this_->cc_worker->fd,
			      (struct sockaddr *) &this_->cc_worker->addr,
			      sizeof(this_->cc_worker->addr));
	}
	return n;
}

////////////////////////////////////////////////////////////////////////////////
/// comm_channel_t
////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static comm_channel_t *comm_channel_alloc( size_t buf_len_ )
{
	comm_channel_t *this_ = calloc( 1, sizeof(*this_));
	buffer_ctor( &this_->buf, buf_len_ );

	return this_;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void comm_channel_free( comm_channel_t *this_ )
{
	if( this_ ) {
		buffer_dtor( &this_->buffer );
		free(this_);
	}
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static int comm_channel_open( comm_channel_t *this_, const struct hostent *host_,
			      uint16_t port_ )

{
	this_->addr.sin_family = AF_INET;
	
	memcpy((char *)&this_->addr.sin_addr.s_addr,
	       (char *)host_->h_addr, 
	       host_->h_length);
	
	this_->addr.sin_port = htons(port_);
	
	this_->fd = s_socket(AF_INET, SOCK_STREAM, 0);
	if (this_->fd < 0) return -1;

	return 0;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static int comm_channel_close( comm_channel_t *this_ )
{
	int n;
	n = s_close(this_->fd);
	return n;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static int comm_channel_reopen( sock_client_t *this_ )
{
	int n=0;

	n = s_close(this_->fd);
	if( n < 0 ) return n;
	
	this_->fd = s_socket(AF_INET, SOCK_STREAM, 0);
	if( cc->fd < 0 ) return -1;

	return 0;
}


////////////////////////////////////////////////////////////////////////////////
/// buffer_t
////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void buffer_ctor( buffer_t *this_, size_t len_ )
{
	size_t len;
	if( len_ ) {
		len = len_;
	} else {
		len = sizeof(sock_tcp_header_t) + 32; // default to tcp header size + 32 byte msg
	}
	this_->len = len;
	this_->n    = 0;
	this_->data = calloc(len,1);
	assert( this_->data );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void buffer_dtor( buffer_t *this_ )
{
	printf("sock::buffer_dtor called\n");	
	this_->len = 0;
	this_->n    = 0;
	if( this_->data ) {
		printf("sock::buffer_dtor: free(data) called\n");
		free( this_->data );
	}
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void buffer_resize( buffer_t *this_, size_t min_len_ )
{
	if( this_->len >= min_len_ ) return;
	
	// Compute the new len as len = 2^n*this_->len >= min_len_
	int n = (int)ceil(log2( ((double)min_len_) / this_->len ));
	size_t len = this_->len << n;

	assert( len > this_->len );

	this_->data = realloc( this_->data, len );
	assert( this_->data );
	
	// Zero the new bytes
	memset( this_->data + this_->len, 0, len - this_->len );

	this_->len = len;	
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void buffer_clear( buffer_t *this_ )
{
	this_->n = 0;
	memset( this_->data, 0, this_->len );
}

////////////////////////////////////////////////////////////////////////////////
// Helper Procedures
////////////////////////////////////////////////////////////////////////////////

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
	if( len != n_ ) {
		rc = -1;
		sock_errno = -1;
	}
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
	ssize_t n = s_send(fd_, data_, n_, flags_ );
	return n;
}

static inline ssize_t __recv( int fd_, void *data_, size_t n_, int flags_ )
{
	ssize_t n = s_recv(fd_, data_, n_, flags_ );
	return n;
}

