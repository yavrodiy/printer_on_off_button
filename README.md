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

Edit button.c if required

	nano button.c

Build

	gcc ~/printer_on_off_button/button.c -o button -lwiringPi -lpthread

Run

	sudo ./button
### Setting the program as a service for automatic startup
	sudo cp button /usr/local/bin/
	sudo cp button.service /etc/systemd/system/
	sudo systemctl enable button


