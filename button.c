#include <stdio.h>
#include <wiringPi.h>

int main (void) {

const char  sens_on = 1;	//compatibiltity with Octoprint PSU control
const char  button_pin = 8; //connect button to 3.3v
const char  sense_pin = 1;
const char  out_pin = 2;    //pin connected to external relay module

//  int system();
  wiringPiSetup();
  pinMode (button_pin, INPUT);
  pullUpDnControl (button_pin, PUD_DOWN);
  pinMode (sense_pin, OUTPUT);
  pinMode (out_pin, OUTPUT);
  while (1)
 {
	//sense for psu control   
    if ( sens_on  &&  !(digitalRead(out_pin))) {
     digitalWrite(sense_pin, 0);
    }
    else if ( sens_on &&  digitalRead(out_pin)) {
     digitalWrite(sense_pin, 1);
    }

	//on-off printer
	if (digitalRead(button_pin) && digitalRead(out_pin)) {
	 digitalWrite(out_pin, 0);
	 delay (2000);
	}
	else if (digitalRead(button_pin) && !(digitalRead(out_pin))) {
	 digitalWrite(out_pin, 1);
	 delay (2000);
	}
	else {
	 delay (500);
	}
 }
return 0;
}
