
#ifndef proxy_proxy_def_h
#define proxy_proxy_def_h

#define MAX_WAITING 10
#define MAX_RECV 256
#define BASE_TEN 10
#define MAX_REQUEST_SIZE 2048
#define DEBUG_ON 1
#define MAX_PORT 65535
#define GET_BODY_LEN 26
#define IP4_LEN 16
#define IP6_LEN 45
#define GET_REQ_SIZE 26
#define NULL_CHAR 1
#define MAX_FILE_SIZE 1048576
#define INDEX_FILE ""
#define MAX_THREADS 10
#define ERR_500 "500: Proxy server error"
#define ERR_400 "400: Host unreachable or invalid"

typedef struct addrinfo ai;
typedef struct sockaddr_storage socket_address;
typedef struct sockaddr* sa_p;
typedef struct sockaddr_in ip4addr;
typedef struct sockaddr_in6 ip6addr;

struct request_body {
	char* hostname;
	char* file;
	int sock;
	int port;
	char* ip;
	int nolog;
};
typedef struct request_body* rb;

#endif
