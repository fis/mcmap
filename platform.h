#ifndef MCMAP_PLATFORM_H
#define MCMAP_PLATFORM_H 1

#if defined(PLATFORM_POSIX)
#include "posix.h"
#elif defined(PLATFORM_WIN32)
#include "win32.h"
#else
#error "Unsupported platform! Try -DPLATFORM_(POSIX|WIN32)."
#endif

void socket_init(void);

socket_t make_socket(int domain, int type, int protocol);

/* Prepare socket for recv/send use after it's been connected and whatnot */
void socket_prepare(socket_t socket);

int socket_recv(socket_t socket,       void *buf, int len, int flags);
int socket_send(socket_t socket, const void *buf, int len, int flags);

void console_init(void);
void console_cleanup(void);

#endif /* MCMAP_PLATFORM_H */
