# Mirage
**Mirage** is a standalone server host executable for SM64CoopDX. It's more optimized than using the game's headless mode due to it only handling networking and not running any of the game logic which frees up a lot of work for your device due to it not needing to run the game.

---
**Mirage** is currently still a work in progress but it's functional in its current state and it supports CoopNet and Direct Connect as network types (Direct Connect requires you to port forward)

# How to Use

## 1. Clone the Repository

```bash
git clone https://github.com/ManIsCat2/Mirage
cd Mirage
```

## 2. Build
(You can use MinGW and UCRT on Windows to to do this)
```bash
make -j$(nproc)
```

The compiled binary will be saved in the `build/` directory.

## 3. Run
```bash
./build/Mirage
```
Or on Windows:
```bash
./build/Mirage.exe
```
Note: Running it for the first time generates a JSON configuration file. Edit this file and restart Mirage to apply your  settings.