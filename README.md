# LoggingDecode
Decoder application for OpenInverter binary logs

# Summary
This application reads in the binary logs created using some experimental OpenInverter builds in conjunction with the SD card logging version of the ESP32 web interface.

Two types of output are available:
Motor data - this is generated on every run of the motor control loop.  It is the core data needed to analyse operation of the FOC algorithms.
Spot value data - This is output at a slower rate (every 32ms at 8.8kHz control loop rate) and contains every spot data value defined within the OpenInverter firmware.  It is intended more for general logging or analysing operation of the vehicle control algorithms.

# Use
Syntax : LoggingDecode [options] source dest

source - full source file name including extension.

dest - optional destination base file name (no extension).  If not supplied then the source name (including path but without extension) will be used.

Options:
  -h, --help     Displays help.
  -v, --version  Displays version information.
  -p             Generate PulseView file for motor data
  -c             Generate CSV file for motor data
  -s             Generate CSV file for spot value data
  -j             Generate JSON file
  -a             Generate All files (default)

# Output Files
<dest>.json           - A JSON format file containing all the inverter parameter definitions.
<dest>.sr             - PulseView native format file containing the motor data.
<dest>_motor_data.csv - A CSV format file containing the motor data (note - large file, approx 4.5 times the size of the input file).
<dest>_spot_values.csv- A CSV format file containing the inverter spot values
