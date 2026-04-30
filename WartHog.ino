
// LIBRARIES  =============================================================================

#include "Display.h" // Handles all screen functions and outputs.
#include <debounce.h> //trigger signal cleanup
#include <PIO_DShot.h> //AM32 DShot

// DEFINES ================================================================================

#define trigger 18
#define solenoid_mosfet 6
#define escPin1 16
#define escPin2 17
#define backHall 0
#define frontHall 1
#define MOTOR_POLES 14

// modeSetting.
#define SEMI 4 
#define BURST 5
#define AUTO 6 
#define BINARY 7

// Brake Setting (ms).
#define off 1100
#define low 2000
#define med 800
#define high 400

// 3 Position Switch.
#define FORWARD 0
#define MIDDLE 1 
#define REAR 2
#define switch_pin_1 20
#define switch_pin_2 19
#define SWITCHREAD_1 digitalRead(switch_pin_1)
#define SWITCHREAD_2 digitalRead(switch_pin_2)

// Solenoid.
#define IDLE 0 
#define THRUSTING 1
#define RETRACTING 2

// Open loop Solenoid.
#define solenoidOn 45
#define solenoidOff 45
#define openDPS 10 
#define maxError 5


// LOGIC VARIABLES ==========================================================================

String motorState = "idle";  //BETA WAS used for old flyshot, idk if we need this
uint8_t switchPosPrev;
uint8_t switchPos;


// TACH VARIABLES ===========================================================================

BidirDShotX1 *esc1;
BidirDShotX1 *esc2;
double escThrottle = 0; //sent to esc
uint32_t rpm = 0; //tracking motor speed
bool motorStabilized = false; //flag for motor reaching desired speed
int targetRpm = 0;
int desiredBrakeTime[4] = {off, low, med, high};       // OFF, Low, Med, High. This represents the desired brake time in ms.
int currentThrottle = 0; //tracking braking calculations
unsigned long currentMillis = 0; //this is used to track time after last dart, so when moved to hang it knows time since last
unsigned long PIDMillis = 0;
int brakeIncrement = 0; //post calculation throttle subtraction
bool firstRun = true;
bool idleCap = false;
bool brakeReady = true;
static uint8_t telemetryCounter = 0;
int rpmCap = 100000; //100k rpm cap
uint32_t returnValue = 0;
bool autoMinFire = false; // Variable to ensure at least one dart is fired on full auto mode.

// Solenoid Variables ======================================================================

int dartQueue = 0; //dart cache
int pushState = IDLE;
unsigned long firstTimeStamp = 0; //taken when solenoid start extending
unsigned long FinishedTimeStamp = 0; //taken when fully retracted
unsigned long cycleTime = 0; //how long solenoid took
double delaySolenoid; //calculated time based on dps. 10 dps = 100ms (before closed loop adjustment)
int realDelay;  //actual delay after solenoid loop
bool binaryhold = 0; //binary temp queue

int errorCount = 0;//+1 everytime solenoid doesnt complete a loop aat all or in time
unsigned long timeInPushState = 0; //timing for open loop
int openDelay; //open loop calc

// FUNCTIONS =================================================================================
unsigned long noidStart = 0;
unsigned long noidEnd = 0;

void selfTest(){

  //   -----Solenoid Testing-----
  delay(1000); //wait, ensure everthime else is chill
  Serial.println("noid time");
  digitalWrite(solenoid_mosfet, HIGH); //power solenoid, moving foward
  noidStart = millis(); //start time
  Serial.print("First Timestamp: ");
  Serial.println(noidStart);


  while(digitalRead(frontHall) == HIGH) {} //While front hall effect is NOT triggered, do nothing
  digitalWrite(solenoid_mosfet, LOW); //turn the john off

  while(digitalRead(backHall) == HIGH) {} //do nothing until backhall retriggered
  noidEnd = millis();
  Serial.print("Second Timestamp: ");
  Serial.println(noidEnd);

  cycleTime = noidEnd - noidStart; //time for cold cycle
  Serial.print("Cold Cycle Time: ");
  Serial.print(cycleTime);
  Serial.println("ms");


  Serial.println("starting warm up!"); 
  delay(500);

  for (int i=0; i < 6; i++){
    if(i == 5){ //proper read time after warming up
      delay(50);
      digitalWrite(solenoid_mosfet, HIGH);
      noidStart = millis();
      while(digitalRead(frontHall) == HIGH) {} //While front hall effect is NOT triggered, do nothing
      digitalWrite(solenoid_mosfet, LOW); //turn the john off

      while(digitalRead(backHall) == HIGH) {} //do nothing until backhall retriggered
      noidEnd = millis();
      cycleTime = noidEnd - noidStart; //time for warm cycle
      Serial.print("warm cycle Time: ");
      Serial.print(cycleTime);
      Serial.println("ms");
    }
    else{ //5 warm up cycles
      digitalWrite(solenoid_mosfet, HIGH);
      delay(cycleTime*.75);
      digitalWrite(solenoid_mosfet, LOW);
      delay(cycleTime*.25);
    }


    
  }





  /*
  delay(200);
  //Motor wiring check
  motorspeedSetting = 5;
  //power1 = motorspeedSetting;
  //power2 = motorspeedSetting;
  delay(200);
  Serial.println(rpm);
  delay(200);
  esc1->getTelemetryErpm(&rpm);
  if(rpm > 5000){
    Serial.println("Motor wire pass");
  }
  else{
    errorCode(1);
  }
  //power1 = 0;
  //power2 = 0;
  delay(900);
  motorspeedSetting = 20;
  //power1 = motorspeedSetting;
  //power2 = motorspeedSetting;
  delay(200);
  Serial.println(rpm);
  delay(200);
  esc1->getTelemetryErpm(&rpm);
  if(rpm > 20000){
    Serial.println("Motor spin and speed pass");
  }
  else{
    errorCode(2);
  }
    //power1 = 0;
    //power2 = 0;                                                    
*/






  
}

void errorCode(int errorMajor) {
  //Terminal error handler: Invoked when further operation is unsafe or impossible. Blips out error code on leds
  //Zero flywheel drive throttle to ensure anything that happened during the fault gets shut off

  #ifdef switch
  #undef switch
  #endif
  switch(errorMajor){ //serial error handler
    case 1:
      Serial.println("1: wire error");
      Serial.println("RPM Not Received");
      break;
    case 2:
      Serial.println("2: Spin error");
      Serial.println("Target RPM Not Reached");
      break;
    default:
      Serial.println("unknown error");
      break;
  }

  while (true) {
    digitalWrite(LED_BUILTIN, LOW); //3 second off period
    delay(3000);

    for (int i = 0; i < errorMajor; ++i) { //error type code
      digitalWrite(LED_BUILTIN, HIGH); //1 = wiring error       2 = tach integrity error          3 = spinning error
      delay(700);
      digitalWrite(LED_BUILTIN, LOW);
      delay(700);
    }

    for (int i = 0; i != 3; ++i) { //gap between error codes with 3 quick flashes
      digitalWrite(LED_BUILTIN, HIGH);
      delay(50);
      digitalWrite(LED_BUILTIN, LOW);
      delay(50);
    }
    delay(500);
  } //Loop for eternity, there's nothing more to be done, it's joever.
}

static void manageTrigger(uint8_t btnId, uint8_t btnState){
  static bool wasTriggered = 0; //trig pressed flag
  if(millis() < 4000){  //startupdelay + every small additional delay
    dartQueue = 0;
    return;
  }

  if(btnState == BTN_PRESSED && wasTriggered){ //next here, released
    wasTriggered = 0; //reset trigger pressed flag
    if(modeSetting == BINARY){ //if binary
      dartQueue++; //add 2nd dart
      binaryhold = 0;  //stop the binaryhold thats keeping the system revved
    }else if(modeSetting == AUTO){ //if auto, and the trigger was released, remove all darts from queue
      if(autoMinFire) {                                 // Make sure that at least one dart fires if the trigger is pressed for a moment on full auto.
        dartQueue = 1;
      } else {                                          // Else, shut it off!!
        dartQueue = 0;
        digitalWrite(solenoid_mosfet, LOW);              // make sure solenoid goes low
        pushState = IDLE; 
        currentMillis = millis(); 
      }
    }
  } else if(btnState != BTN_PRESSED && !wasTriggered){ //start here. pressed
    switch (modeSetting){
      case SEMI:  //semi, +1
        dartQueue++;
        binaryhold = 0;
        break;
      case BURST:  //burst +2,3,4,5 depending on burstSetting picked
        dartQueue += burstSetting;
        binaryhold = 0;
        break;
      case AUTO:  //auto, adds a amount higher than any mag to allow for a full dump but will auto stop if something goes wrong
        autoMinFire = true; 
        dartQueue = 60;
        binaryhold = 0;
        break;
      case BINARY: //binary
        dartQueue ++; //add 1 dart to start
        binaryhold = 1; 
        break;
    }
    wasTriggered = 1; //mark that it was pressed
  } 
}
static Button triggerButton(0, manageTrigger); //ID = 0 and this button only corresponds to the manageTrigger function


int delayCalc(int _dps){  //calculates delay for solenoid with error checking
  int delay = (1000 / _dps);
  if(delay < 0){
    return 0;
  }
  else if(errorCount < maxError){ //raw closed value
    return delay;
  }
  else{
    return (delay - solenoidOn - solenoidOff); //adjusted timing if we need to do open loop control
  }
  
}


uint8_t getSwitchPosition() {
 if (!SWITCHREAD_1 && SWITCHREAD_2) {       // Forward
     return FORWARD; 
   } else if (SWITCHREAD_1 && SWITCHREAD_2) {  // Middle
     return MIDDLE; 
   } else if (SWITCHREAD_1 && !SWITCHREAD_2) { // Rear
     return REAR; 
   }
   return 0;
}

int targetRPM(int motorSpeedPercentage){ //relates requested speed to rpm motors should go to
  int RPM = (348 * motorSpeedPercentage);
  return RPM;
}

//if fire is called, motors have been stabilized, and delay has been calculated
void closedFire(){ 
  if(pushState == IDLE && digitalRead(backHall) == LOW){ //state, and solenoid both ready to go
    //Serial.println("trigger pulled");
    digitalWrite(solenoid_mosfet, HIGH); //power solenoid, moving foward
    firstTimeStamp = millis();
    //Serial.print("First Timestamp: ");
    //Serial.println(firstTimeStamp); //start time
    pushState = THRUSTING;
    //Serial.print("Darts Left: ");
    //Serial.println(dartQueue);
  }
  if(pushState == THRUSTING && (millis() - firstTimeStamp) > 200){ //didnt fully extend or took too long to extend
    digitalWrite(solenoid_mosfet, LOW); //emergency stop
    errorCount ++; //log
    Serial.print("error #: ");
    Serial.println(errorCount);
    pushState = IDLE; //solenoid is back to rest
	dartQueue = 0; //stop all action when error received
  }
  if(pushState == THRUSTING && digitalRead(frontHall) == LOW){ //THRUSTING, fully extended, didnt take too long
    pushState = RETRACTING; //immediately move back
    digitalWrite(solenoid_mosfet, LOW);  //triggered? YES, fully extended, turn off solenoid
    //Serial.println("now retracting");
  }
  if(pushState == RETRACTING && (millis() - firstTimeStamp) > 300){ //didnt retract(jam), or back hall sensor failed
    digitalWrite(solenoid_mosfet, LOW); //just incase emergency stop
    errorCount ++;
    Serial.print("error #: "); 
    Serial.println(errorCount);
    pushState = IDLE;
    dartQueue--; //a dart did fire so we make sure to log
  }
  if(pushState == RETRACTING && digitalRead(backHall) == LOW){ //fully retracted
    FinishedTimeStamp = millis();  //solenoid back to rest? YES, take time after
    //Serial.print("Second Timestamp: "); 
    //Serial.println(FinishedTimeStamp);

    cycleTime = FinishedTimeStamp - firstTimeStamp; //time for full cycle
    realDelay = delaySolenoid - cycleTime; 
    if(FinishedTimeStamp + realDelay < millis()){ //actual delay has finished
      pushState = IDLE;
      if(dartQueue > 0){
        //Serial.print(millis());
        //Serial.print("\t");
        //Serial.println(cycleTime);
      }
      dartQueue--;
      dartsFired++;
    }
  }
  if(dartQueue == 0){ //we just subtracted the last dart in queue above, take timestamp for spindown. a new timestamp shouldnt be taken as this wont update until queue is 0 after fire is called again 
    currentMillis = millis(); //BETA here temp for testing, plz move
  }


}

void openFire(){
  if(pushState == IDLE){ //ready to go
      digitalWrite(solenoid_mosfet, HIGH);
      pushState = THRUSTING;
      timeInPushState = millis();
    }
    if(pushState == THRUSTING){     //Pusher is going forward
      if((8 + timeInPushState + solenoidOn + (openDelay/2)) < millis()) {  //If it's all the way forward retract it
        digitalWrite(solenoid_mosfet, LOW);
        pushState = RETRACTING;
        timeInPushState = millis();
      }
    }
    if(pushState == RETRACTING){    //Pusher is coming back
      if((timeInPushState + solenoidOff + (openDelay/2)) < millis()){ //Checking if pusher is all the way back now, also use this timer to track shot delay for DPS limiting so we don't compromise the first shot
        dartQueue--;                  //We have fired One(1) dart, count it
        dartsFired++;
        pushState = IDLE;           //Go back to idle and let the main loop sort out whether we need to fire another dart. Technically might introduce a few microseconds of delay but the testing done on the solenoid timings should cancel this out so whatever
        timeInPushState = millis(); 
      }
    }

    if(dartQueue == 0){ //we just subtracted the last dart in queue above, take timestamp for spindown. a new timestamp shouldnt be taken as this wont update until queue is 0 after fire is called again 
      currentMillis = millis(); //BETA here temp for testing, plz move
    }
}



void setup(){
  EEPROM.begin(21);  
  Serial.begin(115200);   //debugging
  display_init();
  while(!Serial){} //wait for serial
  Serial.println("Starting setup... \n "); 


  //how each pin should be treated
  pinMode(trigger, INPUT_PULLUP);  
  pinMode(switch_pin_1, INPUT_PULLUP);
  pinMode(switch_pin_2, INPUT_PULLUP);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(backHall, INPUT_PULLUP);
  pinMode(frontHall, INPUT_PULLUP);
  pinMode(solenoid_mosfet, OUTPUT);
  pinMode(clockPin, INPUT); //rotoray encoder setup
  pinMode(dtPin, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);  //for debugging
  triggerButton.setPushDebounceInterval(5);  //Intervals which are used to measure physical debouncing of trigger contacts
  triggerButton.setReleaseDebounceInterval(5);

  switchPos = getSwitchPosition(); //check switch pos
  switchPosPrev = switchPos; //update
  loadvalues(switchPos);  //load values based on current switch pos
  delay(100);
  //display_init(); BETA
  Serial.println(" ");
  Serial.println("Pins set, screen initialized, values loaded");


  Serial.println("Loading menu and eeprom...");
  lastStateCLK = digitalRead(clockPin);
  openDelay = delayCalc(openDPS); //needed for open loop timing
  Serial.print(menuState); 
  Serial.println(":  Started from setup");
  for(int i = 1; i < 22; i++) {
    Serial.print(EEPROM.read(i)); 
    Serial.print(",");
    //mainScreen(); BETA
  }
  Serial.println(" ");
  Serial.println("Setup complete");
  Serial.print("\n\n\n");
  currentMillis = millis(); // Initial reading.
}
void setup1(){
  esc1 = new BidirDShotX1(escPin1, 600);
  esc2 = new BidirDShotX1(escPin2, 600);
  Serial.println("esc setup");
}

double integral, previous = 0;
bool PIDRun;

void loop() {

  if( firstRun == true){ 
    delay(1000);
    if(digitalRead(trigger) == HIGH){
      Serial.println("self testing...");
      selfTestScreen();
      selfTest();
    }
    //selfTest(); //BETA change this so that it only self tests when boot with trigger pull or smth
    Serial.println("finished self testing");
    //speed = motorspeedSetting; //after testing, set esc to current mode speed
    Serial.print("Now loading new speed: ");
    Serial.println(motorspeedSetting);
    Serial.print("mode #: ");
    Serial.println(modeSetting);
    firstRun = false; //set flag
    Serial.println("first run done");
    mainScreen();
  }

  /* 
  BETA removed because no battery yet
  (voltageRead() > 13.5) ? mainScreen() : lowbatteryScreen();
  */ 

  if (!BUTTONHIGH && escThrottle == 0 && pushState == IDLE) {                   // If encoder button is pressed, ground signal sent, also ensure motor and solenoid are at rest.
    settingsMenu();                    // Break to settings menu.  
    if(menuState == "Save") { savevalues(counter); }
    menuState = "Main Menu";           // When done with settings menu, update menuState to reflect going back to Main Menu.
    switchPos = getSwitchPosition();
    loadvalues(switchPos);
    mainScreen(); 
    return; 
  }

  // Main operation ------------------------------------------------------------------------                             // Load current values from persistent memory.
  triggerButton.update(digitalRead(trigger));  // Check for trigger state change.

 
  //Serial.print("Dart Queue: ");
  //Serial.println(dartQueue);
  //Serial.print("stab: ");
  //Serial.println(motorStabilized);
  //Serial.println(motorState);
  //Serial.println(newTimeStamp);
  //Serial.print("dps: ");
  //Serial.println();
  
  //Serial.print("hangtimeSetting: ");
  //Serial.println(hangtimeSetting);


  if(dartQueue > 0) {  //darts in queue, move to main firing actions
    delaySolenoid = delayCalc(dpsSetting);  //calculate delay with previously loaded values
    targetRpm = targetRPM(motorspeedSetting); //target for motors
    PIDRun = true;
    if(motorStabilized) {
      autoMinFire = false;
      if(errorCount < maxError){
        closedFire();
      }
      else{
        openFire();
      }
    }
  }
  else { // Spindown
    if((currentMillis + hangtimeSetting - 300) < millis() && (modeSetting != BINARY || binaryhold == 0) && (menuState == "Main Menu")) {
    PIDRun = false; 
    switchPos = getSwitchPosition();
     if(switchPos != switchPosPrev) {
       loadvalues(switchPos); 
       mainScreen();
       brakeReady = true;
       idleCap = false;
    }
    switchPosPrev = switchPos;
     if(dartsFiredPrev != dartsFired) {
       updateAmmoCounter(dartsFired, dartsFiredPrev);
       dartsFiredPrev = dartsFired; 
     }
    }
  }
}

double actual;
double error; 
double proportional;
double derivative;  

double dt;
double last_time = 0;
double kp = 0.05;
double ki = 0.001;
double kd = 0.08;

uint32_t rpmRaw = 0;
uint32_t rpmFilter = 0;
const uint8_t EMAFilter = 2;
constexpr static uint32_t half = uint32_t{1} << (EMAFilter - 1);

void loop1() { //motor core, should be core1 in main code
	delayMicroseconds(200);

  PIDMillis = micros(); 
  dt = (PIDMillis - last_time) / 1000;
  last_time = PIDMillis;

	esc1->getTelemetryErpm(&rpmRaw);
	rpmRaw /= MOTOR_POLES / 2; // eRPM = RPM * poles/2

  if( rpmRaw * 1000 > (3200 * 16800)){ //motor kV * mV
    rpmRaw = rpm;
  }
  rpmFilter += rpmRaw;
  rpm = (rpmFilter + half) >> EMAFilter; //1st-order exponential moving average
  rpmFilter -= rpm;


  if(PIDRun) {
  actual = rpm;
  error = targetRpm - actual;

  proportional = error;
  integral += error * dt;
  derivative = (error - previous) / dt;
  previous = error;
  if(targetRpm == 0) {
    escThrottle  = 0;
  } else {
  escThrottle  = (kp * proportional) + (ki * integral) + (kd * derivative);
  brakeReady = true;
  idleCap = false;
  }

  } else {

    if(brakeReady == true) {
      currentThrottle = rpm / 29;
      escThrottle = currentThrottle;  // Set escThrottle to whatever RPM we are currently at.
      brakeReady = false;
    }

    if(escThrottle >= 1 && idleCap == false) { //start braking
      if(escThrottle <= (idleSetting * 12) + (2 * abs(round((currentThrottle - idleSetting * 20) / (desiredBrakeTime[brakeSetting] /7.2))))){ //calc to smooth out lower end stutter
        escThrottle = (idleSetting * 12);
        idleCap = true;
      }
      delayMicroseconds(7000); //decrement every 7ms
      brakeIncrement = round((currentThrottle - idleSetting * 12) / (desiredBrakeTime[brakeSetting] / 7.2)); //linear match for brake amount to speed to keep in time
      if(brakeIncrement < 1 || desiredBrakeTime[brakeSetting] == off) {                 //for lower throttles at higher times, or if brakeSetting is off
        brakeIncrement = 1;
      }
      //Serial.println(brakeIncrement); 
      escThrottle -= brakeIncrement;  
      if(escThrottle < 0) {escThrottle = 0;}
    }
  }

  esc1->sendThrottle(max(0, min(1999, static_cast<int32_t>(escThrottle)))); //main thing that turns on motor

    if(escThrottle > 0) {
    Serial.print(PIDMillis);
    Serial.print("\t");
    Serial.print(dt);
    Serial.print("\t");
    Serial.print(rpm);
    Serial.print("\t");
    Serial.println(escThrottle);
    /*Serial.print("\t");
    Serial.print(dt);
    Serial.print("\t");
    Serial.println(targetRpm);*/
    }


  if(targetRpm > 0 && rpm >= (targetRpm - 150)){
    //Serial.println("HELLO WE ARE STABALIZED 哇");
    motorStabilized = true;
  }
  else if(binaryhold == 0 && motorState != "hang"){
    motorStabilized = false;
  }


  //Serial.println(escThrottle);
  if(motorStabilized == 1){
    digitalWrite(LED_BUILTIN, HIGH);
  }
  else{
    digitalWrite(LED_BUILTIN, LOW);
  }
}
