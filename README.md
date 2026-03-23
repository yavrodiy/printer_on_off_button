# Turning the 3d printer on and off with an external button, connected to Orange PIi

The program for Orange Pi allows you to control the power supply of a 3d printer, connected via a relay, using an external non-lockable button

The program requires WiringOP to work

Install WiringOP 
 	
 	git clone https://github.com/orangepi-xunlong/wiringOP.git -b next
	cd wiringOP; sudo ./build clean; sudo ./build	

Download button.c from GitHub

	git clone https://github.com/yavrodiy/printer_on_off_button
	cd printer_on_off_button

Edit button.c if required

	nano button.c

Build

	gcc ~/printer_on_off_button/button.c -o button -lwiringPi -lpthread

Run

	sudo ./button



