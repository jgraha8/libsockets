#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include "global.h"
#include "sock.h"
#include "data_file.h"

#define N 32

// #define DATA_SIZE (4*1024*1024)

int main(int argc, char *argv[])
{

	sock_client_t sock[N];
	data_file_t d;
	
	int i,j;
	ssize_t n;

	char buffer[256];
	void *data;
	size_t *v;

	size_t data_size = 1024*strtol( argv[2], NULL, 10);

	size_t nelem = data_size / sizeof(size_t);

	d.size = data_size;
	printf("d.size = %zd\n", d.size);

	data = malloc( d.size + sizeof(d));
	
	v = (size_t *)((data_file_t *)data + 1);
	
	for( i=0; i<N; i++ ) {

		memset(data, 0, d.size+sizeof(d));

		for( j=0; j<nelem; j++ ) v[j] = j;
		
		if( sock_client_ctor( &sock[i], argv[1] ) < 0 ) perror("Unable to construct");
		if( sock_client_connect( &sock[i] ) < 0 ) {
			sprintf(buffer,"Unable to connect to %s", argv[1]);
			perror(buffer);
			exit(errno);
		}

		sprintf(d.name,"data-%d.bin", i);
		memcpy(data, &d, sizeof(d));

		printf("writing %zd bytes to %s...\n", sizeof(d) + d.size, d.name);

		pid_t fpid = fork();

		if( fpid == 0 ) {

			n=-1;
			while(1) {
				n = sock_client_send( &sock[i], data, sizeof(d) + d.size );
				if( n < 0 ) {
					printf("Unable to send %s...reconnecting\n", d.name);
					sleep(10);
					sock_client_reconnect( &sock[i] );
				} else {
					break;
				}
			}
			printf("%zd:%zd:required %zd sends.\n", n, sizeof(d)+d.size,sock[i].ntrans );

			memset(buffer,0,256);
			n = sock_client_recv( &sock[i], buffer, 255 );
	
			printf("Recv %zd bytes: %s\n",n, buffer);

			memset(buffer,0,256);	
			n = sock_client_recv( &sock[i], buffer, 255 );
			printf("Recv %zd bytes: %s\n", n, buffer);

			sock_client_dtor( &sock[i] );

			free(data);

			goto finish;
		} else if( fpid == -1 ) {
			fprintf(stderr,"unable to fork");
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
