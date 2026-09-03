# Turning on and off the 3d printer with an external button, connected to Orange Pi

This program for Orange Pi, allows you to control the power supply of a 3d printer, connected via a relay, using an external non-lockable button

You need to install [WiringOP](https://github.com/orangepi-xunlong/wiringOP.git)

### Install WiringOP 
 	
 	git clone https://github.com/orangepi-xunlong/wiringOP.git -b next
	cd wiringOP; sudo ./build clean; sudo ./build	

### Install program
Download source from GitHub

	git clone https://github.com/yavrodiy/printer_on_off_button
	cd printer_on_off_button

Edit printer-power.c if required, add Api key

	nano printer-power.c

Build

	sudo apt install gcc libcurl4-openssl-dev
	gcc -O2 -Wall ~/printer_on_off_button/printer-power.c -o printer-power -lwiringPi -lcurl


Run

	sudo ./printer-power
### Setting the program as a service for automatic startup
	sudo cp printer-power /usr/local/bin/
	sudo cp button.service /etc/systemd/system/
	sudo systemctl enable button


