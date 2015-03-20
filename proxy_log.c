
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include "proxy_log.h"
#include <unistd.h>
#include <string.h>

/*
 Writes a line to a log file in the format:
 "date time,client ip address,port number,number bytes sent,requested hostname"
 
 @param proxy_log Pointer to open log file
 @param ip The ip address of the client
 @param port The port of the client
 @param bytes_sent The total bytes sent to the client
 @param hostname The hostname requested by the client
*/
void inlog(const char* ip, int port, int bytes_sent, const char* hostname) {
	FILE* proxy_log;
	if(access("proxy.log", F_OK) != -1) proxy_log = fopen("proxy.log", "a");
	else proxy_log = fopen("proxy.log", "w");
	
	if (!ip || !port || bytes_sent < 0 || !hostname) {
		fprintf(stderr, "x- Error printing to log file: Invalid arguments (ip: %s, port: %d, bytes_sent: %d, hostname: %s)\n", ip, port, bytes_sent, hostname);
	}
	printf("-- Writing to log file for client %s...\n", ip);
	time_t timenow;
	struct tm* time_info;
	
	time(&timenow);
	time_info = localtime(&timenow);
	char* time_str = asctime(time_info);
	time_str[strlen(time_str)-1] = '\0';
	
	fprintf(proxy_log, "%s,%s,%d,%d,%s\n", time_str, ip, port, bytes_sent, hostname);
	fclose(proxy_log);
}
