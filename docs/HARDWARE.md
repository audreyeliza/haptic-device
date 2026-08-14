# Hardware — Build Reference

Physical build reference for the 1-DOF molecular force feedback haptic
device: fasteners, wiring, and CAD sources. For firmware/protocol details
see `firmware/`; for software build and running the simulation see the main
[README.md](../README.md) and [WSL_SETUP.md](WSL_SETUP.md).

## Hex Socket Button Head Screws

### Linear Rail / Carriage / Handle Assembly

| Screw | Connection |
| :---- | :---- |
| M3 x 16mm | Linear rail → Extrusion |
| M3 x 16mm | Carriage plate → Carriage |
| M3 x 12mm | Carriage plate → Thimble |
| M3 x 8mm | Handle → Carriage plate |

### Motor / Encoder Assembly

| Screw | Connection |
| :---- | :---- |
| M3 x 16mm | Motor mount → Extrusion |
| M3 x 6mm | Arduino mount → Motor mount |
| M2.5 x 12mm | Motor mount → Encoder |
| M2.5 x 8mm | Motor plate → Motor |
| M3 x 16mm | Capstan drum → Motor plate |

### Pulley Assembly

| Screw | Connection |
| :---- | :---- |
| M3 x 16mm | Pulley mount → Extrusion |
| M8 x 20mm * | Idler pulley → Pulley mount |

\* Hex Socket Head Cap Screw (not button head).

## Wiring

### Power

Bench power supply → 18 AWG wire → TB_PWR screw terminals (GND and VCC,
matched).

### Data / Programming

Arduino → Computer: USB-C to USB-C.

### Motor

Motor phase wires → Dupont wires → TB_M1 screw terminals.

### Encoder → SimpleFOC Shield v2.0.4

Connection type: JST SH 1.0mm 6-pin (SPI) cable, soldered to Dupont male
pins.

| Wire Color | Signal | Shield Pin |
| :---- | :---- | :---- |
| Black | GND | GND |
| Red | 5V | 5V |
| White | MISO | D12 |
| Yellow | MOSI | D11 |
| Orange | CLK | D13 |
| Green | CSn | D10 |

## CAD Models & References

### Onshape CAD

**Current version:** [Onshape link](https://cad.onshape.com/documents/a2de94f36928897e027b7937/w/de73ef18a2adb99bc6586bcc/e/ae1d9029cf5449f252d1af73?renderMode=0&uiState=6a69220317de83a549d0e5f1)

**Old versions:** [Onshape link](https://cad.onshape.com/documents/0f54a2c9e1ec5e9969cb91f3/w/544f7c77e34320733ff26538/e/8298af7611edbd9ac6a9e3f9?renderMode=0&uiState=6a6922ee8755df138229e3f5)

### 4040 T-Nuts (M3)

[Printables — 4040 T-Nuts M3](https://www.printables.com/model/698409-4040-t-nuts-m3/comments)

## Demo Videos

[YouTube playlist](https://www.youtube.com/playlist?list=PLWunosoP1iG4)
