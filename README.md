# ESP32 TWAI Web Analyzer

A simple ESP32-based TWAI (CAN) web analyzer with a browser UI for frame transmission, live monitoring, and runtime bus configuration.

## Overview

This project turns the ESP32 into a lightweight TWAI/CAN analysis and interaction tool accessible through a web browser.  
It creates a Wi-Fi access point, hosts a local web interface, and exposes HTTP endpoints that allow the user to:

- send TWAI/CAN frames from the browser;
- change bus configuration at runtime;
- monitor received frames;
- inspect the current interface status.

The software is organized in a modular way, separating TWAI handling, Wi-Fi setup, and HTTP server responsibilities.

## Features

- ESP32 TWAI driver integration
- Embedded web interface served directly by the device
- Runtime CAN/TWAI configuration from the browser
- Frame transmission through HTTP API
- Live RX polling for received frames
- Status endpoint with current mode, baud rate, filter, and state
- Software-side RX ring buffer
- Support for:
  - normal mode
  - listen-only mode
  - no-ack mode

## Project Structure

```text
.
├── main/
│   ├── main.c
│   ├── twai_app.c
│   ├── twai_app.h
│   ├── wifi_app.c
│   ├── wifi_app.h
│   ├── http_server.c
│   └── http_server.h
```

## Architecture

The application is divided into three main modules:

### `twai_app`
Responsible for:
- initializing and controlling the TWAI driver;
- applying TWAI configuration;
- queuing transmission requests;
- collecting received frames;
- exposing status information to upper layers.

### `wifi_app`
Responsible for:
- initializing the Wi-Fi stack;
- configuring the ESP32 as a SoftAP;
- starting the network services required by the application.

### `http_server`
Responsible for:
- serving the frontend files;
- handling REST-style endpoints;
- bridging browser requests to the TWAI application layer.

## Web UI

### Transmission UI
<img width="1406" height="1053" alt="tx_ui" src="https://github.com/user-attachments/assets/a5457e1a-8cc6-4f83-a90e-7d1bbdd116d8" />

### Reception UI
<img width="1188" height="499" alt="rx_ui" src="https://github.com/user-attachments/assets/c4be75dd-b062-4b3a-b6c3-25be613d7c72" />

## HTTP Endpoints

### `GET /`
Serves the main web interface.

### `GET /styles.css`
Serves the stylesheet.

### `GET /script.js`
Serves the frontend JavaScript file.

### `POST /api/can/send`
Sends a TWAI/CAN frame.

Example payload:

```json
{
  "id": 256,
  "format": "standard",
  "type": "data",
  "dlc": 8,
  "data": [1, 2, 3, 4, 5, 6, 7, 8]
}
```

### `POST /api/can/config`
Applies a new TWAI configuration.

Example payload:

```json
{
  "baudrate": 500000,
  "mode": "normal",
  "filter": null,
  "mask": null
}
```

### `GET /api/can/rx`
Returns received frames and the current CAN state.

### `GET /api/can/status`
Returns the current TWAI status and active configuration.

## Supported Modes

- `normal`
- `listen-only`
- `no-ack`

## Default Configuration

The application starts with:

- baud rate: `500000`
- mode: `normal`
- software filter: disabled

## Getting Started

### Requirements

- ESP32
- ESP-IDF
- TWAI-compatible transceiver connected to the configured GPIOs
- A browser-enabled device to access the UI

### Build and Flash

```bash
idf.py build
idf.py flash
idf.py monitor
```

## Default Access Point

By default, the ESP32 starts a Wi-Fi access point with the following settings:

- SSID: `ESP32_AP`
- Password: `password`
- IP address: `192.168.0.1`

After connecting to the access point, open the device IP in your browser.

## Use Cases

- basic CAN/TWAI bus inspection
- embedded communication testing
- quick manual frame injection
- educational demonstrations for CAN/TWAI concepts
- validation of IDs, payloads, and runtime configuration changes

## Notes

- The RX filtering shown in the application is handled in software.
- The project is designed to stay simple and easy to extend.
- This repository can serve as a base for more advanced embedded diagnostics tools.

## Future Improvements

- persistent configuration storage
- frame logging export
- timestamp formatting improvements
- bus load and error statistics on the UI
- filter presets and advanced mask helpers
