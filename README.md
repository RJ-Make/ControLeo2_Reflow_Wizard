
Modify Date: 
12-31-2014
Mod Description: 
Slightly modified version of Peter Easton's Reflow Wizard 1.1 source code for the ControLeo2 Reflow Oven
that incorporates Door Opening/Closing functions using a modified version of Philip van Allen's VarSpeedServo libraries. 

The original VarSpeedServo code from Philip van Allen can be found below:
https://github.com/netlabtoolkit/VarSpeedServo

*************************** VarSpeedServo MODIFICATIONS *************************
In the VarSpeedServo.h file:

Change This: (Comment Out This Section of Code)
#elif defined(__AVR_ATmega32U4__)
#define _useTimer3
#define _useTimer1
typedef enum { _timer3, _timer1, _Nbr_16timers } timer16_Sequence_t ;

TO THIS:

//#elif defined(__AVR_ATmega32U4__)  
//#define _useTimer3
//#define _useTimer1 
//typedef enum { _timer3, _timer1, _Nbr_16timers } timer16_Sequence_t ;


*************************** Reflow Wizard 1.1 MODIFICATIONS *************************

1) During Learning Mode, Open door on failure to meet requirements.
2) On completion of successfully reflow, Oven door will Open.
3) On completion of successfully reflow cool down <50c oven door will close

SAMPLE (Vars)
// **** Door Servo ****
VarSpeedServo myservo;    // create servo object to control a servo
#define SERVO_PIN         3
int servoclosePos=		 170; //Door Closed
int servoopenPos=		  30; //Door Open
int servospeed=			  50; //How fast does the servo move
int servocurrPos;

***** About the ControLeo2 Reflow Oven Controller *****
ControLeo2 is an Arduino Leonardo controller with quad relay outputs used to control reflow ovens.

This project is now on Kickstarter!
https://www.kickstarter.com/projects/1471240030/controleo2-reflow-oven

Reflow oven build guide:
http://www.whizoo.com/reflowoven

The original code from Peter Easton. His GitHub source code repository for ControLeo2, can be found below
https://github.com/engineertype/ControLeo2

To install the ControLeo2 library, please refer to:
http://arduino.cc/en/Guide/Libraries

