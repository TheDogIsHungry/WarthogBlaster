
// LIBRARIES ===========================================================================================

#include <Adafruit_SSD1306.h> // Screen library.
#include "Bitmaps.h" 
#include "MemoryManager.h"    // Flash memory functions.

// DEFINES ================================================================================================

#define SET 0 
#define RESET 9
#define clockPin 7
#define dtPin 8
#define buttonPin 9
#define voltPin 26

#define FRAME_DELAY (2)
#define FRAME_WIDTH (48)
#define FRAME_HEIGHT (48)
#define FRAME_COUNT (sizeof(frames) / sizeof(frames[0]))

#define BUTTONHIGH digitalRead(buttonPin)
#define CLOCKCHECK digitalRead(clockPin)
#define DTCHECK    digitalRead(dtPin)

#define burstLower 2
#define burstUpper 5
#define hangUpper 4000
#define dpsUpper 22







// SCREEN PARAMETERS ===================================================================================-

Adafruit_SSD1306 Display(128, 64); 

typedef struct {          // Struct that contains String, and x and y positions for setCursor and fillRect functions.

 String hoverLabel; 
 int x; 
 int y; 

} hover;

hover hoverOver[13] {     // Array of hover objects, serves as lookup table for displaying highlights over labels in the settings menu.
  
 {"Back", 1, 54},         // Magic numbers, correspond to pixels on 128x64 display.
 {"DPS:", 1, 13}, 
 {"Motor:", 1, 22},
 {"Brake:", 1, 31},
 {"Hang:", 1, 40},
 {"Idle:", 69, 13},
 {"Mode:", 69, 22},
 {"BSize:", 69, 31},
 {"Save", 105, 54},
 {"Front", 25, 15},
 {"Middle", 25, 25},
 {"Rear", 25, 35},
 {"Back", 52, 54},

};

String wordGuys[8] = {"Off", "Low", "Medium", "High", "Semi", "Bst", "Auto", "Bin"};  // Non numerical display for brake setting and fire mode.

enum FireMode {
SEMI = 4, BURST, AUTO, BINARY
};

enum menuSetting {
  BACK, DPS, MOTOR, BRAKE, HANG, IDLE, MODE, BSIZE, SAVE, FRONT, MIDDLE, REAR
};

enum BrakeOptions {
  OFF, Low, Med, High
};

uint16_t counter = 1;                 // Incremented / decremented to define where user is hovering (what to highlight), as well as what value to select.
uint8_t counterGhost = 1;             // When entering paramter menu, "Motor" for example, which indexed at 2, remember this position to return to it in master settings screen.
uint8_t lowerBound;                   // Defines lower bound for menus, 0 - 7 in master settings menu encompasses 'Back' to 'Save', 0 - 4000 for hang setting, etc. 
uint16_t upperBound;                  // Defines upperbound of what was explained above. 
uint8_t currentStateCLK;          
uint8_t lastStateCLK;             
unsigned long lastButtonPress = 0;    // Takes timestamp of button press using millis()
int dartsFired = 0;
int dartsFiredPrev = 0;

// FUNCTIONS ============================================================================================

int counterLength(int i) {            // Function to do some math to center labels and numbers properly depending on length, 1 = 1, "Hi" = 2, 500 = 3 etc. 
 if (i == 0) {return 1;} 
    return log10(i) + 1;
}

void waitHigh() {                     // After button press, wait for it to be depressed to avoid chaining if statements. 
  while(1) {
    if(BUTTONHIGH) {
      return;  
    }
  } 
}


float voltageRead() {
  return analogRead(26) * (3.3f / 1023.0f) * 6.0f * 0.94f;  //0.94 is an adjustment for the resisotrs being out of spec
}

void display_init() {                            // Initializes screen, displays splash.

  Display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  
  Display.clearDisplay();
  Display.display();
  Display.setCursor(0, 0);
  Display.setTextColor(1); 
  Display.setTextSize(1);
  Display.setTextWrap(false);
  Display.clearDisplay();
  Display.drawBitmap(0, 0, splash, 128, 64, 1);

  
  Display.setCursor(87,0);
  Display.println("Warthog");
  Display.setCursor(105, 34);
  Display.println("Baja");
  Display.setCursor(93, 44);
  Display.println("Blstrs");
  Display.setCursor(99, 54);  
  Display.println("1.0.0");
  Display.display();

}

// MAIN MENU ----------------------------------------------------------------------------

void mainScreen() {

  Display.clearDisplay();
  Display.setTextSize(1);
  Display.setTextColor(WHITE);

    switch(modeSetting) {
    case SEMI:
    Display.drawBitmap(0, 0, single_bmp, 128, 64, 1);	
    Display.setCursor(52, 55);
    Display.print("Semi");
    break;
    case BURST:  
    Display.drawBitmap(0, 0, burst_bmp, 128, 64, 1);	 
    Display.setCursor(43, 55);             
    Display.print("Burst-");
    Display.println(burstSetting);
    break;
    case AUTO:
    Display.drawBitmap(0, 0, auto_bmp, 128, 64, 1);	
    Display.setCursor(52, 55);      
    Display.print("Auto");
    break;
    case BINARY:
    Display.drawBitmap(0, 0, binary_bmp, 128, 64, 1);	
    Display.setCursor(46, 55);  
    Display.print("Binary");
    break;
  }
  Display.setCursor(98, 0); 
  Display.print(voltageRead()); 
  //Display.print("16.8");
  Display.print("V");
  Display.drawLine(0, 8, 128, 8, 1); 
  Display.setCursor(111 - ((profileSwitch.length() - 3) * 6), 45);// Keep "Front", "Middle", "Rear", as far right as possible. 
  Display.print(profileSwitch); 
  Display.setCursor(111 - (counterLength(dpsSetting) * 6), 55);  
  Display.print(dpsSetting);
  Display.print("Dps");
  Display.setCursor(0, 55); 
  Display.print(motorspeedSetting);
  Display.print("%");
  Display.drawLine(0, 53, 128, 53, 1); 
  Display.setCursor(0, 0);
  Display.print("Warthog");
  Display.display();
}

void updateAmmoCounter(int dartCount, int dartsPrev) {
  Display.setTextSize(4);
  Display.setTextColor(BLACK);
  Display.setCursor(60  - (counterLength(dartsPrev) * 12), 15);   // Width of characters at size 4 is 24 
  Display.print(dartsPrev);
  Display.setCursor(60  - (counterLength(dartCount) * 12), 15);
  Display.setTextColor(WHITE);
  Display.print(dartCount);
  Display.display();
}

void updateSettingScreen(int counterCopy) {                        // Contains settings master menu, save menu, parameter menu.

// SETTINGS MASTER MENU --------------------------------------------------------------------------------

Display.clearDisplay(); 

  if(menuState == "Settings") {

  Display.setTextSize(1);		                                       //  Static labels.
  Display.setTextColor(1);	                                     
  Display.setCursor(40, 0);          
  Display.print("Settings");
  Display.drawLine(40, 8, 86, 8, 1);

  Display.setCursor(1, 13);
  Display.print("DPS:  ");
  Display.print(dpsSetting);

  Display.setCursor(1, 22);
  Display.print("Motor:");
  Display.print(motorspeedSetting);
  Display.print("%");

  Display.setCursor(1, 31);
  Display.print("Brake:");
  Display.println(wordGuys[brakeSetting]);

  Display.setCursor(1, 40);
  Display.print("Hang: ");
  Display.print(hangtimeSetting);
  Display.println("ms");

  Display.setCursor(69, 13); 
  Display.print("Idle: ");
  Display.print(idleSetting);
  Display.print("%");
  
  Display.setCursor(69, 22); 
  Display.print("Mode: ");
  Display.print(wordGuys[modeSetting]);
  if(modeSetting == BURST) {				                                  // If fire mode is 5 (Burst), display BSize to allow user to change.
  Display.setCursor(69, 31); 
  Display.print("BSize:");
  Display.print(burstSetting);
  }
  Display.setCursor(105, 54); 
  Display.println("Save");
  Display.setCursor(1, 54);
  Display.print("Back");
	  
  Display.setTextColor(0);                                      	// Dynamic labels, displays highlight over hovered parameter.	
  Display.fillRect(hoverOver[counterCopy].x - 1, hoverOver[counterCopy].y - 1, (hoverOver[counterCopy].hoverLabel.length() * 6), 9, 1);	
  Display.setCursor(hoverOver[counterCopy].x, hoverOver[counterCopy].y); 
  Display.print(hoverOver[counterCopy].hoverLabel); 

  Display.display();
  return; 
}

// SAVE MENU ------------------------------------------------------------------------------------

if(menuState == "Save") {                                        // Save menu. Allows to save entered values to specific switch position.
  Display.setTextSize(1);
  Display.setTextColor(1); 
  Display.setCursor(40, 0);
  Display.print("Save to:");
  Display.drawLine(40, 8, 81, 8, 1);
  Display.setCursor(25, 15);
  Display.print("Front");
  Display.setCursor(25, 25);
  Display.print("Middle");
  Display.setCursor(25, 35);
  Display.print("Rear");
  Display.setCursor(52, 54);
  Display.print("Back");

  Display.fillRect(81, 15, 9, 8, 1);
  Display.drawRect(89, 15, 9, 8, 1);
  Display.drawRect(97, 15, 9, 8, 1);
  
  Display.drawRect(81, 25, 9, 8, 1);
  Display.fillRect(89, 25, 9, 8, 1);
  Display.drawRect(97, 25, 9, 8, 1);

  Display.drawRect(81, 35, 9, 8, 1);
  Display.drawRect(89, 35, 9, 8, 1);
  Display.fillRect(97, 35, 9, 8, 1);

  Display.setTextColor(0); 

  Display.fillRect(hoverOver[counterCopy].x - 1, hoverOver[counterCopy].y - 1, (hoverOver[counterCopy].hoverLabel.length() * 6), 9, 1);
  Display.setCursor(hoverOver[counterCopy].x, hoverOver[counterCopy].y); 
  Display.print(hoverOver[counterCopy].hoverLabel); 

  Display.display();
  return; 
}

// PARAMETER MENUS ------------------------------------------------------------------------------------

if(menuState != "Settings" && menuState != "Save") {              // When entering parameter menus, display accordingly. 
  Display.setTextSize(2);
  Display.setTextColor(1); 
  Display.setCursor(52 - ((menuState.length() - 3) * 6), 0);      // Aforementioned math to center numbers and labels, one digit or character is ~ 6 pixels. Start at 3 for label "DPS:" as reference
  Display.print(menuState);
  if(menuState == "Mode:" || menuState == "Brake:") {
    Display.setCursor(64 - (wordGuys[counterCopy].length() * 6), 24);
    Display.print(wordGuys[counterCopy]);                         // Display strings in wordGuys instead of counter # depending on menuState.
  }
  else {
  Display.setCursor(64 - (counterLength(counterCopy) * 6), 24);   // Start at 1 for single digit. Each subsequent digit or character subtracts another 6, to keep centered.
  Display.print(counterCopy);                                     // All other parameters are numerical.
  } 
  Display.display();
  return; 
  }
  // END OF updateSettingScreen.
 }


void lowbatteryScreen() {                                         // Called when battery voltage is under limit.
int frame = 0;			    				  // Given frame of battery animation.
 while(1) {							  // lowbatteryScreen is blocking, to prevent blaster operation if battery voltage is too low. 
    Display.setTextSize(1);
    Display.setTextColor(1);
    Display.clearDisplay();
    Display.drawBitmap(40, 0, frames[frame], FRAME_WIDTH, FRAME_HEIGHT, 1);
    Display.setCursor(31, 50);
    Display.print("Low Battery");
    Display.display();
    frame = (frame + 1) % FRAME_COUNT;
    delay(FRAME_DELAY);
 }
}

void selfTestScreen() {
  Display.clearDisplay();
  Display.drawBitmap(52, 20, self_test, 24, 24, 1);
  Display.display();
  delay(1000);
}


void setCounter(int counterCopy, uint8_t counterGhostCopy) {                //Setting upper and lower bound of counter depending on menu, set menuState.

menuState = hoverOver[counterCopy].hoverLabel;                              // Cheap way to keep menuState, correspond counter to index in hoverLabel, EX: hoverOver[1].hoverLabel = "DPS:". 
  switch(counterCopy) {
    case SET:                          // Settings Menu. Macro SET = 0
     lowerBound = BACK; 
     upperBound = SAVE;
     menuState = "Settings";           // Manually set to "Settings".
     counter = counterGhostCopy;       // Reset at previously selected parameter.
     break;
    case RESET:                        // Only called when settingsMenu is called in void loop().                 
     lowerBound = BACK; 
     upperBound = SAVE;                           
     counter = DPS; 												
     counterGhost = counter;
     menuState = "Settings";  
     break;
    case DPS:   // DPS.
     lowerBound = 1;                  // Set upper and lower bound for given parameter menu, DPS can be selected from 1 - 10, for example.
     upperBound = dpsUpper;
     counter = dpsSetting;            // Once a parameter menu is displayed, set counter to value to current (parametername)Setting
     break; 
    case MOTOR:   // MotorSpeed.
     (idleSetting <= 10) ? lowerBound = 15 : lowerBound = idleSetting + 1; 
     upperBound = 100;
     counter = motorspeedSetting; 
     break;
    case BRAKE:  // Brake Setting.
     lowerBound = OFF; 		              // Brake and Fire mode (Mode) display strings instead of numerical values. Upper and lower limits mapped to wordGuys array.
     upperBound = High;
     counter = brakeSetting; 
     break;
    case HANG:   // Hangtime.
     lowerBound = 0; 
     upperBound = hangUpper; 
     counter = hangtimeSetting; 
     break;
    case IDLE:   // Idle
     lowerBound = 0; 
     (motorspeedSetting % 10 == 0) ? upperBound = motorspeedSetting - 10 : upperBound = motorspeedSetting - (motorspeedSetting % 10);    // Calculates 'floor' of motorspeedSetting, EX: motorspeedSetting set to 34 -> 34 - (34 % 10) = 34 - 4 = 30.
     counter = idleSetting;
     break;                                                        // This ensures that the idle speed does not exceed the motor speed at point.
    case MODE:   // Fire Mode.
     lowerBound = SEMI; 
     upperBound = BINARY; 
     counter = modeSetting; 
     break; 
    case BSIZE:  // Burst amount.
     lowerBound = burstLower; 
     upperBound = burstUpper;
     counter = burstSetting; 
     break;
    case SAVE:  // Save menu.
     lowerBound = FRONT; 
     upperBound = 12;     // Back button.
     counter = FRONT; 
     break;
  }
    return; 
}


// MAIN SETTINGS FUNCTION -----------------------------------------------------------------------------------------------------------

void settingsMenu() {                                                    // Master function, calls previous functions to update screen according to counter and menuState values, detects button press and encoder scrolls.

  waitHigh(); 		     				                 // On function call, wait for high signal.
  setCounter(RESET, 1);                                                  // Set master settings menu upper and lower bound.
  updateSettingScreen(counter);				                 // Display master settings menu first.

  while(menuState != "Main Menu") {                                      // Runs until broken from via back button or save button. 
/*
    if(voltageRead() < 13.5) {			                         // Check battery in settings menu.
       while(1) {
        lowbatteryScreen(); 
       }
     }           
*/

// HANDLING ENCODER SCROLLS --------------------------------------------------------------------------------------------------------

   currentStateCLK = CLOCKCHECK; 

   if(currentStateCLK != lastStateCLK  && currentStateCLK == 1) {                                     // Encoder stuff.
     if (DTCHECK != currentStateCLK) {
      if(counter < upperBound) {
       (menuState == "Hang:") ? counter += 100 : (menuState == "Idle:") ? counter += 10 : counter++;  // If on hang, inc. by 100, if on Idle, inc. by 10, otherwise 1.
       (menuState == "Settings" && modeSetting != BURST && counter == BSIZE) ? counter = SAVE : counter += 0;   // If fire mode is not (Burst), skip over the array element that contains BSize.
       }
      } else {
        if(counter > lowerBound) {
      	(menuState == "Hang:") ? counter -= 100 : (menuState == "Idle:") ? counter -= 10 : counter--;
        (menuState == "Settings" && modeSetting != BURST && counter == BSIZE) ? counter = MODE : counter -= 0; 
        }
       }  
      updateSettingScreen(counter);
     }

  lastStateCLK = currentStateCLK;     

// HANDLING ENCODER BUTTON PRESS ----------------------------------------------------------------------------------------------------

   if (!BUTTONHIGH) {	
                                                                                        // If back button pressed on settings, or save position selected in save menu, break, return to void loop()
    if (millis() - lastButtonPress > 50 && (menuState == "Settings" && counter == BACK) || (menuState == "Save" && counter != 12)) { 
      waitHigh();
      break; 
    }
    if (millis() - lastButtonPress > 50 && menuState != "Settings") {                   // If in any other menu than master settings page, return to it.
      waitHigh();
      if(counterGhost < SAVE) {*modifierArray[counterGhost - 1] = counter;}                // Save value entered according to counterGhost and current value of counter.
      setCounter(SET, counterGhost);                                                    // Return to master settings menu settings. lmao.
      updateSettingScreen(counter);                                            
      continue;                                                                         // Return to beginning of for loop to avoid chain if statement
     }
    if (millis() - lastButtonPress > 50 && menuState == "Settings") {                   
      waitHigh();
      counterGhost = counter;                                                           // Remember which parameter was selected
      setCounter(counter, counterGhost);                                                // Send counter position to determine which parameter menu to display, EX: If counter value = 2, Motor parameter menu will display.
      updateSettingScreen(counter);                                                     // Display proper parameter menu.
     }
  lastButtonPress = millis();
   }
  delay(1);                                                                             // Slight delay to help debounce reading.
 }
  return; 										// END OF settingsMenu function
}
