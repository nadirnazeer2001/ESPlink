# ESPlink — ESP32 Wireless Joystick Controller

ESPlink is a low-latency wireless robot controller built on ESP-NOW and ESP32.
It features a joystick-driven teleoperation interface, OLED display menu system, and real-time value monitoring — ideal for UGVs, robots, RC vehicles, and custom robotic platforms.

# Features

 Dual-axis joystick input

 Ultra-fast ESP-NOW communication

 SSD1306 OLED UI menu system

 Trim adjust (X & Y axes)

# Switchable control modes:

Serial

PWM

CAN (will be added later)

 Live joystick output display

 Joystick auto-calibration + adaptive range learning

 Runs on FreeRTOS tasks

 Smooth filtering & dead-zone handling


# Hardware Requirements

ESP32 devkitc with ufl conncetor

ufl antenna

joystick modules

oled screen

esp32 devkitc normal for receiver

buck converter

tactile switches 

# Pairing with Receiver

update the mac address in the field 

uint8_t receiverMAC[] = {0x14, 0x08, 0x08, 0xA6, 0x8E, 0x68};

# Data Packet Format

Field	Range	Description

mode	0=Serial, 1=PWM, 2=CAN	Output mode

linear	0–100	Forward/back

angular	0–100	Left/right

Button mappings: UP / DOWN / LEFT / RIGHT / OK


# Required Libraries

Install via Arduino Library Manager:

U8g2lib

WiFi

esp_now

# Planned Add-Ons

 Receiver firmware for motor drivers

 Battery indicator

 Enclosure 3D model

 Config saving via NVS

 Multirobot pairing support

 # Demo video links 

 Direct communication with ROS2- https://drive.google.com/file/d/1GH9k6X3nOz-qFXIJS_svUUe_qiQqNwZ5/view?usp=sharing

Direct communication with the esc and servo -  
