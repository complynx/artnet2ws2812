# ArtNet2WS2812 for ESP8266

An Art-Net controller for ESP8266, based on [esp-open-rtos](https://github.com/superhouse/esp-open-rtos).
It has several workmodes, it has automatic Wi-Fi detection and cycling through stored networks, and runtime setup features.

## build

Get [esp-open-rtos](https://github.com/superhouse/esp-open-rtos) and all the SDK needed, ensure it's working, using examples.
Ensure you've created esp-open-rtos' `include/private_ssid_config.h` that it will need.
run 
```
export RTOS_PATH="path/to/esp-open-rtos"
make flash EXTRA_CFLAGS="-DLED_NUMBER=10 -DART_NET_SHIFT=16 -DART_NET_UNIVERSE=1"
```
Where you can set up:
* LED_NUMBER — maximum number of leds in the chain _(this parameter can't be changed at the runtime, but you can run with less)_
* ART_NET_SHIFT — the beginning of payload dedicated for the controller in DMX packet, aka DMX Address
* ART_NET_UNIVERSE — the Universe number the controller sits in

## DMX workmodes

DMX payload is processed as
```
WORKMODE + WMPAYLOAD

WORKMODE := byte, see workmodes
WMPAYLOAD := number of bytes, depends on workmode
```

#### 0 — DMX to WS2812

Outputs DMX straight to the leds
```
WORKMODE + WMPAYLOAD

WORKMODE := 0x00
WMPAYLOAD := RGB + [RGB + [...]] — size of LED_NUMBER * 3 in bytes
RGB := RED + GREEN + BLUE
RED := byte color level
GREEN := byte color level
BLUE := byte color level
```

#### 1 — DMX chain

Shifts all the leds by 1 and pushes input into the first led
```
WORKMODE + WMPAYLOAD

WORKMODE := 0x01
WMPAYLOAD := RGB — size of 3 in bytes
RGB := RED + GREEN + BLUE
RED := byte color level
GREEN := byte color level
BLUE := byte color level
```

#### 2 — DMX chain reversed

Shifts all the leds by 1 and pushes input into the last led, using LED_NUMBER parameter
```
WORKMODE + WMPAYLOAD

WORKMODE := 0x02
WMPAYLOAD := RGB — size of 3 in bytes
RGB := RED + GREEN + BLUE
RED := byte color level
GREEN := byte color level
BLUE := byte color level
```

#### 3 — Rainbow

Starts a simple rainbow effect
Parameters:
* ID — unique identifier for current settings, to prevent restaring if the same settings were sent. Restart = resetting starting color. Other settings will be overwritten. If they are the same, it won't change anything.
* delay — delay between steps in milliseconds, actual delay will be quantized by portTICK_PERIOD_MS
* t_step — hue increment between time steps, 0-359 `LED(N).COLOR(T).hue = (LED(N).COLOR(T-1).hue + t_step) % 360`
* l_step — hue increment between adjascent leds, 0-359 `LED(N+1).COLOR(T).hue = (LED(N).COLOR(T).hue + l_step) % 360`
* start color — color of the first led at the beginning `LED(0).COLOR(0).RGB = START_COLOR`
* tint — base color for the rainbow `LED(N).COLOR(T).OUTPUT = LED(N).COLOR(T).& * (1 - tint_level) + TINT.& * tint_level`
* tint_level — level of tint color (see tint) denormalized to 0-255 scale
* tint type — RGB (<128) or HSV (>=128). If tint type is HSV, then tint equation is solved using HSV color space, else in RGB

```
WORKMODE + WMPAYLOAD

WORKMODE := 0x03
WMPAYLOAD := ID + DELAY + T_STEP + L_STEP + START_COLOR + TINT + TINT_LEVEL + TINT_TYPE
ID := unique byte
DELAY := big-endian representation of uint16 delay
T_STEP := big-endian representation of uint16 t_step
L_STEP := big-endian representation of uint16 l_step
START_COLOR := RED + GREEN + BLUE
TINT := RED + GREEN + BLUE
RED := byte color level
GREEN := byte color level
BLUE := byte color level
TINT_LEVEL := byte tint level
TINT_TYPE := byte tint type
```

## runtime setup

You can set up the controller by sending custom Art-Net commands to it.

### Custom Art-Net commands

These use the unused Art-Net opcodes

#### 0xf823 — set stations

Sets Wi-Fi stations.
Payload — bytearray of
```
STATIONS + ZERO

STATIONS := STATION + [STATION + [...]]
STATION := SSID + ZERO + PASS + ZERO
SSID := string of non-zero bytes
PASS := string of non-zero bytes, may be zero-length
ZERO := char '\0'
```

#### 0xf824 — set AP

Sets Wi-Fi Soft-AP.
Payload — bytearray of
```
ALWAYS_ON + SSID_TPL + ZERO + PASS + ZERO
ALWAYS_ON := byte 0x01 if enable AP always, or 0x00 instead
SSID_TPL := string of non-zero bytes where char '#' represents a hex symbol in ESP device ID
PASS := string of non-zero bytes, may be zero-length or not less then 8 bytes
ZERO := char '\0'
```
Final SSID is created by replacing right-to-left all the hashes '#' in the SSID_TPL by hexadecimal representation of ESP device ID padded with zeroes.

#### 0xf825 — set DMX

Sets DMX Universe and shift
Payload:
```
UNIVERSE + SHIFT
UNIVERSE := little-endian representation of uint16 Art-Net Universe number
SHIFT := little-endian representation of uint16 DMX Address
```

### Settings scripts

There are a couple of scripts to test and set up the controller at runtime. **Only on Windows for now, but easily moddable**

#### `testing/remote_setup.py`:

It can:
* set up several WiFi stations for the controller to cycle through and try connecting,
* set up new AP name and password, as well as toggle always-on flag
* change Art-Net universe and shift parameters
All the settings will be saved in onboard memory to be used after rebooting

#### `testing/send_artnet.py`:

Sends different Art-Net commands to test the setup
