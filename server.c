/* A simple server in the internet domain using TCP
   The port number is passed as an argument */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#include "global.h"
#include "sock.h"

#define FILE_SIZE (1*1024*1024*1024)


int main(int argc, char *argv[])
{
	sock_server_t server;
	pid_t cpid;
	size_t len;
	char *fname;
	void *buffer;
	char msg[256];

	sock_server_ctor( &server );
	sock_server_bind( &server );

	sock_server_listen( &server );

	while(1) {
		
		sock_server_accept( &server );

		sock_server_fork( &server );

		if( !server.parent ) {

			cpid = getpid();
			
			sock_server_read( &server, &len, &buffer );
			
			fname = (char *)buffer;
			
			printf("Received %zd bytes\n", len);
			printf("Here is the file name: %s\n", fname);

			sprintf(msg,"PID %d creating file %s of %d MB ...", cpid, fname, FILE_SIZE/1024);

			sock_server_write( &server, strlen(msg)+1, (void *)msg);
				
			char *d = malloc(FILE_SIZE);
			memset( d, 0, FILE_SIZE);
				
			FILE *fd = fopen(fname,"wb");
			fwrite(d, 1, FILE_SIZE, fd);;
			fclose(fd);

			sprintf(msg,"PID %d done", cpid);
			sock_server_write( &server, strlen(msg)+1, (void *)msg);

			sock_server_close( &server );
			sock_server_dtor( &server );
				
		}
	}
	return 0;
	
}
