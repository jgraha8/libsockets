#include <assert.h>
#include <math.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include "global.h"
#include "sock.h"

#define set_bit(a,mask) ( (a) |= (mask) )
#define unset_bit(a,mask) ( (a) &= ( (a) ^ (mask) ) )

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
	size_t alloc_len;
} buffer_t;

typedef struct comm_channel_s {
	int fd;                   // Socket file descriptor 
	socklen_t addr_len;       // Length of address
	struct sockaddr_in addr;  // Remote address
	buffer_t buf;             // Internal buffer	
} comm_channel_t;

int sock_errno = 0;
static char comm_channel_msg[64];

//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
// LOCAL PROTOTYPES
//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

// static void sys_error(const char *msg);
// static void error(const char *msg);

static int __sock_server_open( sock_server_t *this_, uint16_t port_ );
static int __sock_server_close( sock_server_t *this_ );
static int __sock_server_accept( sock_server_t *this_ );
static ssize_t __sock_server_recv( sock_server_t *this_, sock_tcp_header_t *hdr_,
				   void **msg_, size_t *len_  );
static ssize_t __sock_server_send( sock_server_t *this_, const sock_tcp_header_t *hdr_,
				   const void *data_, size_t len_ );
static int __sock_server_setchild( sock_server_t *this_ );

static ssize_t __sock_client_req_wport( sock_client_t *this_, uint16_t *wport_ );
static ssize_t __sock_client_send_sigterm( sock_client_t *this_ );
static int __sock_client_connect_worker( sock_client_t *this_ );

static uint16_t get_sock_port( sock_server_t *this_ );

static ssize_t trans_stream_block( ssize_t (*method_)( int fd_, void *data_, size_t n_, int flags_ ),
				  int fd_, void *data_, size_t n_, size_t *ntrans_ );
static ssize_t trans_socket( ssize_t (*method_)(int fd_, void *data_, size_t n_, int flags_ ),
			     int fd_, sock_tcp_header_t *hdr_, void *data_, size_t len_, size_t *ntrans_ );
static inline ssize_t __send( int fd_, void *data_, size_t n_, int flags_ );
static inline ssize_t __recv( int fd_, void *data_, size_t n_, int flags_ );

static void buffer_ctor( buffer_t *this_, size_t size_ );
static void buffer_dtor( buffer_t *this_ );
static void buffer_resize( buffer_t *this_, size_t size_ );
static void buffer_clear( buffer_t *this_ );
	
static comm_channel_t *comm_channel_alloc( size_t buf_len_ );
static int comm_channel_free( comm_channel_t **this_ );
static int comm_channel_open( comm_channel_t *this_, const struct hostent *host_, uint16_t port_ );
static int comm_channel_close( comm_channel_t *this_ );
static int comm_channel_reopen( comm_channel_t *this_ );
static ssize_t comm_channel_send( comm_channel_t *this_, const sock_tcp_header_t *hdr_,
				  const void *msg_, size_t len_, size_t *ntrans_ );
static ssize_t comm_channel_recv( comm_channel_t *this_, sock_tcp_header_t *hdr_,
				  void **msg_, size_t *len_ , size_t *ntrans_ );

//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
/// sock_server_t
//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_server_ctor( sock_server_t *this_, uint16_t port_, sock_server_t *worker_ )
{
	int n;

	// Initialize
	memset(this_, 0, sizeof(*this_));

	// By default the parent, master, and client parent flags are set
	this_->flags = SOCK_SF_PARENT | SOCK_SF_MASTER;

	// Open listening socket and construct client comm channel
	if( ( n = __sock_server_open( this_, port_ ) ) < 0 ) return n;
	this_->cc_client = comm_channel_alloc(0);

	// External reference to the worker server
	if( worker_ ) {
		this_->worker = worker_;

		if( (n = sock_server_ctor( this_->worker, 0, NULL )) < 0 )
			return n;	
		unset_bit( this_->worker->flags, SOCK_SF_MASTER );
		set_bit ( this_->worker->flags, SOCK_SF_WORKER );
	} else {
		this_->worker = this_;
		// Enable the worker option
		set_bit(this_->flags, SOCK_SF_WORKER);
	}

	return 0;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_server_dtor( sock_server_t *this_ )
{
	int n;
	
	// Checking if a valid address since this procedure is called recursively
	// An alternative is to ensure that worker->worker = worker which protects against
	// recursive dtor calls, but since we nullify all data in ctor and dtor calls,
	// we can simply treat it as a null terminated linked-list.
	if( !this_ ) return 0;

	if( (n = __sock_server_close( this_ )) < 0 ) return n;
	if( (n = comm_channel_free( &this_->cc_client )) < 0 ) return n;
	
	if( this_->worker != this_ )
		if( (n = sock_server_dtor( this_->worker )) < 0) return n;

	memset( this_, 0, sizeof(*this_) );

	return 0;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_server_bind( const sock_server_t *this_ )
{
	int n = s_bind(this_->fd, (struct sockaddr *) &this_->addr,
		       sizeof(this_->addr));
	return n;
	
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_server_listen( const sock_server_t *this_ )
{
	int n = s_listen(this_->fd, 5);
	return n;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_server_accept( sock_server_t *this_ )
{
	ssize_t n;
	sock_tcp_header_t hdr;
	uint16_t wport;

	pid_t pid = getpid();

	assert( this_->flags & SOCK_SF_MASTER );
	
	if( (n = __sock_server_accept( this_ )) < 0 ) return n;

	sprintf(comm_channel_msg, "on sock_server_t (master)");		
	// Recv the incomming wport request
	n = __sock_server_recv( this_, &hdr, NULL, NULL ); if( n < 0 ) return n;

	if( hdr.opts & SOCK_OPTS_REQ_WPORT ) {
		// Open a new socket for the worker	
		if( this_->worker != this_ ) {
			if( this_->worker->fd == 0 ) { // Open worker listen socket if closed
				if( (n = __sock_server_open( this_->worker, 0 )) < 0 )
					return n;
			}
			if( (n = sock_server_bind( this_->worker )) < 0 ) return n;
			if( (n = sock_server_listen( this_->worker )) < 0 ) return n;			
		}

		wport = get_sock_port( this_->worker );
		sprintf(comm_channel_msg, "on sock_server_t.worker");				
		n = __sock_server_send( this_, NULL, &wport, sizeof(wport)); if( n < 0 ) return n;

		// Start accepting on the worker port
		if( this_->worker != this_ ) {
			if( (n = __sock_server_accept( this_->worker )) < 0 ) return n;
			printf("(%d) sock::sock_server_accept: worker accepting on port %d\n", pid, wport);
		}
	} else if( hdr.opts & SOCK_OPTS_SIGTERM ) {
		raise(SIGTERM);
	} else {
		return -1;
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
	if( fpid < 0 ) return fpid;

	if( fpid == 0 ) { // Child
		if( this_->worker != this_ ) { // Close the master
			if( __sock_server_close( this_ ) < 0 ) return -1;
		}
		__sock_server_setchild( this_ );		
	} else { // Parent
		if( this_->worker != this_ ) { // Close the worker
			if( __sock_server_close( this_->worker ) < 0 ) return -1;
		}
	}

	return fpid;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static int __sock_server_open( sock_server_t *this_, uint16_t port_ )
{
	this_->fd = s_socket(AF_INET, SOCK_STREAM, 0);
	if (this_->fd < 0) return -1;

	memset(&this_->addr, 0, sizeof(this_->addr));

	this_->addr.sin_family      = AF_INET;
	this_->addr.sin_addr.s_addr = htonl(INADDR_ANY);
	this_->addr.sin_port        = htons(port_);

	return 0;
}

//------------------------------------------------------------------------------
// Close sockets and frees heap memory; does not change any other state variables
//------------------------------------------------------------------------------
static int __sock_server_close( sock_server_t *this_ )
{
	int n;

	if( this_->fd ) {
		this_->fd = s_close(this_->fd);
		if( this_->fd < 0 ) return this_->fd;
	}

	if( this_->cc_client ) {
		if( (n = comm_channel_close( this_->cc_client )) < 0 ) return n;
	}
	
	return 0;
}
//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ssize_t sock_server_send( sock_server_t *this_, const void *data_, size_t len_ )
{
	sprintf(comm_channel_msg, "on sock_server_t.worker");				
	return __sock_server_send( this_->worker, NULL, data_, len_ );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ssize_t sock_server_recv( sock_server_t *this_, void **data_, size_t *len_  )
{
	sprintf(comm_channel_msg, "on sock_server_t.worker");			
	return __sock_server_recv( this_->worker, NULL, data_, len_ );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static int __sock_server_accept( sock_server_t *this_ )
{
	comm_channel_t *c = this_->cc_client;

	c->fd = s_accept(this_->fd, 
			 (struct sockaddr *) &c->addr, 
			 &c->addr_len);
	if (c->fd < 0) return -1;

	return 0;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static ssize_t __sock_server_recv( sock_server_t *this_, sock_tcp_header_t *hdr_,
				   void **msg_, size_t *len_  )
{
	return comm_channel_recv( this_->cc_client, hdr_, msg_, len_, &this_->ntrans );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static ssize_t __sock_server_send( sock_server_t *this_, const sock_tcp_header_t *hdr_,
				   const void *data_, size_t len_ )
{
	return comm_channel_send( this_->cc_client, hdr_, data_, len_, &this_->ntrans );
}

//------------------------------------------------------------------------------
// 
//------------------------------------------------------------------------------
static int __sock_server_setchild( sock_server_t *this_ )
{
	unset_bit( this_->flags, SOCK_SF_PARENT );
	if( this_->worker != this_ ) // Not necessary but, being explicit here
		unset_bit( this_->worker->flags, SOCK_SF_PARENT );
	return 0;
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

	this_->cc_master = comm_channel_alloc(0);

	n = comm_channel_open( this_->cc_master, this_->server_host, server_port_ );
	
	return n;

}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_client_dtor( sock_client_t *this_ )
{
	int n;

	if( sock_client_close( this_ ) < 0 ) return -1;
	
	if( this_->cc_worker != this_->cc_master ) {
		if( comm_channel_free( &this_->cc_worker ) < 0 ) return -1;
	}
	if( comm_channel_free( &this_->cc_master ) < 0 ) return -1;

	free(this_->server_name);
	memset(this_,0,sizeof(*this_));
	
	return 0;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_client_connect( const sock_client_t *this_, unsigned char opts_ )
{
	int n=0;

	n = s_connect(this_->cc_master->fd,
		      (struct sockaddr *) &this_->cc_master->addr,
		      sizeof(this_->cc_master->addr));
	if( n < 0 ) return n;

	printf("sock::sock_client_connect: opts_ = %d\n", opts_);
	
	if( opts_ & SOCK_OPTS_REQ_WPORT || opts_ == 0 ) {
		n = __sock_client_connect_worker( (sock_client_t *)this_ );
	} else if( opts_ & SOCK_OPTS_SIGTERM ) {
		n = __sock_client_send_sigterm( (sock_client_t *)this_ );
	}

	return n;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_client_open( sock_client_t *this_ )
{
	return comm_channel_open( this_->cc_master, this_->server_host,
				  ntohs(this_->cc_master->addr.sin_port));
}


//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_client_close( sock_client_t *this_ )
{
	if( this_->cc_worker != this_->cc_master ) {
		if( comm_channel_close( this_->cc_worker ) < 0 ) return -1;
	}
	if( comm_channel_close( this_->cc_master ) < 0 ) return -1;

	return 0;
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

	return sock_client_connect( this_, 0 );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ssize_t sock_client_send( sock_client_t *this_, const void *msg_, size_t len_ )
{
	sprintf(comm_channel_msg, "on sock_client_t.cc_worker");	
	return comm_channel_send( this_->cc_worker, NULL, (void *)msg_, len_, &this_->ntrans );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ssize_t sock_client_recv( sock_client_t *this_, void **data_, size_t *len_ )
{
	sprintf(comm_channel_msg, "on sock_client_t.cc_worker");	
	return comm_channel_recv( this_->cc_worker, NULL, data_, len_, &this_->ntrans );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int sock_client_send_sigterm( sock_client_t *this_ )
{
	if( sock_client_close( this_ ) < 0 ) return -1;
	if( sock_client_open( this_ ) < 0 ) return -1;

	return sock_client_connect( this_, SOCK_OPTS_SIGTERM );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static ssize_t __sock_client_req_wport( sock_client_t *this_, uint16_t *wport_ )
{

	ssize_t n=0, _n=0;;
	size_t ntrans;
	sock_tcp_header_t hdr;

	void *msg;
	size_t len;

	sprintf(comm_channel_msg, "on sock_client_t.cc_master");
	
	// Set references for internal buffer
	memset(&hdr,0,sizeof(hdr));
	hdr.opts = SOCK_OPTS_REQ_WPORT;

	// Clear the buffer, so we send no data
	buffer_clear( &this_->cc_master->buf );
	_n = comm_channel_send( this_->cc_master, &hdr, NULL, 0, &ntrans );
	if( _n < 0 ) return _n;
	n = _n;
	this_->ntrans = ntrans;

	// Get reply for the port
	_n = comm_channel_recv( this_->cc_master, &hdr, &msg, &len, &ntrans );
	if( _n < 0 ) return _n;
	n += _n;
	this_->ntrans += ntrans;

	// Check expected message size
	assert( hdr.msg_len == sizeof(uint16_t) );
	*wport_ = *(uint16_t *)msg;

	return n;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static ssize_t __sock_client_send_sigterm( sock_client_t *this_ )
{
	sock_tcp_header_t hdr;

	memset(&hdr,0,sizeof(hdr));
	hdr.msg_len = 0;
	hdr.opts = SOCK_OPTS_SIGTERM;

	return comm_channel_send( this_->cc_master, &hdr, NULL, 0, &this_->ntrans );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static int __sock_client_connect_worker( sock_client_t *this_ )
{
	int n;
	uint16_t wport=0;

	n = __sock_client_req_wport( this_, &wport );

	// Check if the master port was returned
	if( wport == ntohs(this_->cc_master->addr.sin_port) ) {
		this_->cc_worker = this_->cc_master;
	} else {
		if( !this_->cc_worker )
			this_->cc_worker = comm_channel_alloc(0);

		n = comm_channel_open( this_->cc_worker, this_->server_host, wport );
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
static int comm_channel_free( comm_channel_t **this_ )
{
	if( *this_ ) {
		buffer_dtor( &(*this_)->buf );
		free(*this_);
	}
	*this_ = NULL;
	return 0;
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
	if( this_->fd )
		this_->fd = s_close(this_->fd);
	return this_->fd;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static int comm_channel_reopen( comm_channel_t *this_ )
{
	if( this_->fd ) {
		this_->fd = s_close(this_->fd);
		if( this_->fd < 0 ) return this_->fd;
	}
	
	this_->fd = s_socket(AF_INET, SOCK_STREAM, 0);
	if( this_->fd < 0 ) return this_->fd;

	return 0;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static ssize_t comm_channel_send( comm_channel_t *this_, const sock_tcp_header_t *hdr_,
				  const void *msg_, size_t len_, size_t *ntrans_ )
{
	buffer_t *buf = &this_->buf;

	void *msg;
	size_t len;

	sock_tcp_header_t _hdr;
	sock_tcp_header_t *hdr;

	if( hdr_ ) { // Use provided header
		hdr = (sock_tcp_header_t *)hdr_;
	} else { // Construct header for the message
		hdr = &_hdr;
		memset(hdr,0,sizeof(*hdr));
		hdr->msg_len = len_;
	}	

	if( msg_ ) { 
		msg = (void *)msg_;
		len  = len_;
	} else { // Sending internal buffer
		msg = buf->data;
		len = buf->n;
	}

	return trans_socket( __send, this_->fd, hdr, msg, len, ntrans_ );
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static ssize_t comm_channel_recv( comm_channel_t *this_, sock_tcp_header_t *hdr_,
				  void **msg_, size_t *len_ , size_t *ntrans_ )
{
	ssize_t n=0, _n=0;
	size_t ntrans=0, _ntrans=0;

	buffer_t *buf = &this_->buf;

	sock_tcp_header_t _hdr;
	sock_tcp_header_t *hdr;

	if( hdr_ ) {
		hdr = hdr_;
	} else {
		hdr = &_hdr;
	}

	pid_t pid = getpid();

	printf("(%d) sock::comm_channel_recv: (%s) receiving header...\n", pid, comm_channel_msg);
	// Read the header
	_n = trans_socket( __recv, this_->fd, NULL, hdr, sizeof(*hdr), &_ntrans );
	if( _n < 0 ) return _n;
	n = _n;
	ntrans = _ntrans;
	printf("(%d) sock::comm_channel_recv: (%s) header size = %zd, msg len = %u\n", pid, comm_channel_msg, n, hdr->msg_len);

	// Make sure that the buffer is large enough
	printf("(%d) sock::comm_channel_recv: (%s) resizing buffer...\n", pid, comm_channel_msg);
	buffer_resize( buf, hdr->msg_len );
	buf->n = hdr->msg_len;
	printf("(%d) sock::comm_channel_recv: (%s) buffer len = %zd, number used = %zd\n", pid, comm_channel_msg, buf->len, buf->n);
	
	// Read the message
	printf("(%d) sock::comm_channel_recv: (%s) receiving msg...\n", pid, comm_channel_msg);
	_n = trans_socket( __recv, this_->fd, NULL, buf->data, buf->n, &_ntrans );
	if( _n < 0 ) return _n;
	n += _n;
	ntrans += _ntrans;
	printf("(%d) sock::comm_channel_recv: (%s) bytes read = %zd, expected = %zd\n", pid, comm_channel_msg, _n, buf->n);

	// Sanity check
	assert( n == (hdr->msg_len + sizeof(*hdr)) );

	// Provide reference to internal data
	if( msg_ ) {
		*msg_  = buf->data;
		*len_  = buf->n;
	}

	if( ntrans_ ) *ntrans_ = ntrans;
	
	return n;
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
		len = 1; // 1 bytes
	}
	this_->len = len;
	this_->n    = 0;
	this_->data = calloc(len,1);
	assert( this_->data );

	this_->alloc_len += len;

	printf("(%d) sock::buffer_ctor called: bytes_alloc = %zd\n", getpid(), this_->alloc_len);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void buffer_dtor( buffer_t *this_ )
{
	printf("(%d) sock::buffer_dtor called\n", getpid());
	if( this_->data ) {
		this_->alloc_len -= this_->len;
		printf("(%d) sock::buffer_dtor: free(data) called: bytes alloc = %zd\n", getpid(), this_->alloc_len);
		free( this_->data );
	} else {
		printf("(%d) sock::buffer_dtor: null data block found\n", getpid());
	}
	this_->len = 0;
	this_->n    = 0;
	assert( this_->alloc_len == 0 );
	
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

	this_->alloc_len += (len - this_->len);
	this_->len = len;

	printf("(%d) sock::buffer_resize called: bytes alloc = %zd\n", getpid(), this_->alloc_len);		

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
//
//------------------------------------------------------------------------------
static uint16_t get_sock_port( sock_server_t *this_ )
{
	socklen_t addr_len = sizeof( this_->addr );;
	
	getsockname(this_->fd,
		    (struct sockaddr *)&this_->addr,
		    &addr_len);
	return ntohs( this_->addr.sin_port);
}

//------------------------------------------------------------------------------
// Performs consecutive recvs to recv the entire stream block into the buffer.
// Ensures that the entire stream block is recv.
//------------------------------------------------------------------------------
static ssize_t trans_stream_block( ssize_t (*method_)(int fd_, void *data_, size_t n_, int flags_ ),
				   int fd_, void *data_, size_t n_, size_t *ntrans_ )
{
	ssize_t n;
	size_t len;
	size_t nt;

	ssize_t rc = 0;

	nt=0;
	len = 0;

	if( n_ == 0 ) goto fini;
	
	while(1) {
		nt++;
		n = method_(fd_, data_ + len, n_ - len, 0 );

		if( n < 0 ) { // Error occurred
			rc = n;
			goto fini;
		} else if( n == 0 && n_ - len != 0 ) { // Peer disconnect (set as error)
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
			     int fd_, sock_tcp_header_t *hdr_, void *data_, size_t len_, size_t *ntrans_ )
{
	ssize_t n=0, _n=0;
	size_t _ntrans=0;
	size_t ntrans=0;

	if( hdr_ ) {
		_n = trans_stream_block( method_, fd_, hdr_, sizeof(*hdr_), &_ntrans );
		if( _n < 0 ) return _n;
		n = _n;
		ntrans = _ntrans;
	}

	_n = trans_stream_block( method_, fd_, data_, len_, &_ntrans );
	if( _n < 0 ) return _n;
	n += _n;
	ntrans += _ntrans;
	
	if( ntrans_ ) *ntrans_ = ntrans;
	return n;
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

