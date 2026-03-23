#include <stdio.h>
#include <wiringPi.h>

int main (void) {

const char  button_pin = 0;
const char  sense_pin = 1;
const char  out_pin = 2;    //pin connected to external relay module

//  int system();
  wiringPiSetup();
  pinMode (button_pin, INPUT) ;
  pullUpDnControl (button_pin, PUD_UP);
  pinMode (sense_pin, INPUT) ;
  pinMode (out_pin, OUTPUT);
  while (true)
 {
	if (digitalRead(button_pin) && digitalRead(sense_pin)) {
	 digitalWrite(out_pin, 0);
	 delay (2000);
	}
	else if (digitalRead(button_pin) && !(digitalRead(sense_pin))) {
	 digitalWrite(out_pin, 1);
	 delay (2000);
	}
	else {
	 delay (500);
	}
 }
return 0;
}
