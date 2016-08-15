/* A simple server in the internet domain using TCP
   The port number is passed as an argument */
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include "global.h"
#include "sock.h"
#include "data_file.h"

#define MAX_WORKER 7

static int nworker=0;

void handler(int sig)
{
	nworker--;
	assert( nworker >= 0 );
	pid_t pid = wait(NULL);
	printf("Pid %d finished: nworker = %d\n", pid, nworker);
}

int main(int argc, char *argv[])
{

	sock_server_t server;
	pid_t cpid;
	size_t len;

	void *buffer;
	char msg[256];

	data_file_t d;
	void *data;

	signal(SIGCHLD, handler);

	sock_server_ctor( &server );
	sock_server_bind( &server );

	sock_server_listen( &server );

	while(1) {
		
		sock_server_accept( &server );
		
		while( nworker == MAX_WORKER ) {
			sleep(1);
			printf("Maximum workers reached: waiting...\n");
		} 		
  
		nworker++;
		assert( nworker <= MAX_WORKER );
		sock_server_fork( &server );

		if( !server.parent ) {

			cpid = getpid();

			printf("PID %d reading 1...\n", cpid);
			
			sock_server_read( &server, &buffer, &len );

			memcpy( &d, buffer, sizeof(d) );
			data = (void *)((data_file_t *)buffer + 1);			

			printf("Received %zd bytes in %zd transfers\n", len, server.ntrans);
			printf("Here is the file name: %s\n", d.name);
		
			sprintf(msg,"PID %d creating file %s of %zd MB ...", cpid, d.name, d.size/1024/1024);

			printf("PID %d writing 1...\n", cpid);			
			sock_server_write( &server, (void *)msg, strlen(msg)+1);

			FILE *fd = fopen(d.name,"wb");
			fwrite(data, 1, d.size, fd);
			fclose(fd);

			printf("PID %d writing 2...\n", cpid);			
			sprintf(msg,"PID %d done", cpid);
			sock_server_write( &server, (void *)msg, strlen(msg)+1);

			sock_server_close( &server ); // Closes client connection
			break;
		} else {
		}
	}

	sock_server_dtor( &server );
	
	return 0;
	
}
