# Makefile for VecXGL 1.1
# compile SDL project from cli like so:
#gcc -o test test.c `sdl-config --cflags --libs`

CC = gcc
RM = rm

# get the proper CFLAGS and LDFLAGS for SDL:
#SDL_CFLAGS := $(shell sdl-config --cflags)
#SDL_LDFLAGS := $(shell sdl-config --libs)

CFLAGS := $(shell sdl-config --cflags)
LDFLAGS := $(shell sdl-config --libs)
LDFLAGS += -lGL -lGLU

TARGET = vecxgl
OBJS = osint.o vecx.o e6809.o loadTGA.o

all: $(TARGET)

vecxgl: $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS)

clean:
	$(RM) -f $(TARGET)
	$(RM) -f $(OBJS)

# zip up the src code
#archive: $(OBJS)
#	tar -czvvf ../vecxgl_1.2_src.tar.gz ../VecXGL


