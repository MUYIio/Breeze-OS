X_LIBS		+= libxlibc.a libpthread.a

X_INCDIRS	+= netserv/include netserv/lwip/include netserv/lwip/include/ipv4

X_CFLAGS	+= -w

NAME		:= netserv
SRC			+= main.c 
SRC			+= netcard.c 
SRC			+= netmain.c 
SRC			+= httpd.c 
SRC			+= netremote.c 
SRC			+= socket_udp.c
SRC			+= lwip/ 

define CUSTOM_TARGET_CMD
echo [APP] $@; \
$(LD) $(X_LDFLAGS) $(X_OBJS) -o $@ $(patsubst %, -L%, $(X_LIBDIRS)) --start-group $(patsubst %, -l:%, $(X_LIBS)) --end-group; \
cp $@ $(srctree)/../develop/rom/sbin
endef
