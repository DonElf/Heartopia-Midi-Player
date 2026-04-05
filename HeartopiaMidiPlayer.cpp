/* ------------------------------------
 |
 |   A script to turn Midi file & device inputs into inputs for Heartopia.
 |   By Don_Elf
 |   https://github.com/DonElf/Heartopia-Midi-Player
 |
 |   ©2026 Don_Elf
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
		else if (status == 0xF0 || status == 0xF7)
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

	void Start(HWND statusHwnd) {
		// If there are no MIDI devices attached, we can't get midi input. Crazy. Crazy? I was-
		if (midiInGetNumDevs() == 0)
			throw MidiDeviceException("No MIDI devices");

		// Something up with the Midi device. I don't really care what. Not my problem.
		if (midiInOpen(&handle, 0, (DWORD_PTR)&Callback, (DWORD_PTR)this, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
			throw MidiDeviceException("Failed to open MIDI device");

		// GO GO GO
		// Start listening to midi input.
		midiInStart(handle);

		SetWindowTextA(statusHwnd, "Listening for MIDI input...");
	}

	void Stop(HWND statusHwnd) {
		// Stop listening to midi input.
		midiInStop(handle);
		midiInClose(handle);

		SetWindowTextA(statusHwnd, "Stopped.");

		// Clear the handle
		handle = {};
	}
};


// Control IDs. have to be ints rather than HMENU to appease the linter's pointer const requirements.
constexpr int ID_EDIT_FILE  = 101;
constexpr int ID_BTN_BROWSE = 102;
constexpr int ID_CHK_WHITES = 103;
constexpr int ID_CHK_LOOP   = 104;
constexpr int ID_BTN_PLAY   = 105;
constexpr int ID_BTN_LIVE   = 106;
constexpr int ID_BTN_STOP   = 107;
constexpr int ID_LBL_STATUS = 108;

struct AppState {
	std::string filePath;
	bool whitesOnly = false;
	bool loop = false;
	std::atomic<bool> playing = false;
	std::atomic<bool> liveMode = false;

	// Handles
	HWND handleEdit{};
	HWND handleChkWhites{};
	HWND handleChkLoop{};
	HWND handleBtnPlay{};
	HWND handleBtnLive{};
	HWND handleBtnStop{};
	HWND handleStatus{};

	// Live input session stuff
	std::unique_ptr<MidiMapper> liveMapper;
	std::unique_ptr<KeyboardEmitter> liveEmitter;
	std::unique_ptr<MidiLiveInput> liveInput;
};

// Global state pointer for WndProc
AppState* g_state = nullptr;
std::jthread playThread;

void Play(AppState& state) {
	// Put the playing and parsing inside a try/catch for "good enough" error handling
	try {
		// Update value
		state.whitesOnly = (IsDlgButtonChecked(GetParent(state.handleChkWhites), ID_CHK_WHITES) == BST_CHECKED);

		// Set up the map, emitter, and parse the midi file.
		// Parsing is probably one of the most likely parts to throw an error.
		MidiMapper mapper(state.whitesOnly);
		KeyboardEmitter emitter;
		auto events = MidiFileParser::Parse(state.filePath);

		// Wait 3 seconds.
		Sleep(3000);

		// Set text anticipatorily. That's not a word, I'm pretty sure. In anticipation.
		SetWindowTextA(state.handleStatus, "Playing...");

		// How often do you get to use a do while loop in programming? I find them pretty rare, all things considered...
		do {
			// Update
			state.loop = (IsDlgButtonChecked(GetParent(state.handleChkLoop), ID_CHK_LOOP) == BST_CHECKED);

			// Set the start before we play the song.
			// I forgot to move this into the do-while before and spent far too long figuring out the issue.
			auto start = std::chrono::steady_clock::now();

			// Iterate through and play each event
			for (auto const& e : events) {
				// Stop early if requested
				if (!state.playing) break;

				// Sleep intil the event
				std::this_thread::sleep_until(start + std::chrono::milliseconds(e.timeMs));

				// Get the corresponding input to the event. If it exists, play it.
				if (auto mapped = mapper.MapNote(e.note); mapped.has_value())
					emitter.SendKey(*mapped, e.noteOn);
			}
		} while (state.loop && state.playing);
	} catch (const std::exception& ex) { // ZOINKS!
		SetWindowTextA(state.handleStatus, ex.what());
	}

	// Re-enable the buttons.
	EnableWindow(state.handleBtnPlay, TRUE);
	EnableWindow(state.handleBtnLive, TRUE);
	EnableWindow(state.handleBtnStop, FALSE);

	// state.playing will be true if we finished, or false if we were stopped.
	if (state.playing) {
		state.playing = false;
		SetWindowTextA(state.handleStatus, "Done.");
	} else {
		SetWindowTextA(state.handleStatus, "Stopped.");
	}
}

void StartFilePlayback(AppState& state) {
	// Grab the file path from the edit box
	std::string buf(256, '\0');
	GetWindowTextA(state.handleEdit, buf.data(), 256);

	//resize the path to what's needed
	buf.resize(strnlen(buf.data(), buf.size()));

	state.filePath = buf;

	// Null check. Uh. Empty check, sorry. Yuh.
	if (state.filePath.empty()) {
		SetWindowTextA(state.handleStatus, "No file selected.");
		return;
	}

	// Playing. Read line below for more details.
	state.playing = true;

	// Set the buttons to active/inactive respectively.
	EnableWindow(state.handleBtnPlay, FALSE);
	EnableWindow(state.handleBtnLive, FALSE);
	EnableWindow(state.handleBtnStop, TRUE);

	// We set the text here, but we start the timer in the Play function inside the thread, so we still have the UI on the main thread.
	SetWindowTextA(state.handleStatus, "Playback in 3 seconds...");

	// Start the thread.
	playThread = std::jthread(Play, std::ref(state));
}

void StartLiveInput(AppState& state) {
	try {
		// Check whites only
		state.whitesOnly = (IsDlgButtonChecked(GetParent(state.handleChkWhites), ID_CHK_WHITES) == BST_CHECKED);

		state.liveMapper = std::make_unique<MidiMapper>(state.whitesOnly);
		state.liveEmitter = std::make_unique<KeyboardEmitter>();
		state.liveInput = std::make_unique<MidiLiveInput>(*state.liveMapper, *state.liveEmitter);

		state.liveInput->Start(state.handleStatus);

		state.liveMode = true;
		EnableWindow(state.handleBtnPlay, FALSE);
		EnableWindow(state.handleBtnLive, FALSE);
		EnableWindow(state.handleBtnStop, TRUE);
	} catch (const std::exception& ex) {
		SetWindowTextA(state.handleStatus, ex.what());
	}
}

void Stop(AppState& state) {
	if (state.playing) {
		state.playing = false; // Signal the playback thread to exit
	}

	if (state.liveMode) {
		// Clear and reset everything
		state.liveInput->Stop(state.handleStatus);
		state.liveInput.reset();
		state.liveEmitter.reset();
		state.liveMapper.reset();
		state.liveMode = false;

		// Re-enable the buttons.
		EnableWindow(state.handleBtnPlay, TRUE);
		EnableWindow(state.handleBtnLive, TRUE);
		EnableWindow(state.handleBtnStop, FALSE);
	}
}

// Create the UI
LRESULT CALLBACK Create(HWND hwnd, LPARAM lParam) {
	// Get the HInstance
	HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;

	// File selector
	CreateWindowW(L"STATIC", L"MIDI File:",
		WS_CHILD | WS_VISIBLE,
		10, 14, 70, 20, hwnd, nullptr, hInst, nullptr);

	g_state->handleEdit = CreateWindowW(L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
		85, 10, 380, 24, hwnd, (HMENU)ID_EDIT_FILE, hInst, nullptr);

	CreateWindowW(L"BUTTON", L"Browse...",
		WS_CHILD | WS_VISIBLE,
		475, 10, 80, 24, hwnd, (HMENU)ID_BTN_BROWSE, hInst, nullptr);


	// Checkboxes
	g_state->handleChkWhites = CreateWindowW(L"BUTTON", L"15 keys (Double Row)",
		WS_CHILD | WS_VISIBLE | BS_CHECKBOX,
		10, 45, 200, 24, hwnd, (HMENU)ID_CHK_WHITES, hInst, nullptr);

	g_state->handleChkLoop = CreateWindowW(L"BUTTON", L"Loop",
		WS_CHILD | WS_VISIBLE | BS_CHECKBOX,
		200, 45, 120, 24, hwnd, (HMENU)ID_CHK_LOOP, hInst, nullptr);


	// Buttons
	g_state->handleBtnPlay = CreateWindowW(L"BUTTON", L"Play File",
		WS_CHILD | WS_VISIBLE,
		10, 80, 90, 28, hwnd, (HMENU)ID_BTN_PLAY, hInst, nullptr);

	g_state->handleBtnLive = CreateWindowW(L"BUTTON", L"Live Input",
		WS_CHILD | WS_VISIBLE,
		110, 80, 90, 28, hwnd, (HMENU)ID_BTN_LIVE, hInst, nullptr);

	g_state->handleBtnStop = CreateWindowW(L"BUTTON", L"Stop",
		WS_CHILD | WS_VISIBLE,
		210, 80, 90, 28, hwnd, (HMENU)ID_BTN_STOP, hInst, nullptr);


	// Disable until playing
	EnableWindow(g_state->handleBtnStop, FALSE);


	// Status label
	g_state->handleStatus = CreateWindowW(L"STATIC", L"Ready.",
		WS_CHILD | WS_VISIBLE,
		10, 120, 540, 20, hwnd, (HMENU)ID_LBL_STATUS, hInst, nullptr);


	return 0;
}

LRESULT CALLBACK Command(WPARAM wParam, HWND hwnd) {
	switch (LOWORD(wParam)) {
		// Open file browser
		case ID_BTN_BROWSE: {
			char buf[256] = {}; // C-String... Sorry...

			OPENFILENAMEA ofn{};
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner = hwnd;
			ofn.lpstrFilter = "MIDI Files\0*.mid;*.midi\0All Files\0*.*\0";
			ofn.lpstrFile = buf;
			ofn.nMaxFile = sizeof(buf);
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

			if (GetOpenFileNameA(&ofn)) {
				SetWindowTextA(g_state->handleEdit, buf);
				g_state->filePath = buf;
			}

			break;
		}

		case ID_CHK_WHITES:
			// Toggle the checkbox state
			g_state->whitesOnly = !(IsDlgButtonChecked(hwnd, ID_CHK_WHITES) == BST_CHECKED);
			CheckDlgButton(hwnd, ID_CHK_WHITES, g_state->whitesOnly ? BST_CHECKED : BST_UNCHECKED);
			break;

		case ID_CHK_LOOP:
			// Toggle the checkbox state. Same code as above.
			g_state->loop = !(IsDlgButtonChecked(hwnd, ID_CHK_LOOP) == BST_CHECKED);
			CheckDlgButton(hwnd, ID_CHK_LOOP, g_state->loop ? BST_CHECKED : BST_UNCHECKED);
			break;

		case ID_BTN_PLAY:
			StartFilePlayback(*g_state);
			break;

		case ID_BTN_LIVE:
			StartLiveInput(*g_state);
			break;

		case ID_BTN_STOP:
			Stop(*g_state);
			break;

		default: // Linter.
			break;
	}

	return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	// When a button is pressed, we can just switch it.
	switch (msg) {
		case WM_CREATE:
			return Create(hwnd, lParam);

		case WM_COMMAND:
			return Command(wParam, hwnd);

		case WM_DESTROY:
			// Clean up if still running
			if (g_state->playing || g_state->liveMode)
				Stop(*g_state);
			PostQuitMessage(0);
			return 0;
			
		// Linter wants default. I dunno, man. I tried and it broke.
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
	// Tracks our variables for us...
	AppState state{};
	g_state = &state;

	// Register window class
	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); // Standard dialogue grey
	wc.lpszClassName = L"HeartopiaMidiPlayer";
	RegisterClassExW(&wc);

	// Create the window
	HWND hwnd = CreateWindowExW(
		0,
		L"HeartopiaMidiPlayer",
		L"Heartopia MIDI Player",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, // Fixed size
		CW_USEDEFAULT, CW_USEDEFAULT,
		580, 190,
		nullptr, nullptr,
		hInstance, nullptr
	);

	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);

	// Message loop only wakes up when something actually happens. We hate polling in this household.
	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}

// If input is main instead of WinMain, just use WinMain anyway.
int main() {
	WinMain(GetModuleHandle(nullptr), nullptr, nullptr, SW_SHOWDEFAULT);
}
