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
#include <stdbool.h>
#include <errno.h>

#include "global.h"
#include "sock.h"
#include "data_file.h"

#define MAX_WORKER 7

volatile sig_atomic_t wrk_count = 0;
volatile sig_atomic_t run = 1;

static sock_server_t server;

void fini();
void wait_all();
void reset_worker_counter();
void worker_counter_error( const char *msg_ );
void sigterm_handler(int sig);
void sigchld_handler(int sig);

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void fini() {
	if( server.flags & SOCK_SF_PARENT ) wait_all();
	sock_server_dtor( &server );
}
	

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool check_data( size_t n_, const void *data_ )
{
	size_t n;
	const size_t *v = (const size_t *)data_;
	for( n=0; n<n_; n++ ) {
		if( v[n] != n ) {
			printf("v[%zd] = %zd\n", n, v[n]);
			return false;
		}
	}
	return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void wait_all()
{
	pid_t pid;
	while (1) {
		if( (pid = waitpid(-1, NULL, 0)) == -1 ) break;
	}
	
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void reset_worker_counter()
{
	signal(SIGCHLD,SIG_IGN);
	wait_all();
	wrk_count=0;
	signal(SIGCHLD, sigchld_handler);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void worker_counter_error( const char *msg_ )
{
	fprintf(stderr,"ERROR in worker counter: %s: resetting counter\n", msg_);
	reset_worker_counter();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sigterm_handler(int sig)
{
	// Only the parent process respondes to signal
	if( server.flags & SOCK_SF_PARENT ) {
		printf("Caught signal %d: finishing current jobs\n",sig);
		fini(); 
		exit(sig);
	}
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void sigchld_handler(int sig)
{
	pid_t pid;
	int   status;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		wrk_count--;
		printf("PID %d finished: wrk_count = %d\n", pid, wrk_count);
		if( wrk_count < 0 ) worker_counter_error( "worker_count < 0" );
	}	
}

void sys_error( const char *msg_ )
{
	perror(msg_);
	exit(errno);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int main(int argc, char *argv[])
{


	pid_t cpid;
	size_t len;
	ssize_t n;

	void *buffer;
	char msg[256];

	data_file_t d;
	void *data;

	sock_server_t worker;

	signal(SIGCHLD, sigchld_handler);
	signal(SIGINT,  sigterm_handler);
	signal(SIGTERM, sigterm_handler);
	signal(SIGHUP,  SIG_IGN);
	
	if( sock_server_ctor( &server, PORTNO, &worker ) < 0 ) sys_error("cant construct");
	//if( sock_server_bind( &server ) < 0 ) sys_error("cant bind");
	//if( sock_server_listen( &server ) < 0 ) sys_error( "cant listen");

	while(1) {
		
		sock_server_accept( &server );

		while( wrk_count == MAX_WORKER ) {
			printf("Maximum workers reached: waiting...\n");
			sleep(5);
		}

		wrk_count++;
		if( wrk_count > MAX_WORKER ) {
			worker_counter_error( "worker_count > MAX_WORKER" );
		}		
		pid_t fpid  = sock_server_fork( &server );			

		if( fpid == 0 ) { // Child

			cpid = getpid();

			printf("PID %d: receiving 1...\n", cpid);
			
			n = sock_server_recv( &server, &buffer, &len );
			if( n < 0 ) { // Error occured
				printf("PID %d: ERROR %d recieving data\n", cpid, sock_errno);
				goto fini;
			}

			memcpy( &d, buffer, sizeof(d) );
			data = (void *)((data_file_t *)buffer + 1);

			assert( check_data( d.size/sizeof(size_t), data ) );

			printf("Received %zd bytes in %zd transfers\n", len, server.ntrans);
			printf("Here is the file name: %s\n", d.name);
		 
			sprintf(msg,"PID %d creating file %s of %zd MB ...", cpid, d.name, d.size/1024/1024);

			printf("PID %d sending 1...\n", cpid);			
			n = sock_server_send( &server, (void *)msg, strlen(msg)+1);
			if( n < 0 ) { // Error occured
				printf("PID %d: ERROR %d sending data\n", cpid, errno);
				goto fini;
			}			

			FILE *fd = fopen(d.name,"wb");
			fwrite(data, 1, d.size, fd);
			fclose(fd);

			printf("PID %d sending 2...\n", cpid);			
			sprintf(msg,"PID %d done", cpid);
			n  = sock_server_send( &server, (void *)msg, strlen(msg)+1);
			if( n < 0 ) { // Error occured
				printf("PID %d: ERROR %d sending data\n", cpid, errno);
				goto fini;
			}			

		fini:
			break;
		}
	}

	fini();
	
	return 0;
	
}
