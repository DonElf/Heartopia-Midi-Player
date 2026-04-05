# Heartopia-Midi-Player
Turns midi device inputs or .mid files into in-game piano/instrument inputs.  
This program was primarily made with the 22 key layout, but also supports 15 keys (Double row). For that, see [this section](##How-to-use-15-Keys).

## How to use a Midi Instrument
Step 1: Connect the instrument to your computer.
Step 2: In the program, hit the "Live Input" button.
Step 3: Play the instrument, and it should play in game.

## How to play a .Mid file
Step 1: Open the app.  
<img width="592" height="201" alt="image" src="https://github.com/user-attachments/assets/21f3814c-235e-49c1-968d-a8d477dcb7ef" />  

Step 2: Hit browse and choose your file.  
<img width="1041" height="157" alt="image" src="https://github.com/user-attachments/assets/e1789158-c204-4b21-92ff-bcf4c74e07f8" />  

Step 3: Hit play, tab back into your game (where you're hopefully sitting at a piano), and wait 3 seconds.  


## How to use 15 Keys
To use the 15-key mode, check the "15 keys (Double Row)" checkbox before hitting play.


## I got an error!

### Failed to open MIDI file
This is probably the most common error, and means the program failed to open your file. Ensure: 
- The path to the file is correct,
- You have sufficient user permissions to read it.
- The path to the file isn't too long (Typically 260 characters).
- The file name doesn't contain any invalid characters.
- The file isn't restricted by another software.

### Invalid MIDI header
This likely indicated something wrong with the file, such as corruption or an incorrect file extention.
This occurs when the first 4 bytes of a Midi file are not "MThd".

### Invalid track header
This possibly indicates a corrupt file, or an issue with the MIDI parser.  
This occurs when the first 4 bytes of a track in a Midi file are not "MTrk".  
If you get this error, please message me.  

### No MIDI devices
Ensure the device is plugged in, turned on, and recognised by Windows.  

### Failed to open MIDI device
This occurs when the device is _known_, but midiInOpen fails. This is likely caused by one of the following:
- Device already in use. If another software is using your Midi device, close it and try again.
- Driver issues. In some cases, the device drivers may be misbehaving. Try reinstalling the drivers for your device.
- Out of memory. Windows may be unable to allocate the memory for the device. This can occur if you have many Midi devices connected, or many Midi softwares open. Disconnect other devices and/or close software and try again.
- Invalid device IDs, leaks from other software, etc. Try turning the device off and on again (Or unplugging it and plugging it back in), and/or restarting your computer.

## Build requirements
Requires VS & C++23 to build, since it uses #pragma comment(lib, ""), endian, & byteswap.
