#ifndef MCMAP_PLATFORM_H
#define MCMAP_PLATFORM_H 1

#if defined(PLATFORM_POSIX)
#include "posix.h"
#elif defined(PLATFORM_WIN32)
#include "win32.h"
#else
#error "Unsupported platform! Try -DPLATFORM_(POSIX|WIN32)."
#endif

socket_t make_socket(int domain, int type, int protocol);

void console_init(void);
void console_cleanup(void);

#endif /* MCMAP_PLATFORM_H */
