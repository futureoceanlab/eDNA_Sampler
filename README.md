# eDNA Sampler
This is a project that controls an autonomous eDNA Sampler. There are two modules: ESP8266 based hardware controller and a Django Web Application for interact with ESP8266 wirelessly.

## Web App
The program is run on a virtual environment, so install the virtualenv on the machine that will run the Web App.

```bash
pip3 install virtualenv
```

In case that the virtualenv is installed in a path that is not listed (i.e. virtualenv will complain saying that there is no such command), please add the following line to the end of ~/.profile 

```bash
PATH=$PATH:$HOME/.local/bin
```

in eDNA_Sampler/webapp, enter the following line in the terminal

```bash
virtualenv env
source env/bin/activate
``` 

This should activate the virtual environment, and you are ready to install the required packages

```bash
pip3 install -r requirements.txt
```

The webserver hosts' IP address can be found at WLAN0 interface by running the following:

```bash
ifconfig
```

In the webapp directory, run the server

```bash
python3 manage.py runserver 0.0.0.0:5000
```

0.0.0.0 will listen to other devices connected through every interfaces. 5000 is the port, and it can be different as long as it does not coincide with other used ports. 

Finally, check the Firewall if the client cannot access the webapp.

## ESP8266 Based Arduino code
The system uses ESP8266 Huzzah from Adafruit and a variety of libraries, and thus, Arduino IDE requires some setup.

In particular, the libraries that need to be installed are:
[ESP8266](https://learn.adafruit.com/adafruit-huzzah-esp8266-breakout/using-arduino-ide), 
[GrooveNfc](https://github.com/Seeed-Studio/Seeed_Arduino_NFC), 
ArduinoJson,
[MS5837](https://github.com/bluerobotics/BlueRobotics_MS5837_Library), 
[TSYS01](https://github.com/bluerobotics/BlueRobotics_TSYS01_Library).

[ESP8266 Huzzah](https://learn.adafruit.com/adafruit-huzzah-esp8266-breakout/using-arduino-ide) explains how to program the device using FTDI converter.
