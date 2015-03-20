
#ifndef proxy_proxy_core_h
#define proxy_proxy_core_h

#include "proxy_def.h"

char* get_hostname(char* str);
void* get_and_send(void* ptr);
void *get_in_addr(sa_p sa);
int has_req_end(char* req);

#endif
