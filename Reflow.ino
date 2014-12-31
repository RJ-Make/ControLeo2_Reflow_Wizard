// Reflow logic
// Called from the main loop 20 times per second
// This where the reflow logic is controlled


// Buffer used for Serial.print
char debugBuffer[100];

#define MILLIS_TO_SECONDS    ((long) 1000)

// The data for each of pre-soak, soak and reflow phases
struct phaseData {
  int elementDutyCycle[4];    // Duty cycle for each output
  int endTemperature;         // The temperature at which to move to the next reflow phase
  int phaseMinDuration;       // The minimum number of seconds that this phase should run for
  int phaseMaxDuration;       // The maximum number of seconds that this phase should run for
};


// Return false to exit this mode
boolean Reflow() {
  static int reflowPhase = PHASE_INIT;
  static int outputType[4];
  static int maxTemperature;
  static boolean learningMode;
  static phaseData phase[PHASE_REFLOW+1];
  static unsigned long phaseStartTime, reflowStartTime;
  static int elementDutyCounter[4];
  static int counter = 0;
  static boolean firstTimeInPhase = true;
  
  double currentTemperature;
  unsigned long currentTime = millis();
  int i, j;
  int elementDutyStart = 0;
  
  // Read the temperature
  currentTemperature = thermocouple.readThermocouple(CELSIUS);
  if (THERMOCOUPLE_FAULT(currentTemperature)) {
    lcdPrintLine(0, "Thermocouple err");
    Serial.print(F("Thermocouple Error: "));
    switch ((int) currentTemperature) {
      case FAULT_OPEN:
        lcdPrintLine(1, "Fault open");
        Serial.println(F("Fault open"));
        break;
      case FAULT_SHORT_GND:
        lcdPrintLine(1, "Short to GND");
        Serial.println(F("Short to ground"));
        break;
      case FAULT_SHORT_VCC:
        lcdPrintLine(1, "Short to VCC");
		Serial.println(F("Short to VCC"));
        break;
    }
    
    // Abort the reflow
    Serial.println(F("Reflow aborted because of thermocouple error!"));
    reflowPhase = PHASE_ABORT_REFLOW;
  }
  
  // Abort the reflow if a button is pressed
  if (getButton() != CONTROLEO_BUTTON_NONE) {
    reflowPhase = PHASE_ABORT_REFLOW;
    lcdPrintLine(0, "Aborting reflow");
    lcdPrintLine(1, "Button pressed");
    Serial.println(F("Button pressed.  Aborting reflow ..."));
  }
  
  switch (reflowPhase) {
    case PHASE_INIT: // User has requested to start a reflow
      	//// Lets make sure the oven door is closed now
      	if (servocurrPos == servoopenPos)
      	{
	      	myservo.write(servoclosePos,servospeed,true);
	      	servocurrPos = myservo.read();	  // capture the current servo position
      	}	
      // Make sure the oven is cool.  This makes for more predictable/reliable reflows and
      // gives the SSR's time to cool down a bit.
      if (currentTemperature > 50.0) {
        lcdPrintLine(0, "Temp > 50\1C");
        lcdPrintLine(1, "Please wait...");
        // Print degree Celsius symbol
        Serial.println(F("Oven too hot to start reflow.  Please wait ..."));
        
        // Abort the reflow
        reflowPhase = PHASE_ABORT_REFLOW;
        break;
      }
    
      // Get the types for the outputs (elements, fan or unused)
      for (i=0; i<4; i++)
        outputType[i] = getSetting(SETTING_D4_TYPE + i);
      // Get the maximum temperature
      maxTemperature = getSetting(SETTING_MAX_TEMPERATURE);
      
      // If the settings have changed then set up learning mode
      if (getSetting(SETTING_SETTINGS_CHANGED) == true) {
        setSetting(SETTING_SETTINGS_CHANGED, false);
        // Tell the user that learning mode is being enabled
        lcdPrintLine(0, "Settings changed");
        lcdPrintLine(1, "Initializing...");
        Serial.println(F("Settings changed by user.  Reinitializing element duty cycles and enabling learning mode ..."));
        
        // Turn learning mode on
        setSetting(SETTING_LEARNING_MODE, true);
        // Set the starting duty cycle for each output.  These settings are conservative
        // because it is better to increase them each cycle rather than risk damage to
        // the PCB or components
        // Presoak = Rapid rise in temperature
        //           Lots of heat from the bottom, some from the top
        // Soak    = Stabilize temperature of PCB and components
        //           Heat from the bottom, not much from the top
        // Reflow  = Heat the solder and pads rapidly
        //           Lots of heat from all directions
        for (i=0; i<4; i++) {
          switch (outputType[i]) {
            case TYPE_UNUSED:
              setSetting(SETTING_PRESOAK_D4_DUTY_CYCLE + i, 0);
              setSetting(SETTING_SOAK_D4_DUTY_CYCLE + i, 0);
              setSetting(SETTING_REFLOW_D4_DUTY_CYCLE + i, 0);
              break;
            case TYPE_TOP_ELEMENT:
              setSetting(SETTING_PRESOAK_D4_DUTY_CYCLE + i, 45);
              setSetting(SETTING_SOAK_D4_DUTY_CYCLE + i, 40);
              setSetting(SETTING_REFLOW_D4_DUTY_CYCLE + i, 60);
              break;
            case TYPE_BOTTOM_ELEMENT:
              setSetting(SETTING_PRESOAK_D4_DUTY_CYCLE + i, 80);
              setSetting(SETTING_SOAK_D4_DUTY_CYCLE + i, 70);
              setSetting(SETTING_REFLOW_D4_DUTY_CYCLE + i, 80);
              break;
            case TYPE_BOOST_ELEMENT:
              setSetting(SETTING_PRESOAK_D4_DUTY_CYCLE + i, 40);
              setSetting(SETTING_SOAK_D4_DUTY_CYCLE + i, 35);
              setSetting(SETTING_REFLOW_D4_DUTY_CYCLE + i, 55);
              break;
            case TYPE_CONVECTION_FAN:
              setSetting(SETTING_PRESOAK_D4_DUTY_CYCLE + i, 100);
              setSetting(SETTING_SOAK_D4_DUTY_CYCLE + i, 100);
              setSetting(SETTING_REFLOW_D4_DUTY_CYCLE + i, 100);
              break;
          }
        }
        // Wait a bit to allow the user to read the message
        delay(3000);
      } // end of settings changed
      
      // Read all the settings
      learningMode = getSetting(SETTING_LEARNING_MODE);
      for (i=PHASE_PRESOAK; i<=PHASE_REFLOW; i++) {
        for (j=0; j<4; j++)
          phase[i].elementDutyCycle[j] = getSetting(SETTING_PRESOAK_D4_DUTY_CYCLE + ((i-PHASE_PRESOAK) *4) + j);
        // Time to peak temperature should be between 3.5 and 5.5 minutes.
        // While J-STD-20 gives exact phase temperatures, the reading depends very much on the thermocouple used
        // and its location.  Varying the phase temperatures as the max temperature changes allows for thermocouple
        // variation.
        switch(i) {
          case PHASE_PRESOAK:
            phase[i].endTemperature = maxTemperature * 3 / 5; // J-STD-20 gives 150C
            phase[i].phaseMinDuration = 60;
            phase[i].phaseMaxDuration = 90;
            break;
          case PHASE_SOAK:
            phase[i].endTemperature = maxTemperature * 4 / 5; // J-STD-20 gives 200C
            phase[i].phaseMinDuration = 80;
            phase[i].phaseMaxDuration = 120;
            break;
          case PHASE_REFLOW:
            phase[i].endTemperature = maxTemperature;
            phase[i].phaseMinDuration = 60;
            phase[i].phaseMaxDuration = 90;
            break;
        }
      }
      // Let the user know if learning mode is on
      if (learningMode) {
        lcdPrintLine(0, "Learning Mode");
        lcdPrintLine(1, "is enabled");
        Serial.println(F("Learning mode is enabled.  Duty cycles may be adjusted automatically if necessary"));
        delay(3000);
      }
      
      // Move to the next phase
      reflowPhase = PHASE_PRESOAK;
      lcdPrintLine(0, phaseDescription[reflowPhase]);
      lcdPrintLine(1, "");
      
      // Display information about this phase
      serialDisplayPhaseData(reflowPhase, &phase[reflowPhase], outputType);

      // Stagger the element start cycle to avoid abrupt changes in current draw
      for (i=0; i< 4; i++) {
        elementDutyCounter[i] = elementDutyStart;
        // Turn the next element on (elementDutyCounter[i+1] == 0) when this element turns off (elementDutyCounter[i] == phase[reflowPhase].elementDutyCycle[i])
        // For example, assume two element both at 20% duty cycle.  
        //   The counter for the first starts at 0
        //   The counter for the second should start at 80, because by the time counter 1 = 20 (1 turned off) counter 2 = 0 (counter 2 turned on)
        elementDutyStart = (100 + elementDutyStart - phase[reflowPhase].elementDutyCycle[i]) % 100;
      }
      
      // Start the reflow and phase timers
      reflowStartTime = millis();
      phaseStartTime = reflowStartTime;
      break;
      
    case PHASE_PRESOAK:
    case PHASE_SOAK:
    case PHASE_REFLOW:
      // Has the ending temperature for this phase been reached?
      if (currentTemperature >= phase[reflowPhase].endTemperature) {
        // Was enough time spent in this phase?
        if (currentTime - phaseStartTime < (phase[reflowPhase].phaseMinDuration * MILLIS_TO_SECONDS)) {
          sprintf(debugBuffer, "Warning: Oven heated up too quickly! Phase took %d seconds.", (currentTime - phaseStartTime) / MILLIS_TO_SECONDS);
          Serial.println(debugBuffer);
          // Too little time spent in this phase.  Reduce the duty cycle
          if (learningMode) {
            // Reduce the duty cycle of the elements and abort this test run
            adjustPhaseDutyCycle(reflowPhase, -4);
            lcdPrintPhaseMessage(reflowPhase, "Too fast");
            lcdPrintLine(1, "Aborting ...");
            reflowPhase = PHASE_ABORT_REFLOW;
            Serial.println(F("Adjustments have been made to duty cycles for this phase.  Aborting ..."));
            break;
          }
          else {
            // It is bad to make adjustments when not in learning mode, because this leads to inconsistent
            // results.  However, this situation cannot be ignored.  Reduce the duty cycle slightly but
            // don't abort the reflow
            adjustPhaseDutyCycle(reflowPhase, -1);
            Serial.println(F("Duty cycles lowered slightly for future runs"));
          }
        }
        // The temperature is high enough to move to the next phase
        reflowPhase++;
        firstTimeInPhase = true;
        lcdPrintLine(0, phaseDescription[reflowPhase]);
        phaseStartTime = millis();
        // Stagger the element start cycle to avoid abrupt changes in current draw
        for (i=0; i< 4; i++) {
          elementDutyCounter[i] = elementDutyStart;
          // Turn the next element on (elementDutyCounter[i+1] == 0) when this element turns off (elementDutyCounter[i] == phase[reflowPhase].elementDutyCycle[i])
          // For example, assume two element both at 20% duty cycle.  
          //   The counter for the first starts at 0
          //   The counter for the second should start at 80, because by the time counter 1 = 20 (1 turned off) counter 2 = 0 (counter 2 turned on)
          elementDutyStart = (100 + elementDutyStart - phase[reflowPhase].elementDutyCycle[i]) % 100;
        }
        // Display information about this phase
        if (reflowPhase <= PHASE_REFLOW)
          serialDisplayPhaseData(reflowPhase, &phase[reflowPhase], outputType);
        break;
      }
      
      // Has too much time been spent in this phase?
      if (currentTime - phaseStartTime > (phase[reflowPhase].phaseMaxDuration * MILLIS_TO_SECONDS)) {
        Serial.print(F("Warning: Oven heated up too slowly! Current temperature is "));
        Serial.println(currentTemperature);
        // Still in learning mode?
        if (learningMode) {
          // Keep the element on for longer next time.  Adjust by more if the temperature was pretty low
          if (currentTemperature < phase[reflowPhase].endTemperature - 15)
            adjustPhaseDutyCycle(reflowPhase, 12);
          else
            adjustPhaseDutyCycle(reflowPhase, 6);
          lcdPrintPhaseMessage(reflowPhase, "Too slow");
          lcdPrintLine(1, "Aborting ...");
          reflowPhase = PHASE_ABORT_REFLOW;
          Serial.println(F("Adjustments have been made to duty cycles for this phase.  Aborting ..."));
          break;
        }
        else {
          // It is bad to make adjustments when not in learning mode, because this leads to inconsistent
          // results.  However, this situation cannot be ignored.  Increase the duty cycle slightly but
          // don't abort the reflow
          adjustPhaseDutyCycle(reflowPhase, 1);
          Serial.println(F("Duty cycles increased slightly for future runs"));
            
          // Turn all the elements on to get to temperature quickly
          for (i=0; i<4; i++) {
            if (outputType[i] != TYPE_UNUSED)
              phase[reflowPhase].elementDutyCycle[i] = 100;
          }
            
          // Extend this phase by 5 seconds, or abort the reflow if it has taken too long
          if (phase[reflowPhase].phaseMaxDuration < 200)
            phase[reflowPhase].phaseMaxDuration += 5;
          else {
            lcdPrintPhaseMessage(reflowPhase, "Too slow");
            lcdPrintLine(1, "Aborting ...");
            reflowPhase = PHASE_ABORT_REFLOW;
            Serial.println(F("Aborting reflow.  Oven cannot reach required temperature!"));
          }
        }
      }
      
      // Turn the output on or off based on its duty cycle
      for (i=0; i< 4; i++) {
        // Skip unused elements
        if (outputType[i] == TYPE_UNUSED)
          continue;
        // Turn all the elements on at the start of the presoak
        if (reflowPhase == PHASE_PRESOAK && currentTemperature < (phase[reflowPhase].endTemperature * 3 / 5)) {
          digitalWrite(4 + i, HIGH);
          continue;
        }
        // Turn the output on at 0, and off at the duty cycle value
        if (elementDutyCounter[i] == 0)
          digitalWrite(4 + i, HIGH);
        if (elementDutyCounter[i] == phase[reflowPhase].elementDutyCycle[i])
          digitalWrite(4 + i, LOW);
        // Increment the duty counter
        elementDutyCounter[i] = (elementDutyCounter[i] + 1) % 100;
      }
      
      // Don't consider the reflow process started until the temperature passes 50 degrees
      if (currentTemperature < 50.0)
        phaseStartTime = currentTime;
      
      // Update the displayed temperature roughly once per second
      if (counter++ % 20 == 0)
        displayReflowTemperature(currentTime, reflowStartTime, phaseStartTime, currentTemperature);
      break;
      
    case PHASE_WAITING:  // Wait for solder to reach max temperatures and start cooling
      if (firstTimeInPhase) {
        firstTimeInPhase = false;
        // Update the display
        lcdPrintLine(0, "Reflow");
        lcdPrintLine(1, " ");
        Serial.println(F("******* Phase: Waiting *******"));
        Serial.println(F("Turning all heating elements off ..."));
        // Make sure all the elements are off (keep convection fans on)
        for (int i=0; i<4; i++) {
          if (outputType[i] != TYPE_CONVECTION_FAN)
            digitalWrite(i+4, LOW);
        }
        // If we made it here it means the reflow is within the defined parameters.  Turn off learning mode
        setSetting(SETTING_LEARNING_MODE, false);
      }
      // Update the displayed temperature roughly once per second
      if (counter++ % 20 == 0) {
        displayReflowTemperature(currentTime, reflowStartTime, phaseStartTime, currentTemperature);
        // Countdown to the end of this phase
        lcd.setCursor(13, 0);
        lcd.print(30 - ((currentTime - phaseStartTime) / MILLIS_TO_SECONDS));
        lcd.print("s ");
      }
       
      // Wait in this phase for 30 seconds.  The maximum time in liquidous state is 150 seconds
      // Max 90 seconds in PHASE_REFLOW + 30 seconds in PHASE_WAITING + some cool down time in PHASE_COOLING_BOARDS_IN is less than 150 seconds.
      if (currentTime - phaseStartTime > 30 * MILLIS_TO_SECONDS) {
        reflowPhase = PHASE_COOLING_BOARDS_IN;
        firstTimeInPhase = true;
      }
      break;
      
    case PHASE_COOLING_BOARDS_IN: // Start cooling the oven.  The boards must remain in the oven to cool
      if (firstTimeInPhase) {
        firstTimeInPhase = false;
        // Update the display
        lcdPrintLine(0, "Cool - open door");
        Serial.println(F("******* Phase: Cooling *******"));
        Serial.println(F("Open the oven door ..."));
        // Play a tune to let the user know the door should be opened
        playTones(TUNE_REFLOW_DONE);

		  //// Lets Open the oven door now
		  if (servocurrPos == servoclosePos)
		  {
			myservo.write(servoopenPos,servospeed,true);
			servocurrPos = myservo.read(); 	  // capture the current servo position
		  }
	
      }
      // Update the temperature roughly once per second
      if (counter++ % 20 == 0)
        displayReflowTemperature(currentTime, reflowStartTime, phaseStartTime, currentTemperature);
        
      // Boards can be removed once the temperature drops below 100C
      if (currentTemperature < 100.0) {
        reflowPhase = PHASE_COOLING_BOARDS_OUT;
        firstTimeInPhase = true;
      }
      break;
      
    case PHASE_COOLING_BOARDS_OUT: // The boards can be removed without dislodging components now
      if (firstTimeInPhase) {
        firstTimeInPhase = false;
        // Update the display
        lcdPrintLine(0, "Okay to remove  ");
        lcdPrintLine(1, "          boards");
        // Play a tune to let the user know the boards can be removed
        playTones(TUNE_REMOVE_BOARDS);
      }
      // Update the temperature roughly once per second
      if (counter++ % 20 == 0)
        displayReflowTemperature(currentTime, reflowStartTime, phaseStartTime, currentTemperature);
        
      // Once the temperature drops below 50C a new reflow can be started
      if (currentTemperature < 50.0) {
        reflowPhase = PHASE_ABORT_REFLOW;
        lcdPrintLine(0, "Reflow complete!");
        lcdPrintLine(1, " ");
		
		  //// Lets close the oven door now
		  if (servocurrPos == servoopenPos)
		  {
			  myservo.write(servoclosePos,servospeed,true);
			  servocurrPos = myservo.read(); 	  // capture the current servo position
		  }		
		
      }
      break;
      
    case PHASE_ABORT_REFLOW: // The reflow must be stopped now
      Serial.println(F("Reflow is done!"));
      // Turn all elements and fans off
      for (i = 4; i < 8; i++)
        digitalWrite(i, LOW);
      // Start next time with initialization
      reflowPhase = PHASE_INIT;
      // Wait for a bit to allow the user to read the last message
      delay(3000);
	  
	      //// Lets Open the oven door now but only if we are in learning mode
	      if (learningMode) {
		      if (servocurrPos != servoopenPos)
		      {
			      myservo.write(servoopenPos,servospeed,true);
			      servocurrPos = myservo.read(); 	  // capture the current servo position
		      }
	      }
		  	  
      // Return to the main menu
      return false;
  }
  
  return true;
}


// Adjust the duty cycle for all elements by the given adjustment value
void adjustPhaseDutyCycle(int phase, int adjustment) {
  int newDutyCycle;
  sprintf(debugBuffer, "Adjusting duty cycles for %s phase by %d", phaseDescription[phase], adjustment);
  Serial.println(debugBuffer);
  // Loop through the 4 outputs
  for (int i=0; i< 4; i++) {
    int dutySetting = SETTING_PRESOAK_D4_DUTY_CYCLE + ((phase-1) * 4) + i;
    switch(getSetting(SETTING_D4_TYPE + i)) {
      case TYPE_TOP_ELEMENT:
      case TYPE_BOTTOM_ELEMENT:
      case TYPE_BOOST_ELEMENT:
        newDutyCycle = getSetting(dutySetting) + adjustment;
        // Duty cycle must be between 0 and 100
        newDutyCycle = constrain(newDutyCycle, 0, 100);

        sprintf(debugBuffer, "D%d (%s) changed from %d to %d", i+4, outputDescription[getSetting(SETTING_D4_TYPE + i)], getSetting(dutySetting), newDutyCycle);
        Serial.println(debugBuffer);
        // Save the new duty cycle
        setSetting(dutySetting, newDutyCycle);
        break;
      
      default:
        // Don't change the duty cycle if the output is unused, or used for a convection fan
        break;
    }
  }
}


// Displays a message like "Reflow:Too slow"
void lcdPrintPhaseMessage(int phase, const char* str) {
  char buffer[20];
  // Sanity check on the parameters
  if (!str || strlen(str) > 8)
    return;
  sprintf(buffer, "%s:%s", phaseDescription[phase], str);
  lcdPrintLine(0, buffer);
}


// Print data about the phase to the serial port
void serialDisplayPhaseData(int phase, struct phaseData *pd, int *outputType) {
  sprintf(debugBuffer, "******* Phase: %s *******", phaseDescription[phase]);
  Serial.println(debugBuffer);
  sprintf(debugBuffer, "Minimum duration = %d seconds", pd->phaseMinDuration);
  Serial.println(debugBuffer);
  sprintf(debugBuffer, "Maximum duration = %d seconds", pd->phaseMaxDuration);
  Serial.println(debugBuffer);
  sprintf(debugBuffer, "End temperature = %d Celsius", pd->endTemperature);
  Serial.println(debugBuffer);
  Serial.println(F("Duty cycles: "));
  for (int i=0; i<4; i++) {
    sprintf(debugBuffer, "  D%d = %d  (%s)", i+4, pd->elementDutyCycle[i], outputDescription[outputType[i]]);
    Serial.println(debugBuffer);
  }
}


// Display the current temperature to the LCD screen and print it to the serial port so it can be plotted
void displayReflowTemperature(unsigned long currentTime, unsigned long startTime, unsigned long phaseTime, double temperature) {
  // Display the temperature on the LCD screen
  displayTemperature(temperature);
  // Write the time and temperature to the serial port, for graphing or analysis on a PC
  sprintf(debugBuffer, "%ld, %ld, ", (currentTime - startTime) / MILLIS_TO_SECONDS, (currentTime - phaseTime) / MILLIS_TO_SECONDS);
  Serial.print(debugBuffer);
  Serial.println(temperature);
}


// For debugging you can use this function to simulate the thermocouple
/*double getTemperature() {
  static double counterTemp = 29.0;
  static int phase = 0;
  double diff[] = {0.08, 0.03, 0.05, -0.26}; 
  double tempChange[] = {140, 215, 255, 40};
  
  if (diff[phase] > 0 && counterTemp > tempChange[phase])
    phase++;
  else if (diff[phase] < 0 && counterTemp < tempChange[phase])
    phase++;
  phase = phase % 4;
  counterTemp += diff[phase];
}*/
