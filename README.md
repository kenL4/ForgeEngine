# Forge Engine

This is a lightweight game engine, which uses raymarching as the primary rendering method. Raymarching allows for unique manipulation of shapes and cool effects which would be complex to implement in non-SDF based engines. It is heavily based on the legendary work of Inigo Quilez (definitely check out their work).

## Dependencies
You need to install SDL2 and OpenGL.

On Ubuntu, we can do:
```
sudo apt install libsdl2-dev libglew-dev libgl1-mesa-dev
```

## How to run

1. Run `make clean && make forge`
2. Run `./build/forge`