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

void console_init(void);
void console_cleanup(void);

mmap_handle_t make_mmap(int fd, size_t len, void **addr);
mmap_handle_t resize_mmap(mmap_handle_t old, void *old_addr, int fd, size_t old_len, size_t new_len, void **addr);
void sync_mmap(void *addr, size_t len);

#endif /* MCMAP_PLATFORM_H */
