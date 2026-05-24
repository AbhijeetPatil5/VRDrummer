#include <iostream>
#include <vector>
#include <openxr/openxr.h>
#include "RtMidi.h"

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
        std::cout << "Drum Hit! Pad Note: " << note 
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
    
    // Set the callback function to handle incoming MIDI messages.
    midiIn->setCallback(&myMidiCallback);

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