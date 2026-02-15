# Bread — Pouch interface (WiFi AP + web)

Bread is the ESP32 in the pouch. It runs a **WiFi access point** and a **simple web server** so the hiker can send a message out (to the internet) using their phone — **no cellular or internet needed** on the phone.

## Flow

1. Hiker powers on Bread (in the pouch).
2. Bread creates WiFi network **"Breadcrumbs-Pouch"** (password: `trail123`).
3. Hiker connects their phone to that WiFi.
4. Hiker opens a browser and goes to **http://192.168.4.1**
5. They type a message and tap **Send**.
6. Bread sends the message via ESP-NOW to Crumb_A → B → C → D → gateway POSTs to API.

## Hardware

- **Bread** MAC: `E4:65:B8:83:56:30` (see `../MACs.md`)
- Bread sends to **Crumb_A** only. Crumb_A must be running the relay firmware (receive from Bread, forward to B).

## Upload

1. Open `bread.ino` in Arduino IDE (or PlatformIO).
2. Select your ESP32 board and port.
3. Upload.

## Optional: open network

In `bread.ino`, set `ap_password` to `""` to make the AP open (no password). Less secure but easier for demos.
