#ifndef MCMAP_POSIX_H
#define MCMAP_POSIX_H 1

#define mcmap_main main

#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

typedef int socket_t;
#define make_socket socket

typedef void *mmap_handle_t; /* the mapped-to address */

#endif /* MCMAP_POSIX_H */
