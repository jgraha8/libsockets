
#include "global.h"
#include "sock.h"


int main(int argc, char *argv[])
{

	sock_client_t sock[10];
	
	int i,n;

	char buffer[256];

	for( i=0; i<10; i++ ) {
		sock_client_ctor( &sock[i], argv[1] );
		sock_client_connect( &sock[i] );		
	}
	
	for( i=0; i<10; i++ ) {
		sprintf(buffer,"data-%d.bin", i);
		
		sock_client_write( &sock[i], strlen(buffer)+1, buffer );
	
		memset(buffer,0,256);
		sock_client_read( &sock[i], 255, buffer );
	
		printf("%s\n",buffer);
	}

	for( i=0; i<10; i++ ) {
		memset(buffer,0,256);	
		sock_client_read( &sock[i], 255, buffer );
		printf("%s\n",buffer);
		sock_client_dtor( &sock[i] );		
	}
	
	return 0;
}
