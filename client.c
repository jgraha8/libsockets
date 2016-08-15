#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include "global.h"
#include "sock.h"
#include "data_file.h"

#define N 32

#define DATA_SIZE (40*1024*1024)

int main(int argc, char *argv[])
{

	sock_client_t sock[N];
	data_file_t d;
	
	int i;
	size_t n;

	char buffer[256];
	void *data;

	d.size = DATA_SIZE;
	data = malloc( d.size + sizeof(d));

	for( i=0; i<N; i++ ) {

		memset(data, 0, d.size+sizeof(d));
		
		sock_client_ctor( &sock[i], argv[1] );
		sock_client_connect( &sock[i] );

		sprintf(d.name,"data-%d.bin", i);
		memcpy(data, &d, sizeof(d));

		printf("writing %zd bytes...\n", sizeof(d) + d.size);

		pid_t fpid = fork();

		if( fpid == 0 ) {
			n = sock_client_write( &sock[i], data, sizeof(d) + d.size );

			printf("required %zd writes.\n", sock[i].ntrans );
	
			memset(buffer,0,256);
			n = sock_client_read( &sock[i], buffer, 255 );
	
			printf("Read %zd bytes: %s\n",n, buffer);

			memset(buffer,0,256);	
			n = sock_client_read( &sock[i], buffer, 255 );
			printf("Read %zd bytes: %s\n", n, buffer);

			sock_client_dtor( &sock[i] );

			free(data);

			goto finish;
		}
		
	}

	pid_t pid;
	while (1) {
		pid = waitpid(-1, NULL, 0);
		if (errno == ECHILD) {
			break;
		}
	}	

 finish:
	
	return 0;
}
