/* ------------------------------------
 |
 |   A script to turn Midi file & device inputs into inputs for Heartopia.
 |   By Don_Elf
 |   https://github.com/DonElf/Heartopia-Midi-Player
 |
 |   ©2025 Don_Elf
 |   Some Rights Reserved.
 |
 |
 |   Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
 |   http://www.apache.org/licenses/LICENSE-2.0
 |
 |   Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 |   See the License for the specific language governing permissions and limitations under the License.
 |
------------------------------------ */

#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <stdexcept>
#include <string>
#include <optional>
#include <cstdint>

// Dedicated exception for MIDI file errors
class MidiFileException : public std::runtime_error { using runtime_error::runtime_error; };

// Dedicated exception for MIDI device errors
class MidiDeviceException : public std::runtime_error { using runtime_error::runtime_error; };


class KeyboardEmitter {
private: // Defaults, I know. Explicit though :pleading:

	// Turn from a vk (int) to a scancode (WORD).
	// Allows mapping text characters to keyboard buttons, basically.
	WORD GetScanCode(int vKey) const {
		// These characters misbehave with MapVirtualKey, so do them manually.
		// A full generic list, although we'll only need a couple. Remove them if you want IG. Saves a couple CPU instructions?
		static const std::unordered_map<int, WORD> manual{
			{VK_OEM_COMMA, 0x33}, {VK_OEM_PERIOD, 0x34},
			{VK_OEM_1, 0x27}, {VK_OEM_2, 0x35},
			{VK_OEM_3, 0x29}, {VK_OEM_4, 0x1A},
			{VK_OEM_5, 0x2B}, {VK_OEM_6, 0x1B},
			{VK_OEM_7, 0x28}, {VK_OEM_MINUS, 0x0C},
			{VK_OEM_PLUS, 0x0D},
			{VK_SPACE, 0x39}, {VK_RETURN, 0x1C},
			{VK_BACK, 0x0E}, {VK_TAB, 0x0F},
			{VK_ESCAPE, 0x01}
		};

		// If the manual list contains 
		if (auto it = manual.find(vKey); it != manual.end())
			return it->second;

		// And, convert to scancode
		return (WORD)MapVirtualKey(vKey, MAPVK_VK_TO_VSC);
	}

	// Use a couple range checks, since it's pretty fast
	inline bool IsExtended(int vKey) const {
		return (vKey >= VK_PRIOR && vKey <= VK_DOWN) || // Page Up/Down, End, Home, Arrows
			(vKey >= VK_INSERT && vKey <= VK_DELETE) || // Insert, Delete
			(vKey == VK_LWIN || vKey == VK_RWIN) ||     // Windows keys
			(vKey == VK_APPS) ||                        // Menu key
			(vKey == VK_RCONTROL) ||                    // Right Ctrl
			(vKey == VK_RMENU);                         // Right Alt
	}

public:
	void SendKey(int vKey, bool pressed) const {
		// First, create an INPUT (Windows struct :P) using our input 
		INPUT input{};
		input.type = INPUT_KEYBOARD;
		input.ki.wVk = 0;
		input.ki.wScan = GetScanCode(vKey); // Scancode instead of vk, since vk is for typing & scancode is for input.
		input.ki.dwFlags = KEYEVENTF_SCANCODE;

		// If it's an extended range key, enable extended. Crazy, I know.
		if (IsExtended(vKey))
			input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;

		// If it's being unpressed, flip this flag.
		if (!pressed)
			input.ki.dwFlags |= KEYEVENTF_KEYUP;

		// And finally, send the input. This emulated a keyboard press, basically.
		SendInput(1, &input, sizeof(INPUT));
	}
};


class MidiMapper {
private:
	// Here in chars & VKs to allow easy editing.

	// Type doesn't allow in-class definition or whatever. Sure.
	// Plus, avoids initialising into memory when unused, I think? Unless it loads anyway. I dunno.
	static std::unordered_map<int, int> fullMap() {
		return {
			{48, VK_OEM_COMMA},{49, 'L'},{50, VK_OEM_PERIOD},{51, VK_OEM_1},
			{52, VK_OEM_2},{53, 'O'},{54, '0'},{55, 'P'},{56, VK_OEM_MINUS},
			{57, VK_OEM_4},{58, VK_OEM_PLUS},{59, VK_OEM_6},
			{60,'Z'},{61,'S'},{62,'X'},{63,'D'},{64,'C'},
			{65,'V'},{66,'G'},{67,'B'},{68,'H'},{69,'N'},
			{70,'J'},{71,'M'},{72,'Q'},{73,'2'},{74,'W'},
			{75,'3'},{76,'E'},{77,'R'},{78,'5'},{79,'T'},
			{80,'6'},{81,'Y'},{82,'7'},{83,'U'},{84,'I'}
		};
	}

	// ^ Ditto
	static std::unordered_map<int, int> whitesMap() {
		return {
			{60,'A'},{62,'S'},{64,'D'},{65,'F'},{67,'G'},
			{69,'H'},{71,'J'},{72,'Q'},{74,'W'},{76,'E'},
			{77,'R'},{79,'T'},{81,'Y'},{83,'U'},{84,'I'}
		};
	}

	// The map in question.
	std::unordered_map<int, int> map;

public:
	// Constructor :yippee:
	explicit MidiMapper(bool whitesOnly) {
		if (whitesOnly)
			map = whitesMap();
		else
			map = fullMap();
	}

	std::optional<int> MapNote(int note) const {
		// Find an iterator to the note in the map, if it exists. Then return the corresponding key.
		// Also, inverted for the in-if init statement. Thanks for reminding me, linter.
		if (auto it = map.find(note); it != map.end())
			return it->second;

		return std::nullopt;
	}
};

// Struct to store midi events. Self-descriptive, really.
struct MidiEvent {
	uint64_t timeMs; // I LOVE UNSIGNED DATA TYPES. I LOVE CSTDINT. I LOVE ALL UINT##_T TYPES. I HATE UNPACKING OVERHEAD. darn.
	int note;
	bool noteOn;
};

class MidiFileParser {
private:
	// Intermediate event stored with tick time (before tempo conversion)
	struct RawEvent {
		uint32_t tick;
		int note;
		bool noteOn;
	};

	// A tempo change at a specific tick position
	struct TempoChange {
		uint32_t tick;
		uint32_t tempo; // microseconds per quarter note
	};

	// Convert an absolute tick position to microseconds using the global tempo map.
	// This ensures all tracks share the same tempo changes, fixing multi-track timing.
	static uint64_t TickToUs(uint32_t tick, const std::vector<TempoChange>& tempoMap, uint16_t tpqn) {
		uint64_t us = 0;
		uint32_t lastTick = 0;
		uint32_t currentTempo = 500000; // default 120 BPM

		for (const auto& tc : tempoMap) {
			if (tc.tick >= tick) break;
			us += (uint64_t)(tc.tick - lastTick) * currentTempo / tpqn;
			lastTick = tc.tick;
			currentTempo = tc.tempo;
		}

		us += (uint64_t)(tick - lastTick) * currentTempo / tpqn;
		return us;
	}

public:
	static std::vector<MidiEvent> Parse(const std::string& path) {
		// Step 1: Open the file.
		std::ifstream file(path, std::ios::binary);

		// File failed to open.
		if (!file)
			throw MidiFileException("Failed to open MIDI file");

		// Invalid file header- not midi.
		if (ReadString(file, 4) != "MThd")
			throw MidiFileException("Invalid MIDI header");

		uint32_t headerLength = Read32(file);
		std::streampos headerStart = file.tellg();
		Read16(file); // format
		uint16_t tracks = Read16(file);
		uint16_t tpqn = Read16(file); // Ticks per quarter note

		// Skip any extra header bytes
		if (headerLength > 6) {
			file.seekg(headerStart + static_cast<std::streamoff>(headerLength));
		}

		std::vector<RawEvent> rawEvents;
		std::vector<TempoChange> tempoMap;

		for (int tr = 0; tr < tracks; ++tr) {
			if (ReadString(file, 4) != "MTrk")
				throw MidiFileException("Invalid track header");

			auto trackLength = Read32(file);
			auto trackEnd = file.tellg() + (std::streamoff)trackLength;

			uint32_t tick = 0;
			uint8_t lastStatus = 0;

			while (file.tellg() < trackEnd) {
				uint32_t delta = ReadVar(file);
				tick += delta;

				uint8_t status = 0;
				file.read((char*)&status, 1);

				if (status < 0x80) {
					file.seekg(-1, std::ios::cur);
					status = lastStatus;
				} else {
					lastStatus = status;
				}

				uint8_t type = status & 0xF0;

				if (type == 0x90 || type == 0x80) {
					uint8_t note = 0;
					uint8_t vel = 0;
					file.read((char*)&note, 1);
					file.read((char*)&vel, 1);

					rawEvents.push_back({tick, note, type == 0x90 && vel > 0});
				} else if (status == 0xFF) {
					uint8_t metaType = 0;
					file.read((char*)&metaType, 1);
					uint32_t len = ReadVar(file);

					if (metaType == 0x51 && len == 3) {
						uint8_t buf[3];
						file.read((char*)buf, 3);
						uint32_t newTempo = (buf[0] << 16) | (buf[1] << 8) | buf[2];
						tempoMap.push_back({tick, newTempo});
					} else {
						file.seekg(len, std::ios::cur);
					}
				} else {
					SkipEvent(file, status);
				}
			}

			file.seekg(trackEnd);
		}

		// Sort tempo map by tick position
		std::sort(tempoMap.begin(), tempoMap.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });

		// Convert tick-based events to real-time events using the global tempo map
		std::vector<MidiEvent> events;
		events.reserve(rawEvents.size());

		for (const auto& raw : rawEvents) {
			events.push_back({TickToUs(raw.tick, tempoMap, tpqn) / 1000, raw.note, raw.noteOn});
		}

		// Sort the events by time.
		std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.timeMs < b.timeMs; });

		return events;
	}

private:
	static inline uint32_t ReadVar(std::ifstream& f) {
		uint32_t value = 0;
		uint8_t c = 0;

		// OKAY, so
		// If the first bit is 1, more data follows. Otherwise, this is the end. 
		// It's also only up to 4 bytes in length, so we add a hard limit there, too.
		// If this is data, shift it & add the 7 right bits.
		// (value = value * 128 + next 7 bits)
		for (int i = 0; i < 4; ++i) {  // MIDI VLQ max 4 bytes
			f.read(reinterpret_cast<char*>(&c), 1);
			value = (value << 7) | (c & 0x7F);

			if (!(c & 0x80)) // End!
				return value;
		}

		// Bruh
		return value;
	}

	static inline uint32_t Read32(std::ifstream& f) {
		// Read 4 bytes in big-endian order (MIDI standard)
		uint8_t b[4];
		f.read(reinterpret_cast<char*>(b), 4);
		return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
	}


	static inline uint16_t Read16(std::ifstream& f) {
		// Read 2 bytes in big-endian order (MIDI standard)
		uint8_t b[2];
		f.read(reinterpret_cast<char*>(b), 2);
		return (uint16_t(b[0]) << 8) | uint16_t(b[1]);
	}

	static inline std::string ReadString(std::ifstream& f, size_t n) {
		// Erm... Pretty straightforward.
		// String. Read. Return.
		std::string s(n, ' ');
		f.read(s.data(), n);
		return s;
	}

	static void SkipEvent(std::ifstream& f, uint8_t status) {
		/*
		Status contains the event type. One of the following:
		- 0x80 Note Off
		- 0x90 Note On
		- 0xA0 Polyphonic key pressure
		- 0xB0 Control Change
		- 0xC0 Program Change
		- 0xD0 Channel pressure
		- 0xE0 Pitch bend
		*/

		// Get top 4 bits of the status.
		uint8_t type = status & 0xF0;

		// Program Change or Channel pressure. Skip 1.
		if (type == 0xC0 || type == 0xD0)
			f.seekg(1, std::ios::cur);

		// Polyphonic key pressure, Control Change, or Pitch bend. Skip 2.
		else if (type == 0xA0 || type == 0xB0 || type == 0xE0)
			f.seekg(2, std::ios::cur);

		// Meta event
		else if (status == 0xFF) {
			uint8_t metaType = 0;
			f.read(reinterpret_cast<char*>(&metaType), 1);
			f.seekg(ReadVar(f), std::ios::cur);
		}

		// System something or-rather. Check the length, then skip the length.
		else if ( status == 0xF0 || status == 0xF7)
			f.seekg(ReadVar(f), std::ios::cur);
	}
};


class MidiLiveInput {
private:
	HMIDIIN handle{}; // Every Windows handle makes me want to cry.
	MidiMapper& mapper;
	KeyboardEmitter& emitter;
	std::unordered_set<int> pressed;
	std::mutex mutex;

	static void CALLBACK Callback(HMIDIIN, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR) {
		// If it's not data, just return.
		// MIM is midi callback messages, btw. Just in case you care. :pleading:
		if (msg != MIM_DATA) return;

		// Should I do reinterpret(static<void>(instance)) to get the linter to shut up? Do I care? Unsafe my ass.
		auto* self = reinterpret_cast<MidiLiveInput*>(instance);

		uint8_t status = param1 & 0xFF;
		uint8_t note = (param1 >> 8) & 0xFF;
		uint8_t vel = (param1 >> 16) & 0xFF;

		// Get the mapped key.
		auto mapped = self->mapper.MapNote(note);

		// It's optional.
		if (!mapped.has_value())
			return;

		// Mutex, to avoid multithreading issues or smth. Jthread my beloved.
		std::scoped_lock lock(self->mutex);

		if ((status & 0xF0) == 0x90 && vel > 0) { // Note on
			if (self->pressed.contains(*mapped)) // If it's already pressed, return.
				return;

			// Add it to the list of pressed keys, and press it.
			self->pressed.insert(*mapped);
			self->emitter.SendKey(*mapped, true);

		} else if (((status & 0xF0) == 0x80) || ((status & 0xF0) == 0x90 && vel == 0)) { // Note off.
			if (!self->pressed.contains(*mapped)) // If it's not pressed, return.
				return;

			// Remove it from the list of pressed keys, and unpress it.
			self->pressed.erase(*mapped);
			self->emitter.SendKey(*mapped, false);
		}
	}

public:
	MidiLiveInput(MidiMapper& mapper, KeyboardEmitter& emitter) : mapper(mapper), emitter(emitter) {}

	void Run() {
		// If there are no MIDI devices attached, we can't get midi input. Crazy. Crazy? I was-
		if (midiInGetNumDevs() == 0)
			throw MidiDeviceException("No MIDI devices");

		// Something up with the Midi device. I don't really care what. Not my problem.
		if (midiInOpen(&handle, 0, (DWORD_PTR)&Callback, (DWORD_PTR)this, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
			throw MidiDeviceException("Failed to open MIDI device");

		// GO GO GO
		// Start listening to midi input.
		midiInStart(handle);

		std::cout << "Listening... Press Enter to quit.\n";
		std::cin.get(); // Wait for enter

		// Stop listening to midi input.
		midiInStop(handle);
		midiInClose(handle);
	}
};

// Can we put sunglasses on functions? I wish we could.
int main(int argc, char** argv) {
	try { // Good error handling is for dorks. Everything in a try catch(e).
		bool whitesOnly = false;
		std::string file;

		// If there are arguments
		for (int i = 1; i < argc; i++) {
			std::string arg = argv[i]; // Only one get. Something something multiple gets in the same contiguous memory are quick (I don't care). I had to google how to spell contiguous btw.

			if (arg == "--whites") 
				whitesOnly = true; // Okay, I know this LOOKS bad-
			else file = arg;
		}

		MidiMapper mapper(whitesOnly);
		KeyboardEmitter emitter;

		if (file.empty()) {
			// If we weren't given a file to play, try to play from a midi device.
			MidiLiveInput live(mapper, emitter);
			live.Run();

			// We could put a return here and not need the else, but I think this is more readable. Don't you?
		} else {
			// Try and read the file.
			auto events = MidiFileParser::Parse(file);

			// Wait 3 seconds, to let you get ready.
			std::cout << "Playback in 3 seconds...\n";
			std::this_thread::sleep_for(std::chrono::seconds(3));

			// Start the clock! Ready! Set!
			auto start = std::chrono::steady_clock::now();

			// GO!
			for (auto const& e : events) {
				// Sleep until the event.
				std::this_thread::sleep_until(start + std::chrono::milliseconds(e.timeMs));

				// Get the corresponding input to the event. If it exists, play it.
				if (auto mapped = mapper.MapNote(e.note); mapped.has_value())
					emitter.SendKey(*mapped, e.noteOn);
			}
		}
	} catch (const std::exception& e) { // ZOINKS!
		std::cerr << "Error: " << e.what() << "\n";
		std::cout << "\n\nPress Enter to quit.\n";
		std::cin.get(); // Wait for enter
		return 1;
	}

	// :thumbsup:
	return 0;
}
