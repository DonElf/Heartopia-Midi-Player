# Heartopia-Midi-Player
Turns midi device inputs or .mid files into in-game piano/instrument inputs

Add the path to a .mid file in the cl arguments, or use "open with" on a .mid file to play one. Otherwise, will default to midi devices.  
Use the --whites argument to use the smaller. whites-only layout the non-piano instruments use.  
And remember to have fun.

Requires VS & C++23 to build, since it uses #pragma comment(lib, ""), endian, & byteswap.
