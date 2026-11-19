# Radiance - Player Light Source for Enshrouded

Nights in Embervale are dark, and caves are darker. Radiance makes your character give off light, so you can see without holding a torch.

- The default is the light of a held Wax Torch: the same colour, brightness and reach.
- No item, no crafting, no slot. Client side only: nothing goes on the server and nothing changes in your save.
- **F8** turns the light on or off. **Shift+F8** opens a small menu drawn with the game's own interface: a switch, Brightness (up to three times a torch), Range (up to 300 m) and nine colours. It is used with the mouse (the camera stays still while it is open); Esc or Close when done. A controller opens it with LB + D-pad Left and moves through it with the D-pad.
- Players who both have Radiance see each other's lights, on any server (`src/sync.h`). Tested by one player only so far.
- The menu comes in all 15 languages the game supports, following the game's language setting. The texts are in `src/strings.h`.

Download and install instructions: see the [Nexus Mods page](https://www.nexusmods.com/enshrouded/mods/384). The player-facing readme is [dist/README.txt](dist/README.txt).

## Settings

`greeneye\radiance\radiance.ini`, each setting documented in the file. It is read again within a second of being saved, and the menu writes what the player changes into it. Colour as RGB, flicker, height, shadows, the multiplayer switches and every key are set there.

## Building

Needs Visual Studio 2022 Build Tools (x64) and Python 3 with Pillow for the tools. The source includes the shared headers in `..\common` (`greeneye.h`, `input_binds.h`, `hidden_chat.h`).

```
build.bat                     -> bin\loader\radiance.dll, mod.json, radiance.ini
build-dev.bat                 -> bin-dev\loader\... with RADIANCE_DEV and GREENEYE_INPUT_TEST (testing aids; never ship it)
tools\package.py              -> dist\Radiance-<version>.zip
tools\make_nexus_images.py    -> dist\nexus\images\*.jpg (the listing images, from assets\raw)
tools\vfxparse.py             -> reads the dev build's effect survey (greeneye\radiance\vfxdump.txt)
```

The mod runs through the Greeneye Loader (API 2). Nothing is a fixed address: every game function is found by byte signature, layout offsets are read out of the game's own code or its reflection tables at start, and effect parameters go by the hash of their name. If something is missing, the affected part (the menu, the shared lights, or the light itself) stays off and `greeneye\loader.log` says why. Every detour holds a loader lease, so the mod hot reloads.

How it fits together:

- `main.cpp`: the light. One of the game's own light-only effects, started in the renderer's effect manager at the local player and moved each frame at the frame's staging call; its `light_*` parameters come from the settings. The player's position is the RenderTransform of the row the ECS system `player_camera_transform` marks as local. Also the settings file and its watcher thread.
- `menu.h`: the hotkeys and the panel, drawn in the in-game UI pass. While it is open a window procedure in front of the game's takes the raw mouse (so the camera gets none), the mouse buttons and Esc.
- `sync.h`: other players' lights, over hidden chat lines (entry type 255, `common/hidden_chat.h`) that every server relays and the chat window never shows.
- `reflect.h`: field offsets and enum values by name from the game's reflection tables.
- `strings.h`: the menu's words in 15 languages.

## License

MIT, see [LICENSE](LICENSE). Made with the help of AI (Claude by Anthropic). Enshrouded and its assets belong to Keen Games.
