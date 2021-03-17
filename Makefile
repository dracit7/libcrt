
LIBPCRT_SO = libpcrt.so

LIBPCRT_SRC = \
	pthread.c \
	coroutine.c \

LIBPCRT_HEADERS = \
	common.h \
	coroutine.h \

$(LIBPCRT_SO): $(LIBPCRT_SRC) $(LIBPCRT_HEADERS)
	gcc -fPIC -shared -I. -ldl -g -o $(LIBPCRT_SO) $^

all: $(LIBPCRT_SO)

clean:
	rm -f $(LIBPCRT_SO)