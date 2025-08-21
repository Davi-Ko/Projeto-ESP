# ESP8266 Interlock Project

This project implements an ESP8266 application that allows multiple ESP devices to communicate with each other using ESP-NOW. The devices can automatically identify their own MAC addresses and connect without manual intervention.

## Features

- Automatic MAC address identification for each ESP device.
- Communication between devices using ESP-NOW.
- Role management for different functionalities (e.g., button, relay, sensor).
- State management to track the status of each device.
- Interlock functionality to ensure safe operation of connected devices.

## Setup Instructions

1. **Hardware Requirements:**
   - ESP8266 modules (e.g., ESP-01).
   - Relay module (if using relay functionality).
   - Push buttons (if using button functionality).
   - Jumper wires and breadboard for connections.

2. **Software Requirements:**
   - Arduino IDE or compatible development environment.
   - Install the following libraries:
     - ESP8266WiFi
     - ESP-NOW
     - ArduinoJson
     - EEPROM

3. **Uploading the Code:**
   - Open the `ESP-01.ino` file in your Arduino IDE.
   - Connect your ESP8266 module to your computer.
   - Select the appropriate board and port in the Arduino IDE.
   - Upload the code to the ESP8266.

4. **Automatic MAC Address Identification:**
   - Upon startup, each ESP device will automatically retrieve its MAC address.
   - The devices will broadcast their presence to each other and establish connections without needing to manually input MAC addresses.

5. **Testing the Setup:**
   - Power on all ESP devices.
   - Monitor the Serial output to see the devices announcing themselves and establishing connections.
   - Test the functionality of buttons and relays as per your configuration.

## Troubleshooting

- Ensure that all devices are powered and within range of each other.
- Check the Serial Monitor for any error messages or connection issues.
- Verify that the correct libraries are installed and up to date.

## Contribution

Feel free to contribute to this project by submitting issues or pull requests. Your feedback and improvements are welcome!