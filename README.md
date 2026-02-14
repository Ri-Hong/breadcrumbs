# 🍞 Breadcrumbs — Off-Grid Communication Network for Hikers

Breadcrumbs is a wireless emergency communication system designed for hikers and outdoor explorers who venture into areas with no cellular service or Wi-Fi coverage.

By dropping small ESP32 “crumb” nodes along a trail, users can create a relay network that forwards messages hop-by-hop back to a gateway device connected to the internet.

---

## 🚀 Inspiration

Getting lost or injured in remote areas can quickly become dangerous when there is no signal to call for help.

Breadcrumbs provides a lightweight, portable way to extend connectivity into the wilderness using low-cost microcontrollers, enabling hikers to send SOS messages and find their way back safely.

---

## 🧠 What It Does

- 📡 **Creates an off-grid relay network** using ESP32 nodes dropped along a hiking path  
- 🛰️ **Relays messages hop-by-hop** until reaching a gateway node with internet access  
- 🆘 Supports emergency communication like SOS alerts  
- 🔦 Includes guidance features such as:
  - detecting when a node is picked up
  - beeping/blinking the next node to drop
  - lighting a return path back to safety

---

## 🏗️ How It Works

1. The hiker carries multiple ESP32 “crumb” devices in a pouch.
2. As they walk deeper into the woods, they drop crumbs periodically.
3. Each crumb automatically connects to its nearest neighbor and forms a relay chain.
4. Messages are forwarded node-to-node using **ESP-NOW**, a low-power peer-to-peer Wi-Fi protocol.
5. When the message reaches the gateway node near connectivity, it is forwarded to the internet (e.g., Discord webhook, SMS, or server).

---

## 🔑 Key Features

### ✅ Multi-Hop Wireless Relaying
ESP32 nodes forward packets across multiple hops without requiring routers or infrastructure.

### 🧲 Pickup Detection with Hall Sensors
Each crumb can detect when it has been removed from the pouch using a Hall effect sensor + magnet system.

### 🔔 Smart Deployment Assistance
When one crumb is picked up, the next crumb can beep or blink to guide the user.

### 🆘 Emergency Messaging
Users can send SOS alerts that propagate through the breadcrumb trail back to safety.

### 🔦 Return-to-Exit Trail Mode (Optional)
Crumbs can blink sequentially to guide hikers back along the original path.

---

## 🛠️ Tech Stack

- **ESP32 DevKit v1**
- **ESP-NOW** (wireless packet relay protocol)
- Hall effect sensors + magnets (pickup detection)
- Gateway node with Wi-Fi hotspot/internet forwarding
- Optional Discord/Telegram webhook integration

---

## ⚡ Power

Each node is battery-powered for outdoor portability.

Recommended configurations:
- 4×AA battery pack into VIN  
- LiPo/Li-ion battery with voltage regulation  
- USB power bank for prototyping/demo

---

## 🌲 Use Cases

- Hiking in remote areas with no signal  
- Emergency SOS relay system  
- Backtracking and navigation assistance  
- Outdoor mesh sensor network extensions  

---

## 🌟 Future Improvements

- Upgrade from ESP-NOW to ESP-MESH or LoRa for longer range  
- Full phone-to-internet routing through mesh  
- GPS logging for breadcrumb mapping  
- Encrypted messaging and authentication  
- Automatic drop-spacing optimization using RSSI/link quality  

---

## 👥 Team

Built for **MakeUofT** as a hackathon project exploring off-grid communication, embedded networking, and wilderness safety.

---

## 📷 Demo

*(Add photos, wiring diagrams, and a short demo video link here)*

---
