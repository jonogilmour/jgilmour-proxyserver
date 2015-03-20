
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <netdb.h>
#include <string.h>
#include <pthread.h>
#include "proxy_core.h"
#include "proxy_def.h"
#include "proxy_log.h"

pthread_mutex_t proxy_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 Strips the bare hostname from a request string.
 
 @param str The request string
 
 @returns Pointer to the beginning of hostname, or NULL on failure
*/
char* get_hostname(char* str) {
	char* search; // Stores the location of characters returned by strchr()

	// Search for the next space after the hostname
	search = strchr(str, ' ');
	
	// Check the request for errors
	if (search == '\0') {
		return NULL;
	}
	
	// Cuts off the string before the 'HTTP/1.0' part (end of URL)
	str[search - str] = '\0';
	
	int i;
	// Lower the whole hostname for compatibility (especially with curl or wget)
	for(i = 0; str[i]; i++){
		str[i] = tolower(str[i]);
	}
	
	if (str[search - str - 1] == '/') {
		// Cut off the trailing '/'
		str[search - str - 1] = '\0';
	}
	if (!strncmp(str, "http://", 7)) {
		// Url starts with 'http://' so remove that
		str = &str[7];
	}
	
	return str;
}

/*
 Gets the address from a sockaddr structure based on the address family
 
 @param sa The sockaddr structure with the address to convert
 
 @returns The address of the IP address field that corresponds to the address family.
 
*/
void *get_in_addr(sa_p sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((ip4addr*)sa)->sin_addr);
    }
	
    return &(((ip6addr*)sa)->sin6_addr);
}

/*
 Checks request string for the termination CRLFCRLF
 
 @param req The request string
 
 @returns 1 if the request has been terminating, or 0 otherwise
 */
int has_req_end(char* req) {
	return strstr(req, "\r\n\r\n") != NULL;
}

/*
 Retrieves a file from a webserver and sends the response through a socket
 
 @param ptr A pointer to a request structure
 
 @returns 0 on success
*/
void* get_and_send(void* ptr) {
	// Cast argument
	rb req = (rb)ptr;
	
	//////////////////////////////////
	// Check if the socket is valid //
	//////////////////////////////////
	if(req->sock <= 0) {
		fprintf(stderr, "x- Error for client %s: Socket invalid or does not exist\n", req->ip);
		send(req->sock, ERR_500, strlen(ERR_500), 0);
		close(req->sock);
		pthread_exit(0);
	}
	//////////////////////////////////
	//////////////////////////////////
	
	int socketDescriptor = 0; // Socket to send/receive with the webserver
	struct sockaddr_in *server_addr; // Will be used to store the socket information for the webserver
	struct addrinfo hints, // Hints struct for getaddrinfo()
	*server_addr_info, // Contains list of bindable addresses filled by getaddrinfo()
	*p; //used to iterate through the available addresses in server_addr_info
	char request[GET_REQ_SIZE + NULL_CHAR + strlen(req->hostname)]; // Stores the GET request string
	int returnv; // Used to contain the return value of getaddrinfo()
	char ip4_address[IP4_LEN]; // Stores string version of IPv4 address
	char ip6_address[IP6_LEN]; // Stores string version of IPv6 address
	char* ip_address; // Will point to either of the above IP strings
	
	///////////////////////////////////////////////////
	// Form the GET request to send to the webserver //
	///////////////////////////////////////////////////
	sprintf(request, "GET /%s HTTP/1.0\r\nHost: %s\r\n\r\n", req->file, req->hostname);
	///////////////////////////////////////////////////
	///////////////////////////////////////////////////
	
	//////////////////////////////////////////
	// Zero the hints structure and fill it //
	// with our preferences.                //
	//////////////////////////////////////////
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	//////////////////////////////////////////
	//////////////////////////////////////////
	
	////////////////////////////////////////////////////////
	// Lookup a bunch of valid IP and port numbers on the //
	// target webserver to connect to using HTTP.         //
	////////////////////////////////////////////////////////
	if( (returnv = getaddrinfo(req->hostname, "80", &hints, &server_addr_info)) != 0) {
		fprintf(stderr, "x- Error with hostname '%s' for client %s: %s\n", req->hostname, req->ip, gai_strerror(returnv));
		close(req->sock);
		free(req->file);
		free(req->hostname);
		free(req->ip);
		free(req);
		pthread_exit(0);
	}
	////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////
	
	// Free our hints structure
	freeaddrinfo(&hints);
	
	/////////////////////////////////////////
	// Get the IP address of the webserver //
	/////////////////////////////////////////
	for (p = server_addr_info; p != NULL; p = p->ai_next) {
		server_addr = (struct sockaddr_in*) p->ai_addr;
		
		if(p->ai_family == AF_INET) ip_address = ip4_address;
		else if(p->ai_family == AF_INET6) ip_address = ip6_address;
		else continue;
		
		strcpy(ip_address, inet_ntoa(server_addr->sin_addr));
	}
	/////////////////////////////////////////
	/////////////////////////////////////////
	
	/////////////////////////////////////////////////////
	// Find an IP to bind a socket to on the webserver //
	/////////////////////////////////////////////////////
	for (p = server_addr_info; p != NULL; p = p->ai_next) {
		socketDescriptor = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		
		if (socketDescriptor < 0) {
			// Couldn't create socket using the options for this IP and port
			continue;
		}
		
		if (connect(socketDescriptor, p->ai_addr, p->ai_addrlen) < 0) {
			// Cant connect to the given address using the socket
			close(socketDescriptor);
			socketDescriptor = -1;
			continue;
		}
		
		break; // Successfully connected using this socket
	}
	/////////////////////////////////////////////////////
	/////////////////////////////////////////////////////
	
	// Free the server address info structure
	freeaddrinfo(server_addr_info);
	
	///////////////////////////////////////////////
	// Use this socket to get the file specified //
	///////////////////////////////////////////////
	printf("-- Sending request to %s for client %s\n", req->hostname, req->ip);
    // Send a GET request to the webserver to get the file specified
	send(socketDescriptor, request, strlen(request), 0);
	printf("-- Receiving response from %s for client %s\n", req->hostname, req->ip);
	
	long int bytes_returned; // The total bytes returned by recv()
	char rbuffer[MAX_RECV+1]; // The buffer to store the recv()'d bytes in
	int total_bytes_returned = 0; // The total size of the response in bytes
	
	do {
		// Reset the buffer
		memset(rbuffer, 0, MAX_RECV+1);
		// Receive response whole/part from webserver
		bytes_returned = recv(socketDescriptor, rbuffer, MAX_RECV, 0);
		total_bytes_returned += bytes_returned;
		
		if(bytes_returned < 0) {
			// Some sort of error with recv
			close(socketDescriptor);
			printf("x- Error contacting server at %s for client %s. Host unreachable.\n", req->hostname, req->ip);
			send(req->sock, ERR_400, strlen(ERR_400), 0);
			close(req->sock);
			close(socketDescriptor);
			free(req->file);
			free(req->hostname);
			free(req->ip);
			free(req);
			pthread_exit(0);
		}
		
		// Try forwarding data chunk to the client
		if(send(req->sock, rbuffer, strlen(rbuffer), 0) < 0) {
			// Error sending data to client
			printf("x- Send to client %s failed, data now invalid, closing connection\n", req->ip);
			close(req->sock);
			close(socketDescriptor);
			free(req->file);
			free(req->hostname);
			free(req->ip);
			free(req);
			pthread_exit(0);
		}
		
	} while (bytes_returned > 0);
	///////////////////////////////////////////////
	///////////////////////////////////////////////

	printf("-- Forwarding response from %s to client %s\n", req->hostname, req->ip);
	
	////////////////////////////
	// Done. Log the transfer //
	////////////////////////////
	pthread_mutex_lock(&proxy_log_mutex);
	if(!req->nolog)	inlog(req->ip, req->port, total_bytes_returned, req->hostname);
	pthread_mutex_unlock(&proxy_log_mutex);
	////////////////////////////
	////////////////////////////
	
	close(req->sock);
	close(socketDescriptor);
	free(req->file);
	free(req->hostname);
	free(req->ip);
	free(req);
	return 0;
}
