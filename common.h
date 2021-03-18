
#ifndef _LIBPCRT_COMMON_H
#define _LIBPCRT_COMMON_H

#include <malloc.h>
#include <stdlib.h>
#include <errno.h>

#define DEBUG

#define cBLK "\x1b[0;30m"
#define cRED "\x1b[0;31m"
#define cGRN "\x1b[0;32m"
#define cBRN "\x1b[0;33m"
#define cBLU "\x1b[0;34m"
#define cMGN "\x1b[0;35m"
#define cCYA "\x1b[0;36m"
#define cLGR "\x1b[0;37m"
#define cGRA "\x1b[1;90m"
#define cLRD "\x1b[1;91m"
#define cLGN "\x1b[1;92m"
#define cYEL "\x1b[1;93m"
#define cLBL "\x1b[1;94m"
#define cPIN "\x1b[1;95m"
#define cLCY "\x1b[1;96m"
#define cBRI "\x1b[1;97m"
#define cRST "\x1b[0m"

#ifdef DEBUG
#define log(type, msg, ...) printf(type " " msg "\n", ##__VA_ARGS__)
#else
#define log(type, msg, ...)
#endif

#define debug(msg, ...) log(cBLU "[debug]" cRST, cGRA msg cRST, ##__VA_ARGS__)
#define info(msg, ...) log(cLGN "[info]" cRST, msg, ##__VA_ARGS__)

#define fault(msg, ...) do { printf(cLRD "[Error] " cRST msg "\n", ##__VA_ARGS__); exit(EXIT_FAILURE); } while (0)

#endif