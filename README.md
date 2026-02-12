# Heartopia-Midi-Player
Turns midi device inputs or .mid files into in-game piano/instrument inputs.  
This program was primarily made with the 22 key layout, but also supports 15 keys (Double row). For that, see [this section](##How-to-use-15-Keys).

## How to use a Midi Instrument
1. Connect the instrument to your computer.
2. Run the program.  
👍

## How to play a .Mid file
To play a .Mid file, the simplest solution I'd suggest is to right-click the file, select "Open with" -> "Choose another app".  
<img width="741" height="380" alt="image" src="https://github.com/user-attachments/assets/54d41c16-518e-4217-8e64-3d75f078f266" />  

From here, scroll down and select "Choose an app on your PC", and navigate to the program. It should open, and after a 3 second delay play the song.  
<img width="423" height="569" alt="image" src="https://github.com/user-attachments/assets/94792f4a-1f20-41f8-8cd5-588b53859621" />  
<img width="632" height="112" alt="image" src="https://github.com/user-attachments/assets/0557a1c1-2cbd-471b-9602-b6a4ee4ab9c3" />  

From now on, you should also see the program as an option in the "Open with" menu for .Mid files.  
<img width="605" height="318" alt="image" src="https://github.com/user-attachments/assets/0c834ee7-6c71-45e5-a858-4afc0c40c5bd" />  

<br>
<br>

Alternatively, you can use the command line by providing the file as a paramater.  
<img width="834" height="202" alt="image" src="https://github.com/user-attachments/assets/20abb529-57bd-41e4-a31f-88966f2540f7" />

## How to use 15 Keys
To use the 15-key mode, run the program with `--whites` as one of the paramaters. The order does not matter.  
<img width="816" height="226" alt="image" src="https://github.com/user-attachments/assets/6fff1555-85e7-493d-8b4f-f8f62a288dee" />

<br>
<br>

Alternatively, create a shortcut to the program and add `--whites` to the target  
<img width="635" height="476" alt="image" src="https://github.com/user-attachments/assets/353f618c-b94e-4520-a107-1c18726a1be0" />  
<img width="702" height="331" alt="image" src="https://github.com/user-attachments/assets/1d2796de-e400-417f-9e9b-ab9893783c81" />


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
