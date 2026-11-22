Radiance - Player Light Source 1.1.0 for Enshrouded
===================================================

Too dark to see? Your character gives off light, so you don't have to hold a torch.
Client side only: install it on your own game. The server needs nothing.

How to use it
-------------
1. Load into a world. The light is on, with the color, brightness and reach of a Wax Torch.
2. F8 turns the light off and on.
3. Shift+F8 opens the Radiance menu: a switch, Brightness, Range and nine colors. Use it with
   the mouse; the camera stays still while it is open. Esc or Close when you are done.
   Controller: hold LB and press D-pad Left; the D-pad moves through the menu, B closes.

Install
-------
1. Close Enshrouded.
2. Open the game folder (Steam: right-click Enshrouded > Manage > Browse local files).
3. Copy everything from this zip into it, keeping the folders: dbgcore.dll next to
   enshrouded.exe, and the greeneye\radiance folder.
4. Start the game.

That's all, for single player, hosted games and dedicated servers alike. Nothing goes on the
server. Install by hand: Vortex does not know the Greeneye Loader yet.

Multiplayer
-----------
Players who have Radiance see each other's lights, each in its owner's color, brightness and
range; the server needs nothing for that. The mods find each other with one hidden chat line when
you join a world, which players without Radiance see as one empty chat message; nothing more is
sent unless another Radiance player is there. This part has only been tested by one player so
far. ShareMyLight and ShowOtherLights in the settings file turn it off.

Settings
--------
greeneye\radiance\radiance.ini, each setting explained in the file. Changes apply within a
second of saving it, and the menu writes what you change into the same file.
  Enabled, Intensity, Radius   the menu's switch, Brightness and Range (1 and 45 are a Wax Torch)
  Color=255, 137, 88           any color, as red, green, blue or a web color such as #FF8958
  Flicker, Height, Shadows     how the light flickers, where it sits, whether it casts shadows
  ToggleLight=F8               the keys; a controller button goes after a comma
  OpenMenu=Shift+F8, Pad:LB+Left
  Language=auto                the menu's language, if you want another than the game's

Uninstall
---------
Delete greeneye\radiance. If no other Greeneye mod is left, also delete dbgcore.dll and the
greeneye folder.

Troubleshooting
---------------
Please attach this file to a bug report:
  greeneye\loader.log in the game folder (the mod's lines start with "Radiance:").
No light at all? Check that dbgcore.dll sits next to enshrouded.exe, that Enabled=true is in
radiance.ini, and press F8 once.

Changes
-------
1.1.0
  - The menu looks like the game's own dialogs: the framed window Find's search uses, the
    game's stone Close button, and slider rows laid out like the game's Settings screen.
  - Far fewer hidden chat lines: players without Radiance saw an empty chat message every 10
    seconds. Now one is sent when you join a world, and nothing more unless another Radiance
    player is there.
  - Safer on game updates: more of what the mod uses is read from the game at start; if a menu
    piece changes in an update, the menu draws its own instead.
  Reported bugs fixed:
  - Empty chat messages for players without the mod (Neocybermario).
1.0.0
  - First release.

Made by GreeneyeDS with the help of AI. MIT license (see LICENSE).
Source: https://github.com/Greeneye0/radiance-enshrouded
