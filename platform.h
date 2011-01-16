#ifndef MCMAP_PLATFORM_H
#define MCMAP_PLATFORM_H 1

#if PLATFORM == posix
#include "posix.h"
#elif PLATFORM == win32
#include "win32.h"
#else
#error "Unsupported platform! Try -DPLATFORM=(posix|win32)."
#endif

socket_t make_socket(int domain, int type, int protocol);

void console_init(void);
void console_cleanup(void);

#endif /* MCMAP_PLATFORM_H */
