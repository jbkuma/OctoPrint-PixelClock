# OctoPrint-PixelClock
A pixel matrix clock that automatically switches to display 3D printing status data when active.

NOTE: This project is intended as a reference, and may require some additional work to suit your purposes.  At this time I do not intend on making a more generalized sketc as this currently suits my needs perfectly.

This project displays a clock using an ESP8266, DS1307 RTC, and 8x32 matrix panel.  There are two standard clock modes: time with a seconds ticker sweeping around it, time and date with the day of the week displayed as a 7 led "graph." I am planning on adding a small symbolic weather forecast in the time/date mode.

When a 3D print is active, an MQTT plugin in OctoPrint sends data that is parsed by NodeRed into the required topics for this clock.  NodeRed isn't necessary, but since I'm capturing and parsing the data anyway, this makes my life easier on the MCU side of things.
The clock expects: target temp and actual temp for the hot end and bed, progress percentage as an integer, time remaining, and "is_active" flag.  The "is_active" flag is determined by NodeRed and counts STARTING, PRINTING, PAUSING, PAUSED, RESUMING, FINISHING, CANCELLING as true, anything else is considered false.

OctoPrint plugins:
https://github.com/OctoPrint/OctoPrint-MQTT

Arduino Libraries:  (all should be available through the manager)
https://github.com/Makuna/NeoPixelBus
https://github.com/adafruit/RTClib
https://github.com/arduino-libraries/NTPClient
https://playground.arduino.cc/Code/Time/
https://pubsubclient.knolleary.net/
