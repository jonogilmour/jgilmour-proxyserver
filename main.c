/*
 SIMPLE PROXY
 (c) 2014 Jonathan Gilmour
 SID: 540451
 
 Please see README.txt for more information
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <pthread.h>
#include <semaphore.h>
#include "proxy_log.h"
#include "proxy_core.h"
#include "proxy_def.h"

pthread_mutex_t proxy_mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, const char * argv[]) {
	
	// Make sure the request store is large enough to store a request
	if(DEBUG_ON) assert(MAX_RECV <= MAX_REQUEST_SIZE);
	
	/////////////////////////////////////////////////////////////
	// Check for enough command line arguments (should be one) //
	/////////////////////////////////////////////////////////////
	if (argc < 2) {
		fprintf(stderr, "Usage: <port-number>\n");
		exit(1);
	}
	/////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////
	
	if(DEBUG_ON) assert(argc >= 2);
	
	///////////////////////////////////////////////////////////
	// Get port number from argument and check it for errors //
	///////////////////////////////////////////////////////////
	char* pn = (char*)argv[1];
	errno = 0;
	unsigned int port_number = (unsigned int)strtoul(argv[1], &pn, BASE_TEN);
	if(errno) {
		// Error in strtoul
		fprintf(stderr, "Error: Invalid port number, please only enter numbers\n");
		exit(1);
	}
	if(argv[1] == pn) {
		// No-chars read by strtoul
		fprintf(stderr, "Error: Invalid port number\n");
		exit(1);
	}
	if (*pn != '\0') {
		// Non-numeric characters in port number
		fprintf(stderr, "Error: Invalid port number, please only enter numbers\n");
		exit(1);
	}
	if(port_number > MAX_PORT) {
		// Requested port number is beyond maximum
		fprintf(stderr, "Error: Port number must be less than 65535\n");
		exit(1);
	}
	///////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////
	
	if(DEBUG_ON) assert(port_number <= MAX_PORT);
	
	// Store string version of port number
	const char* port_number_str = argv[1];
	
	
	
	
	
	//////////////////////////////////////////////////////
	//////////////////////////////////////////////////////
	//////// PART 1: SET UP A LISTEN SOCKET //////////////
	//////////////////////////////////////////////////////
	//////////////////////////////////////////////////////
	
	int listen_socket = 0, connect_socket = 0; // The socket to listen for connections on, and the socket to accept connections onto.
	ai hints, // The hints structure to tell getaddrinfo() what we want in a connection
	*server_info, // The linked list that getaddrinfo() will populate
	*aip; // Used for traversing the linked list in server_info
	char ip_str[INET6_ADDRSTRLEN]; // Used to store human readable forms of IP addresses
	int yes = 1; // Used in the socket options
	FILE* proxy_log = NULL; // Proxy log file
	int no_logging = 0; // Flag to turn on/off the logging functionality
	
	//////////////////////////////////////////////////
	// Setup log file. If we can't create log file, //
	// the program will just continue with logging  //
	// disabled instead of exiting.                 //
	//////////////////////////////////////////////////
	proxy_log = fopen("proxy.log", "w+");
	if(!proxy_log) {
		printf("x- Could not create logging file. Logging disabled.\n");
		no_logging = 1;
	}
	else fclose(proxy_log);
	//////////////////////////////////////////////////
	//////////////////////////////////////////////////
	
	///////////////////////////////////////////////
	// Zero out the hints structure and give it  //
	// the specifications we need for the socket //
	///////////////////////////////////////////////
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	//////////////////////////////////////////////
	//////////////////////////////////////////////
	
	/////////////////////////////////////////////
	// Look for possible addresses to bind the //
	// listening socket to                     //
	/////////////////////////////////////////////
	int gai_error; //Stores the result of getaddrinfo()
	if ((gai_error = getaddrinfo(NULL, port_number_str, &hints, &server_info)) != 0) {
        // Error in getaddrinfo()
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_error));
        exit(1);
    }
	/////////////////////////////////////////////
	/////////////////////////////////////////////
	
	//////////////////////////////////////////////////
	// Try binding a socket to one of the addresses //
	// that getaddrinfo() returned                  //
	//////////////////////////////////////////////////
	for(aip = server_info; aip != NULL; aip = aip->ai_next) {
        
		// Try creating a socket for the given info
		if ((listen_socket = socket(aip->ai_family, aip->ai_socktype, aip->ai_protocol)) == -1) {
			// Couldn't set the socket for some reason, try the next addrinfo
            perror("server: socket");
            continue;
        }
		
		// Try setting options for the socket we have made
        if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
			// Couldn't set options
            perror("setsockopt");
            exit(1);
        }
		
		// Try to bind the socket to an IP and port number
        if (bind(listen_socket, aip->ai_addr, aip->ai_addrlen) == -1) {
			// Couldn't bind it, something wrong with the socket maybe? Close it and start again with a new socket.
            close(listen_socket);
            perror("server: bind");
            continue;
        }
		
		// Reached here, now have
		// - A socket to use
		// - Socket options all set
		// - Socket is associated with an IP and port number
		
        break;
    }
	//////////////////////////////////////////////////
	//////////////////////////////////////////////////
	
	////////////////////////////////////////////////////////////////
	// Check if we looped through every address and couldn't bind //
	// a socket to one.                                           //
	////////////////////////////////////////////////////////////////
	if (aip == NULL)  {
        fprintf(stderr, "x- Couldn't bind listen socket\n");
        exit(1);
    }
	////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////
	
	/////////////////////////////////////////////////////////////
	// Final check to make sure the socket was bound correctly //
	/////////////////////////////////////////////////////////////
	if(listen_socket <= 0) {
		fprintf(stderr, "x- Couldn't bind listen socket\n");
        exit(1);
	}
	/////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////
	
	////////////////////////////////////////////////////////
	// Tell the socket to begin listening for connections //
	////////////////////////////////////////////////////////
	if (listen(listen_socket, MAX_WAITING) == -1) {
        perror("listen");
        exit(1);
    }
	////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////
	
	
	
	
	
	///////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////
	//////// PART 2: BEGIN LISTENING FOR CONNECTIONS //////////////
	///////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////
	
	long int bytes_received; // The number of bytes returned by recv()
	int recv_err; // Flag set if some error occurs when receiving the request
	const int req_slots = MAX_REQUEST_SIZE + 1; // Size of the request string buffer
	char incoming_request[req_slots]; // Stores the request string
	char* target_hostname; // Stores the URL of the target webserver
	long int hostname_len; // Stores the length of target_hostname
	socket_address client_addr; // Used to store the client's address
	socklen_t client_addr_size; // Stores the size of client_addr
	char* req_point; // Used to traverse the request buffer
	int reqlen; // Tracks the length of the request string in bytes
	rb request; // GET request structure
	pthread_t thread; // Create a thread
	sem_t semaphore; // Used to limit number of threads that get/send at once
	
	// Initialise the semaphore to only allow MAX_THREADS to run the get_and_send function
	sem_init(&semaphore, 0, MAX_THREADS);
	
	// Zero the incoming request string
	memset(incoming_request, 0, req_slots);
	
	/////////////////////////////////////////////////////////////
	// Begin the main function fo the proxy, where connections //
	// are accepted, the requests parsed and handled, and the  //
	// webpage is returned to the client. This will loop fore- //
	// ver until the process is terminated or on error.        //
	/////////////////////////////////////////////////////////////
	while(1) {
		printf("\n- Proxy now running. Listening for incoming connections...\n");
		// Reset variables used in the accept phase
		recv_err = 0;
		connect_socket = 0;
		reqlen = 0;
		
		// Get the size of the client address structure
        client_addr_size = sizeof(client_addr);
		
		//////////////////////////////////////////////
		// Attempt to accept connection from client //
		// from the listen socket and place on a    //
		// new connect socket.                      //
		//////////////////////////////////////////////
 		connect_socket = accept(listen_socket, (sa_p)&client_addr, &client_addr_size);
        if(connect_socket < 0) {
			// Couldn't accept connection
            fprintf(stderr, "x- Couldn't bind connection socket\n");
            continue;
        }
		
		/////////////////////////////////////////////////////
		// Convert the IP address (v4 OR v6) of the client //
		// into human readable form and print it.          //
		/////////////////////////////////////////////////////
        inet_ntop(client_addr.ss_family, get_in_addr((sa_p)&client_addr), ip_str, sizeof(ip_str));

		printf("-- Received connection from client at %s\n", ip_str);
		
		///////////////////////////////////////////////////////
		// Final check to make sure connect socket was bound //
		// correctly.                                        //
		///////////////////////////////////////////////////////
		if(connect_socket <= 0) {
			fprintf(stderr, "x- Couldn't bind connection socket\n");
			close(connect_socket);
			continue;
		}
		///////////////////////////////////////////////////////
		///////////////////////////////////////////////////////
		
		/////////////////////////////////////////
		// Receive the request from the client //
		/////////////////////////////////////////
		req_point = incoming_request; // Set the request string pointer to the start of the buffer
		memset(incoming_request, 0, req_slots); // Zero the buffer
		printf("-- Receiving request from %s\n", ip_str);
		
		while(1) {
			// Receive part/whole request
			bytes_received = recv(connect_socket, req_point, MAX_RECV, 0);
			reqlen += bytes_received;
			
			if(bytes_received == 0 || has_req_end(incoming_request)) {
				// Connection was closed by client, or the end of request detected
				
				if(reqlen > MAX_REQUEST_SIZE) {
					//Request is too long for our buffer
					printf("x- Request from %s too long, closing connection", ip_str);
					recv_err = 1;
					break;
				}
				
				// Got the request in full
				break;
			}
			else {
				// Error in recv
				fprintf(stderr, "x- Error in recv for client %s, disconnecting...\n", ip_str);
				recv_err = 1;
				break;
			}
			
			if(DEBUG_ON) assert(reqlen < req_slots);
		}
		/////////////////////////////////////////
		/////////////////////////////////////////
		
		///////////////////////////////////////////////////////////
		// Check if there was a receive error, if so then ignore //
		// the request and close the connection.                 //
		///////////////////////////////////////////////////////////
		if(recv_err) {
			close(connect_socket);
			continue;
		}
		///////////////////////////////////////////////////////////
		///////////////////////////////////////////////////////////
		
		printf("-- Request from %s fully received\n", ip_str);
		
		//////////////////////////////////////////////////////////
		// Buffer contains whole request, so grab the hostname. //
		//////////////////////////////////////////////////////////
		char incoming_request_mutable[req_slots-4]; // Used to store a mutable copy of the request string to preserve the original
		char* req_pointer;
		
		// Copy over the request so we can manipulate it
		strcpy(incoming_request_mutable, &incoming_request[4]);
		
		// Clip the string to just contain the hostname
		req_pointer = get_hostname(incoming_request_mutable);
		if(!req_pointer) {
			// Malformed header
			fprintf(stderr, "x- Malformed header for client IP %s. Request body follows:\n--------------\n'%s'\n---------------\n", ip_str, incoming_request);
			close(connect_socket);
			continue;
		}
		
		// Rename the URL string for clarity
		target_hostname = req_pointer;
		hostname_len = strlen(target_hostname);
		//////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////
		
		/////////////////////////////////////////////////////////
		// Form the GET request, send it to the target server, //
		// receive the response, and forward it back to our    //
		// client.                                             //
		/////////////////////////////////////////////////////////
		printf("-- Beginning request from %s to target server at %s...\n", ip_str, target_hostname);
		
		// Fill out our request structure
		request = (rb)malloc(sizeof(struct request_body));
		request->port = port_number;
		request->sock = connect_socket;
		request->hostname = (char*)calloc(sizeof(char), (strlen(target_hostname) + NULL_CHAR));
		strcpy(request->hostname, target_hostname);
		request->ip = (char*)calloc(sizeof(char), (strlen(ip_str) + NULL_CHAR));
		strcpy(request->ip, ip_str);
		request->file = (char*)calloc(sizeof(char), (strlen(INDEX_FILE) + NULL_CHAR));
		strcpy(request->file, INDEX_FILE);
		request->nolog = no_logging;
		
		// Use the semaphore to control traffic
		sem_wait(&semaphore);
		pthread_create(&thread, 0, get_and_send, (void *)request);
		pthread_detach(thread);
		sem_post(&semaphore);
		///////////////////////////////////////////////////////////////
		///////////////////////////////////////////////////////////////
		
		// Continue on to accept waiting connections
		continue;
    }
	
	// Clean up
	pthread_mutex_destroy(&proxy_mutex);
	sem_destroy(&semaphore);
    return 0;
}

