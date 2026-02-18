CFLAGS := $(shell sdl2-config --cflags) -Wall -Wextra
LDFLAGS := $(shell sdl2-config --libs) -lGLEW -lGL -lm

SRCS := gl_renderer.c main.c

forge:
	rm -rf build/
	mkdir build
	gcc $(CFLAGS) $(SRCS) -o build/forge $(LDFLAGS)

clean:
	rm -rf build/
