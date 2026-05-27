#include <iostream>
#include <array>
#include <vector>
#include <string_view>
#include <openxr/openxr.h>
#include "RtMidi.h"

// Class to hold the drumkit information.
class simmonsTitan50B_EX{
    private:
        // Create an array of 128 elements covering all possible MIDI notes 0-127
        // Initialize all to "Unknown" or an empty string by default
        static constexpr std::array<std::string_view, 128> drumMap = []() {
            std::array<std::string_view, 128> tempDrumMap{};

            for (int i = 0; i < 128; ++i) {
                tempDrumMap[i] = "Unknown";
            }

            // Map MIDI note numbers to specific drum pads on the Simmons Titan 50 B-EX
            tempDrumMap[36] = "Kick Drum";                  // C2
            tempDrumMap[38] = "Snare Drum - Center";        // D2
            tempDrumMap[40] = "Snare Drum - Rim";           // E2
            tempDrumMap[41] = "Tom 4";                      // F2
            tempDrumMap[42] = "Hi-Hat - Closed";            // F#2
            tempDrumMap[43] = "Tom 3";                      // G2
            tempDrumMap[44] = "Hi-Hat - Pedal";             // G#2
            tempDrumMap[45] = "Tom 2";                      // A2
            tempDrumMap[46] = "Hi-Hat - Open";              // A#2
            tempDrumMap[48] = "Tom 1";                      // C3
            tempDrumMap[49] = "Crash";                      // C#3
            tempDrumMap[51] = "Ride";                       // D#3
            tempDrumMap[57] = "Crash 2";                    // A3
            tempDrumMap[85] = "Hi-Hat - Splash";            // C#6
            tempDrumMap[86] = "Hi-Hat - Semi-Open";         // D6

            return tempDrumMap;            
        }();

    public: 
        simmonsTitan50B_EX() = default;

        // Function to get the drum pad name based on the MIDI note number.
        std::string_view getDrumPad(int midiNote) const {
        
            // Return an error message for out-of-range MIDI notes
            if (midiNote < 0 || midiNote > 127) {
                return "Invalid MIDI Note";
            }

            // Return the drum pad name for the given MIDI note number
            return drumMap[midiNote];
        }
};


// Function to trigger RtMidi's callback mechanism. It will be called whenever a MIDI message is received.
void myMidiCallback(double deltaTime, std::vector<unsigned char> *message, void *userData) {

    // If message is empty, we can ignore it. 
    // This can happen if the MIDI device sends a message with no data.
    if (message->empty()) return;

    // A standard MIDI note message has 3 bytes:
    // Byte 0: Status (Is this a Note On or Note Off message?)
    // Byte 1: Note (Which specific drum pad was hit?)
    // Byte 2: Velocity (How hard did you hit it? 1-127)
    int status      = (int)message->at(0);
    int note        = (int)message->at(1);
    int velocity    = (int)message->at(2);

    // If status is 0x90 i.e. "Note On" and velocity is greater than 0, we know it's a true hit.
    if ((status & 0xF0) == 0x90 && velocity > 0) {

        // Cast the userData pointer back to our simmonsTitan50B_EX class to access the drum map.
        simmonsTitan50B_EX* drumKit = static_cast<simmonsTitan50B_EX*>(userData);

        std::cout << "Drum Hit! " << drumKit->getDrumPad(note)
                  << " | Velocity: " << velocity << std::endl;
    }
}


int main() {

    // Initialize RtMidiIn for MIDI input
    // Create a null pointer to the RtMidiIn class object to handle MIDI input.
    RtMidiIn *midiIn = nullptr;

    try {
        // Create an RtMidiIn object to handle MIDI input.
        // This will automatically initialize the MIDI system.
        midiIn = new RtMidiIn();
    } catch (RtMidiError &error) {
        // If an error occurs during initialization, print the error message and exit.
        error.printMessage();
        return -1;
    }

    // Scan USB Ports for the MIDI devices and get the count of available MIDI input ports.
    unsigned int nPorts = midiIn->getPortCount();

    // If no MIDI input ports are found, print a message and exit.
    if (nPorts == 0) {
        std::cout << "No MIDI devices found! Is the kit plugged in and on?" << std::endl;
        delete midiIn;
        return 0;
    }

    // If MIDI input ports are found, print the count and list their names.
    std::cout << "Available MIDI Ports:" << std::endl;
    for (unsigned int i = 0; i < nPorts; i++) {
        std::cout << "  Port " << i << ": " << midiIn->getPortName(i) << std::endl;
    }

    // Open the first available MIDI input port (port 0).
    // Can change this later to open a specific port if needed.
    unsigned int portToOpen = 0;        // We default to Port 0. 
    std::cout << "\nOpening Port " << portToOpen << "..." << std::endl;
    midiIn->openPort(portToOpen);

     // Create an instance of the Simmons Titan 50B EX drum kit.
    simmonsTitan50B_EX connectedDrumKit;

    // Set the callback function to handle incoming MIDI messages.
    // Pass a pointer to the connectedDrumKit instance as user data to use it inside the callback.
    midiIn->setCallback(&myMidiCallback, &connectedDrumKit);

    // Ignore MIDI clock and system messages.
    midiIn->ignoreTypes(true, true, true);

    // Inform the user that the application is now listening for MIDI messages.
    std::cout << "\nListening for drum hits... Press Enter to quit.\n";

    // Wait for the user to press Enter before exiting.
    // This keeps the application running and able to receive MIDI messages.
    std::cin.get();

    // Clean up and exit. The destructor of RtMidiIn will automatically close the MIDI port.
    delete midiIn;

    // Return 0 to indicate successful execution.
    return 0;
}