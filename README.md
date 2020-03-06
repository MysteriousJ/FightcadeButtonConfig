My local scene has been using fightcade to run side events for old games. Since fightcade's button config is horrible, I wrote a little program to help us speed it up. It works by directly changing the button mapping in a text editor.

# Setup for tournament organizers
* Launch ggpofba.exe. This will open the emulator (FB Alpha) for local play without the fightcade matchmaking interface.
* In FB Alpha, go to "Misc" in the menu and disable "Auto-save input mapping".
* In your fightcade folder, open the config folder, then games, and open the .ini file in a text editor (I recommend notepad++) for the game you're going to play.
* The .ini file has an inputs section. Change all the inputs you want to map to 0x4080. You can do this all at once in notepad++ by using block-select (hold down alt before making a selection).
* Launch this program. It simulates keys in response to button presses, but it doesn't use any keys that are shortcuts in FB Alpha, so you can leave it open while playing the game.

# Setup for players
* Plug in your controllers
* Switch to the window with the button config file.
* Place the cursor at the end of a line you want to change.
* Press the button or direction you want to map. The cursor will automatically move to the next line.
* When both players have mapped their buttons, save the file and switch to the FB Alpha window.
* Go to "Game" in the menu and chose "Load Game". Pick the game you want to play, even if it's already open. This will get FB Alpha to refresh what controllers and plugged in and reload the config file.