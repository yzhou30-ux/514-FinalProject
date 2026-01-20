# FreshSense: Smart Food Tracking and Management System

This smart refrigerator labeling system integrates multiple individual sensor "tags" with an external magnetic display "hub." The tags automatically monitor and record the time and humidity of stored food items from the moment they are detached from the hub. This system offers a cost-effective solution for food management and improves storage tracking to reduce waste.

![general sketch](assets/Slide1%20-%20general%20sketch.png)

---

## TAG: Sensor

The TAG is a sensing device responsible for:
- Collecting environmental data (humidity, temperature, battery percentage, and time since detaching).
- Transmitting data to the HUB via BLE.
- Supporting contact charging for its battery.

### Components:
- **XIAO ESP32C**
- **HTU21D** (Sleep Humidity Sensor)
- **LIR2032 Coin Cell** (Rechargeable)

### What data does the TAG collect?
- Humidity & Temperature  
- Time since detaching  
- Battery Percentage  

---

## HUB: Display

The HUB is a display controller that:
- Receives data from the TAG.
- Displays information using motor-needle, LED, and OLED interfaces.
- Manages multiple TAG addresses and states.
- Switches active TAGs.
- Monitors and displays system battery percentage.

### Components:
- **XIAO ESP32C**  
- **28BYJ-48 Stepper Motor, Driver & Needle**  
- **LED**  
- **OLED Display** (128x64)  
- **Piezoelectric Button**  
- **7.4V 2S LiPo Battery**  
  - **TP5100**, **MP1584**, **LM2596**, **TP4056**

### What functions does the HUB serve?
- **Display**: Motor-needle and LED display.  
- **Other Interfaces**: OLED display, button.  
- **Contact Charging**: For sensors.  
- **Management**: Manage sensor addresses, detect if a TAG is present, and receive TAG data.  
- **Battery Monitoring**: Display system battery percentage.  

---

## System Communication Diagram
*(Based on BLE Communication)*

---

## System Workflow
*(Detailed workflow of TAG and HUB interactions)*