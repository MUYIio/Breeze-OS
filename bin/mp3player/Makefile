X_LIBS		+= libxlibc.a
X_INCDIRS	+= mp3player/helix/pub

X_CFLAGS	+= -w

NAME		:= mp3player
SRC			+= mp3_buf.c mp3.c play.c helix/

define CUSTOM_TARGET_CMD
echo [APP] $@; \
$(LD) $(X_LDFLAGS) $(X_OBJS) -o $@ $(patsubst %, -L%, $(X_LIBDIRS)) --start-group $(patsubst %, -l:%, $(X_LIBS)) --end-group; \
cp $@ $(srctree)/../develop/rom/bin
endef
