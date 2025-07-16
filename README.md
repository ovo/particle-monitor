# Air Quality Monitor

An air quality monitoring system using ESP32 + PMS5003 sensor with local web interface for realtime monitoring.

## Architecture

```
ESP32 + PMS5003 → Local Web Server (API + Frontend)
                      ↓
                 Realtime Display
```

## Configuration

Before deploying, you'll need to configure the following:

### ESP32 Configuration (`main/particle-monitor.c`)
```c
#define WIFI_SSID          "YOUR_WIFI_SSID"        // Your WiFi network name
#define WIFI_PASS          "YOUR_WIFI_PASSWORD"    // Your WiFi password
```

## Setup Instructions

### 1. Configure ESP32

1. Update WiFi credentials in `main/particle-monitor.c`:
   - Replace `YOUR_WIFI_SSID` with your WiFi network name
   - Replace `YOUR_WIFI_PASSWORD` with your WiFi password
2. Build and flash: `idf.py build flash`

### 2. Access Your Monitor

1. Connect to the same WiFi network as your ESP32
2. Find the ESP32's IP address from your router or monitor the serial output
3. Visit the ESP32's IP address in your browser: `http://192.168.x.x`

## API Endpoints

- `GET /` - Web interface (HTML frontend)
- `GET /api/data` - Get current air quality data (JSON)

## Hardware Requirements

- ESP32 development board
- PMS5003 particulate matter sensor
- Jumper wires

## Wiring (TX/RX pins can be changed depending on your board)

- PMS5003 VCC → ESP32 5V
- PMS5003 GND → ESP32 GND
- PMS5003 TX → ESP32 GPIO17 (RX)
- PMS5003 RX → ESP32 GPIO16 (TX)

## Air Quality Standards

- **Good**: PM2.5 ≤ 12 μg/m³
- **Moderate**: PM2.5 13-35 μg/m³
- **Unhealthy**: PM2.5 > 35 μg/m³
