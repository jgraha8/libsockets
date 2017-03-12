#define _MULTI_THREADED
#define _GNU_SOURCE
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>

#include "global.h"
#include "sock.h"
#include "data_file.h"

int main(int argc, char *argv[])
{

	sock_client_t sock;
	data_file_t header;

	char *server_name=NULL;
	char *file_name=NULL;

	server_name = argv[1];
	file_name   = argv[2];

	FILE *fd = fopen( file_name, "rb" );
	assert( fd );
	fseek(fd, 0L, SEEK_END);
	size_t file_len = ftell(fd);
	fclose(fd);

	int fp = open( file_name, O_RDONLY );

	void *data=NULL;
	int pagesize=getpagesize();
	size_t data_len = pagesize;
	size_t read_len=0;

	ssize_t n;
	char buffer[256];
	char *msg;
	size_t msg_len;	

	if( sock_client_ctor( &sock, server_name, PORTNO ) < 0 )
		perror("Unable to construct");
	if( sock_client_connect( &sock, 0 ) < 0 ) {
		sprintf(buffer,"Unable to connect to %s", server_name);
		perror(buffer);
		exit(errno);
	}

	
	memset( &header, 0, sizeof(header) );
	strncpy( header.name, file_name, sizeof(header.name)-1);
	header.size = file_len;

	printf("writing file (%zd bytes) to %s on %s...\n", sizeof(header) + file_len, header.name, server_name);

	if( (n = sock_client_send( &sock, &header, sizeof(header) )) < 0 ) {
		sprintf(buffer,"unable to send file header");
		perror(buffer);
		goto fini;
	}
#define min(a,b) ( (a) < (b) ? (a) : (b) )

	int i=0;
	while(1) {
		printf("i = %d", i++);
		// Map one page at a time
		data = mmap( data, file_len, PROT_READ, MAP_PRIVATE, fp, 0 );
		assert( data );
		
		if( (n = sock_client_send( &sock, data, file_len )) < 0 ) {
			sprintf(buffer,"unable to send data file");
			perror(buffer);
			goto fini;
		}
		munmap( data, file_len );
		read_len += data_len;		     
	}

	printf("%zd:%zd:required %zd sends.\n", sizeof(header) + file_len, sizeof(sock_tcp_header_t), sock.ntrans );

	//memset(buffer,0,256);
	n = sock_client_recv( &sock, (void **)&msg, &msg_len );
	
	printf("recv %zd bytes: %s\n", n, msg);

	//memset(buffer,0,256);	
	n = sock_client_recv( &sock, (void **)&msg, &msg_len );
	printf("recv %zd bytes: %s\n", n, msg);

	sock_client_dtor( &sock );

	// Tell the server to shutdown
	/* printf("Sending SIGTERM to server...\n"); */
	/* if( sock_client_ctor( &sock, server_name, PORTNO ) < 0 ) { */
	/* 	perror("Unable to construct client socket"); */
	/* 	goto fini; */
	/* } */
	/* if( sock_client_connect( &sock, SOCK_OPTS_SIGTERM ) < 0 ) */
	/* 	perror("Unable to send SIGTERM"); */

 fini:

	munmap(data, data_len);
	
	return 0;
}
