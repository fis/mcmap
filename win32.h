#ifndef MCMAP_WIN32_H
#define MCMAP_WIN32_H 1

#undef main

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET socket_t;

typedef HANDLE mmap_handle_t; /* mapping object handle */

#endif /* MCMAP_WIN32_H */
