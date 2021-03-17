
#ifndef _LIBPCRT_COMMON_H
#define _LIBPCRT_COMMON_H

#include <malloc.h>
#include <stdlib.h>
#include <errno.h>

#define DEBUG

#ifdef DEBUG
#define log(type, msg, ...) printf("[" #type "] " msg "\n", ##__VA_ARGS__)
#else
#define log(type, msg, ...)
#endif

#define fault(msg, ...) do { printf("Error: " msg "\n", ##__VA_ARGS__); exit(EXIT_FAILURE); } while (0)

#endif