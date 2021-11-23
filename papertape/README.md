# Paper tape uploader for kim1-blackpill
This is a utility to upload a .ptp file to the kim1-blackpill
emulator running on a KIM-UNO board. To build it, just do:
```
go build -o papertape main.go
```
To run it, supply the device name for the serial port that is
connected to the blackpill, and the filename to upload. For
example:
```
papertape /dev/ttyUSB0 wumpus.ptp
```
