# Mirage
**Mirage** is a standalone server host executable for SM64CoopDX. It's more optimized than using the game's headless mode due to it only handling networking and not running any of the game logic which frees up a lot of work for your device due to it not needing to run the game.

---
**Mirage** is currently still a work in progress but it's functional in its current state and it supports CoopNet and Direct Connect as network types (Direct Connect requires you to port forward)

## How to Use
Using **Mirage** is very simple:
1. Using MSYS2 MingW64 run `make -j$(nproc)` inside the repo to compile your executable
2. Go into `build` and run the executable inside any terminal you like by dragging and dropping into the terminal
3. Keep the terminal open and now you can join the server by either using CoopNet if you set that as the network type or by using your IP address if you're using direct connect
