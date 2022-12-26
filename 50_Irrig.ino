

// ============================== Irrig ====================================================
// Statemachine to control the irrigation program

//============================== known issues ===============================================

// @ power up or after reset it shall turn to auto wait or remain in idle depending on thHumid_SoilHumidPercente status before power down / reset
// string deletion function at the bottom should be moved to some central module
// new lawn function (repeating of multiple irigation cycles within one day) to be implemented
// 8 days irrigation history indicator to be implemented



// =====================definitions Irrig ==========================
// --------------text sources---------------------------------------
 
const char Irrig_StrStatus[] = "status: ";
const char Irrig_StrOff[] = "off";
const char Irrig_StrAutoWait[] = "auto";
const char Irrig_StrAutoIrrig[] = "irrigation in progress";
const char Irrig_StrPause[] = "pause";
const char Irrig_StrManual[] = "manual";

int Irrig_Durations[8] = {0, 0, 0, 0, 0, 0, 0, 0};                 //durations as
int Irrig_AutoValvePoint = 0;
int Irrig_suspendHours = 0;                                        // number of hours to suspend irrigation
int Irrig_PauseTimeoutCtr;                                      //used to time out the pause states AutoPause and SingleCPause
#define IRRIG_AUTO_PAUSE_TIMEOUT_TIME 1800 //seconds            //dto.
#define IRRIG_REPET_DUTY_CYC  2   //hours                       // repeat irrigation cycle every two hours (new lawn)

//===================== public global variables provided by Irrig=================

char Irrig_ModeDisplayStringHeadl[IRRIG_MODEDISPLSTRINGHEADLENGTH];             //headline to be displayed to user interface in State ComTelnet "Mode"
char Irrig_ModeDisplayStringDetail[IRRIG_MODEDISPLSTRINGDET];                   //details to be displayed to user interface in State ComTelnet "Mode"
byte Irrig_ActiveValvePattern = 0;                                              // lsb: true = valve 1 active, ...msb: true = valve 8 active
byte Irrig_HumidHistory[8] = {0,0,0,0,0,0,0,0};                                 // stores the soil moisture at the time the cycle (would) start(s)
byte Irrig_IrrigHistory = 0;                                                    // stores whether a automatic cycle was triggered (1 bit per day)
byte Irrig_HistoryPtr = 0;                                                      // indicates at which position today's value is stored

//===================== global variables used from other modules===============================

extern byte GetTime_Mez[3];           // day time {h, m, s}
extern char ComTel_ModeCommand;       // command character from user interface ComTelnet updated in state "Mode"  
extern byte ComTel_SetupData[COMTEL_SETDATAARRSIZ]; // irrigation parameters from user interface / EEPROM   
extern int Humid_SoilHumidPercent;  // soil humidity and signal qualifier                                        


//======================= statemachine definitions===============================unsigned long GetTime_sendNTPpacket(IPAddress& address)  //Send a standard NTP request

//define instances of Stm_StateFcnsPtr
//    type             instance name           adresses of the functions of the stm states
const Stm_StateFcnsPtr Irrig_StmPIdleFcns =         { &Irrig_StmIdleCondExit,       &Irrig_StmIdleEntr,     &Irrig_StmIdleAct };
const Stm_StateFcnsPtr Irrig_StmPAutoWaitFcns =   { &Irrig_StmAutoWaitCondExit,   &Irrig_StmAutoWaitEntr, &Irrig_StmAutoWaitAct };
const Stm_StateFcnsPtr Irrig_StmPManualFcns =       { &Irrig_StmManualCondExit,   &Irrig_StmManualEntr,   &Irrig_StmManualAct };
const Stm_StateFcnsPtr Irrig_StmPAutoIrrigFcns =  { &Irrig_StmAutoIrrigCondExit,  &Irrig_StmAutoIrrigEntr, &Irrig_StmAutoIrrigAct };
const Stm_StateFcnsPtr Irrig_StmPAutoPauseFcns =  { &Irrig_StmAutoPauseCondExit,  &Irrig_StmAutoPauseEntr, &Irrig_StmAutoPauseAct };
const Stm_StateFcnsPtr Irrig_StmPSingleCycleFcns =  { &Irrig_StmSingleCycleCondExit,  &Irrig_StmSingleCycleEntr, &Irrig_StmSingleCycleAct };
const Stm_StateFcnsPtr Irrig_StmPSingleCPauseFcns =  { &Irrig_StmSingleCPauseCondExit,  &Irrig_StmSingleCPauseEntr, &Irrig_StmSingleCPauseAct };

// new state? just add the definition of the pointer struct accordingly

Stm_StateFcnsPtr *Irrig_StmP2Fcns = &Irrig_StmPIdleFcns; // define Pointer to struct of stm functions and initialize with the struct of the "Idle" state; 
                                                           // result: Irrig_StmP2Fcns ---> { &Irrig_StmIdleCondExit, &Irrig_StmIdleEntr, &Irrig_StmIdleAct }



//====================== Initializations (to be included into void(setup) ============================================================================
//Public
void Irrig_init() {
 // Irrig_StmIdleEntr();                    //not done here because it would overwrite the EEPROM value of the state before power down
}

//======================Loop================================================================================================

//=================================Irrig Statemachine======================================================================

//------------------------------core Stm--------------------------------------------
//Public
void Irrig_Stm_1s() {  //to be attached to the loop
  static boolean latch;
  //beginning of stm
  if (Irrig_StmP2Fcns->PCondExit() == true) {  //call of condition+exit code of current state; just to remember it later ...: this calls the fcn AND checks whether fcn's return == true 
                                                //the fcn returns if it carried out a transition to another state; if so ComTel_StmP2Fcns points to the fcns of the new state now
    Irrig_StmP2Fcns->PEntr();                  // call the entry code of the new state
  }
  Irrig_StmP2Fcns->PAct();                     // call the action code of current or new state
  //End of stm
  //calculate history pointer
  if (GetTime_Mez[0] == DIAG_TIMEHRTRIGCYC && GetTime_Mez[1]== DIAG_TIMEMINTRIGCYC) {              //increment history pointer at midnight
    if (!latch) {
      latch = true;
      Irrig_HistoryPtr ++;
      if (Irrig_HistoryPtr > 7) {
        Irrig_HistoryPtr = 0;
      }
      bitClear(Irrig_IrrigHistory, Irrig_HistoryPtr);    // clear the irrig history flag @ today's position
      Irrig_HumidHistory[Irrig_HistoryPtr] = Humid_SoilHumidPercent; // update the humidity value at midnight; note that this will be overwritten if a auto cycle is started; see Irrig_StmAutoWaitCondExit 
    }
  }
  else {
    latch = false;
  }
#ifdef DEBUG_IRRIG_MAX
  Serial.println(Irrig_ActiveValvePattern, BIN);
#endif 
}

//Private (all down form here)
// -------------------------------State "Idle" -----------------------------------------
// assumed after power on through initialization of Irrig_StmP2Fcns

void Irrig_StmIdleEntr() {                                                                // Idle entry code is not executed when coming from power on
  
//  FcnsDeleteString(Irrig_ModeDisplayStringHeadl, IRRIG_MODEDISPLSTRINGHEADLENGTH);                                 
//  strcat(Irrig_ModeDisplayStringHeadl, Irrig_StrStatus);
//  strcat(Irrig_ModeDisplayStringHeadl, Irrig_StrOff);                                      // string Irrig_ModeDisplayStringHeadl: "status: off"
//  FcnsDeleteString(Irrig_ModeDisplayStringDetail, IRRIG_MODEDISPLSTRINGDET); 
//  Irrig_AutoValvePoint = 0;                                                                // set the valve index to start value
  
  EEPROM.update(EEPROM_AutoBeforePowerOff, (byte) false);                       //memorize in EEPROM that it was switched to off  
}


boolean Irrig_StmIdleCondExit() {              // returns true if a transition is triggered
 boolean Trans2AutoFromPowerOffDone = false; 
 switch (ComTel_ModeCommand) {
  case 'a':
    Irrig_StmP2Fcns = &Irrig_StmPAutoWaitFcns;    // transition will is taken after power on depending on EEPROM value of ComTel_ModeCommand
    ComTel_ModeCommand = 0;
    EEPROM.update(EEPROM_AutoBeforePowerOff, (byte) true);    //memorize in EEPROM that it was switched to auto           
    return true;
    break;
  case 't':                                     // go directly to the irrigation cycle, but with the same test duration for all valves
    for (int i = 0; i < 8; i ++) {
      Irrig_Durations[i] = (int) ComTel_SetupData[8] * 60;   // load test duration from user interface // EEPROM into the counters
    }
    Irrig_StmP2Fcns = &Irrig_StmPSingleCycleFcns;
    ComTel_ModeCommand = 0;
    return true;
    break;
  case 'm':                                     // go to manual operation of the valves (service purpose)
    Irrig_StmP2Fcns = &Irrig_StmPManualFcns;   
    ComTel_ModeCommand = 0;  
    return true;
    break;
  case 'd':                                     // go directly to the irrigation cycle with duration parameters from user interface / EEPROM
    for (int i = 0; i < 8; i ++) {
      Irrig_Durations[i] = (int) ComTel_SetupData[i] * 60;   // load durations from user interface // EEPROM into the counters and convert them from min to s
    }
    Irrig_StmP2Fcns = &Irrig_StmPSingleCycleFcns;  
    ComTel_ModeCommand = 0;  
    return true;
    break;
  default:
    if ((boolean) EEPROM.read(EEPROM_AutoBeforePowerOff) && !Trans2AutoFromPowerOffDone) {
       Trans2AutoFromPowerOffDone = true;
       Irrig_StmP2Fcns = &Irrig_StmPAutoWaitFcns;    // transition will is taken after power on depending on EEPROM value of ComTel_ModeCommand
       return true;
    }
    else {
       return false;
    } 
    break;
    } 
}

void Irrig_StmIdleAct() {  
  FcnsDeleteString(Irrig_ModeDisplayStringHeadl, IRRIG_MODEDISPLSTRINGHEADLENGTH);                                 
  strcat(Irrig_ModeDisplayStringHeadl, Irrig_StrStatus);
  strcat(Irrig_ModeDisplayStringHeadl, Irrig_StrOff);                                      // string Irrig_ModeDisplayStringHeadl: "status: off"
  FcnsDeleteString(Irrig_ModeDisplayStringDetail, IRRIG_MODEDISPLSTRINGDET); 
  Irrig_AutoValvePoint = 0; 
}
// --------------------------State "AutoWait"------------------------------------------------------------------
void Irrig_StmAutoWaitEntr() {                                    
 FcnsDeleteString(Irrig_ModeDisplayStringHeadl, IRRIG_MODEDISPLSTRINGHEADLENGTH);                                 
  strcat(Irrig_ModeDisplayStringHeadl, Irrig_StrStatus);
  strcat(Irrig_ModeDisplayStringHeadl, Irrig_StrAutoWait);   
#ifdef DEBUG_IRRIG_BASIC
  Serial.println(F("AutoWaitEntr")); 
#endif
  Irrig_AutoValvePoint = 0;                                         // needed in case AutoWait was entered from Pause through the time out transition  
}

boolean Irrig_StmAutoWaitCondExit() {  
  static boolean repeatCycle;
  static boolean timeCond;
  static boolean firstCycle;
  static int cycleRepeatNo;
//start conditions for irrigation cycle: 1. condition for repetition of a cycle
  if (cycleRepeatNo > 0) {   //never true for a first cycle as the cycleRepeatNo is only loaded from user settings during first cycle
    repeatCycle = (((int)ComTel_SetupData[enum_startTime] + ((int)ComTel_SetupData[enum_repet] - cycleRepeatNo + 1) * IRRIG_REPET_DUTY_CYC)%24) == (int)GetTime_Mez[0] && GetTime_Mez[1] == 0;
  }
  else {
    repeatCycle = false;
  }                                      //2. condition for starting the first cycle
  timeCond = GetTime_Mez[0] == ComTel_SetupData[enum_startTime] && GetTime_Mez[1] == 0;  //time condition to start the first cycle (def.: cycle := uninterrupted sequence of valve actuations) 
  if (timeCond) {
    Irrig_HumidHistory[Irrig_HistoryPtr] = Humid_SoilHumidPercent; 
  }
  firstCycle = Irrig_suspendHours == 0 && timeCond && ComTel_SetupData[enum_humid] > Humid_SoilHumidPercent 
                || ComTel_ModeCommand == 'd'; // start irrigation cycle if suspend hours are elapsed, time is right (do this within the first minute of the hour only in order to avoid multiple stars) and if current soil humidity is below setpoint 
                                              //or if and a[d] hoc cycle is commanded
  if (firstCycle) {
    cycleRepeatNo = ComTel_SetupData[enum_repet]; // get from user settings: how often the cycle shall be repeated additionaly to the first cycle(use for new lawn)
    bitSet(Irrig_IrrigHistory, Irrig_HistoryPtr); // set irrigation flag @ today's position 
  }
#ifdef DEBUG_IRRIG_MAX
Serial.println();
Serial.print(F("SetuDataRepet "));
Serial.println(ComTel_SetupData[enum_repet]);
Serial.print(F("local repeat counter "));
Serial.println(cycleRepeatNo);
Serial.print(F("first c "));
Serial.println(firstCycle);
Serial.print(F("repeatC "));
Serial.println(repeatCycle);
#endif
  if (ComTel_ModeCommand == 'o'){
    repeatCycle = false;
    firstCycle = false;
    cycleRepeatNo = 0;
    Irrig_StmP2Fcns = &Irrig_StmPIdleFcns;    // go to idle in case of [o]ff command
    ComTel_ModeCommand = 0;
    return true;
  }
  else if (firstCycle || repeatCycle) {   
    for (int i = 0; i < 8; i ++) {
      Irrig_Durations[i] = (int) ComTel_SetupData[i] * 60;   // load parameters from user interface // EEPROM into the counters
    }
    if (cycleRepeatNo > 0 && repeatCycle) {
      cycleRepeatNo --;
    }
    Irrig_StmP2Fcns = &Irrig_StmPAutoIrrigFcns;   // go to AutoIrrig in case time and humidity condition is true 
    ComTel_ModeCommand = 0;
    return true; 
  }
  else if (ComTel_ModeCommand == 't'){    // go to AutoIrrig in case [t]est was commanded
    for (int i = 0; i < 8; i ++) {
      Irrig_Durations[i] = (int) ComTel_SetupData[8] * 60;   // load parameters from user interface // EEPROM into the counters
    }
    repeatCycle = false;
    firstCycle = false;
    cycleRepeatNo = 0;
    Irrig_StmP2Fcns = &Irrig_StmPAutoIrrigFcns;   
    ComTel_ModeCommand = 0;
    return true;
  }    
  else {
    return false; 
  }

}

void Irrig_StmAutoWaitAct() { 
  static byte previousHour;
  char buf[3];
  if(ComTel_ModeCommand == 's'){
    if (Irrig_suspendHours < 76){
      Irrig_suspendHours = Irrig_suspendHours + 24;
    }    
    ComTel_ModeCommand = 0;
  }
  else if (ComTel_ModeCommand == 'u') {
    Irrig_suspendHours = max(0, Irrig_suspendHours - 24);
    ComTel_ModeCommand = 0;
  }
  else if (Irrig_suspendHours > 0){
    if (GetTime_Mez[0] != previousHour) {
      Irrig_suspendHours --;
      previousHour = GetTime_Mez[0];
    }
  }
  FcnsDeleteString(Irrig_ModeDisplayStringDetail, IRRIG_MODEDISPLSTRINGDET);                                 
  strcat(Irrig_ModeDisplayStringDetail, "susp h ");
  itoa(Irrig_suspendHours, buf, 10);
  strcat(Irrig_ModeDisplayStringDetail, buf);
  strcat(Irrig_ModeDisplayStringDetail, " hum ");
  itoa(Humid_SoilHumidPercent, buf, 10);
  strcat(Irrig_ModeDisplayStringDetail, buf);
  
}

//--------------------------State "AutoIrrig"-------------------------------------------------------------------
void Irrig_StmAutoIrrigEntr() { 
  FcnsDeleteString(Irrig_ModeDisplayStringHeadl, IRRIG_MODEDISPLSTRINGHEADLENGTH);                                 
  strcat(Irrig_ModeDisplayStringHeadl, Irrig_StrStatus);
  strcat(Irrig_ModeDisplayStringHeadl, Irrig_StrAutoIrrig);                                                                    
}

boolean Irrig_StmAutoIrrigCondExit() {  
  if (ComTel_ModeCommand == 'o'){
    Irrig_ActiveValvePattern = 0;             //switch off valves
    Irrig_StmP2Fcns = &Irrig_StmPIdleFcns;    // go to idle in case of [o]ff command
    ComTel_ModeCommand = 0;
    return true;
  }
  else if (Irrig_AutoValvePoint > 7){         //irrigation cycle finished
    Irrig_AutoValvePoint = 0;                 // prepare valve pointer for next cycle
    Irrig_ActiveValvePattern = 0;    
    Irrig_StmP2Fcns = &Irrig_StmPAutoWaitFcns; 
    return true;
  }
  else if (ComTel_ModeCommand == 'p'){
    Irrig_ActiveValvePattern = 0;    
    Irrig_StmP2Fcns = &Irrig_StmPAutoPauseFcns; 
    ComTel_ModeCommand = 0;
    return true;
  }
  else {
    return false; 
  } 
}

void Irrig_StmAutoIrrigAct() {
  Irrig_ValveSched();                       //used in SingleCycle as well
}

//----------------------- State "AutoPause"---------------------------------------------------------------------------
void Irrig_StmAutoPauseEntr() {                                   
  FcnsDeleteString(Irrig_ModeDisplayStringHeadl, IRRIG_MODEDISPLSTRINGHEADLENGTH);                                 
  strcat(Irrig_ModeDisplayStringHeadl, Irrig_StrStatus);
  strcat(Irrig_ModeDisplayStringHeadl, Irrig_StrPause);  
  Irrig_PauseTimeoutCtr = IRRIG_AUTO_PAUSE_TIMEOUT_TIME;        //initialize timeout counter 
}

boolean Irrig_StmAutoPauseCondExit() {  
  if (ComTel_ModeCommand == 'r'){
    Irrig_StmP2Fcns = &Irrig_StmPAutoIrrigFcns;   
    ComTel_ModeCommand = 0; 
    return true;
  }
  else if (Irrig_PauseTimeoutCtr == 0) {
    Irrig_StmP2Fcns = &Irrig_StmPAutoWaitFcns;    
    return true;
  }
  else {
    return false;
  }  
}

void Irrig_StmAutoPauseAct() { 
  if (Irrig_PauseTimeoutCtr > 0){
    Irrig_PauseTimeoutCtr --;
  } 
}
//--------------------------State "SingleCycle"-------------------------------------------------------------------
void Irrig_StmSingleCycleEntr() {  
  FcnsDeleteString(Irrig_ModeDisplayStringHeadl, IRRIG_MODEDISPLSTRINGHEADLENGTH);                                 
  strcat(Irrig_ModeDisplayStringHeadl, Irrig_StrStatus);
  strcat(Irrig_ModeDisplayStringHeadl, Irrig_StrAutoIrrig);        
}

boolean Irrig_StmSingleCycleCondExit() {  
  if (ComTel_ModeCommand == 'o'){
    Irrig_ActiveValvePattern = 0;             //switch off valves
    Irrig_StmP2Fcns = &Irrig_StmPIdleFcns;    // go to idle in case of [o]ff command
    ComTel_ModeCommand = 0;
    return true;
  }
  else if (Irrig_AutoValvePoint > 7){         //irrigation cycle finished
    Irrig_AutoValvePoint = 0;                 // prepare valve pointer for next cycle
    Irrig_ActiveValvePattern = 0;    
    Irrig_StmP2Fcns = &Irrig_StmPIdleFcns;
    return true;
  }
  else if (ComTel_ModeCommand == 'p'){
    Irrig_ActiveValvePattern = 0;    
    Irrig_StmP2Fcns = &Irrig_StmPSingleCPauseFcns; 
    ComTel_ModeCommand = 0;
    return true;
  }
  else {
    return false; 
  } 
}

void Irrig_StmSingleCycleAct() {
 Irrig_ValveSched();
}

// -------------------------State "SingleCPause"----------------------------------------------------------------------
void Irrig_StmSingleCPauseEntr() { 
    FcnsDeleteString(Irrig_ModeDisplayStringHeadl, IRRIG_MODEDISPLSTRINGHEADLENGTH);                                 
  strcat(Irrig_ModeDisplayStringHeadl, Irrig_StrStatus);
  strcat(Irrig_ModeDisplayStringHeadl, Irrig_StrPause);  
}

boolean Irrig_StmSingleCPauseCondExit() {  
  if (ComTel_ModeCommand == 'r'){
    Irrig_StmP2Fcns = &Irrig_StmPSingleCycleFcns;   
    ComTel_ModeCommand = 0; 
    return true;
  }
  else {
    return false;
  } 
}

void Irrig_StmSingleCPauseAct() {
 

}
//------------------------- State "Manual"-----------------------------------------------------------------------
void Irrig_StmManualEntr() {                                
  FcnsDeleteString(Irrig_ModeDisplayStringHeadl, IRRIG_MODEDISPLSTRINGHEADLENGTH);              // prepare string "headline" in status manual                     
  strcat(Irrig_ModeDisplayStringHeadl, Irrig_StrStatus);
  strcat(Irrig_ModeDisplayStringHeadl, Irrig_StrManual);  
}

boolean Irrig_StmManualCondExit() {  
  if (ComTel_ModeCommand == 'o'){
    Irrig_StmP2Fcns = &Irrig_StmPIdleFcns;    // go to idle in case of [o]ff command
    ComTel_ModeCommand = 0;
    Irrig_ActiveValvePattern = 0;             // switch off all valves
    return true;
  }
  else {
    return false;
  }
  
}

void Irrig_StmManualAct() { 
  char buf[9];
#ifdef DEBUG_IRRIG_BASIC
Serial.print(F("Command "));
Serial.println(ComTel_ModeCommand);
Serial.println(Irrig_ActiveValvePattern); 
#endif  
  switch (ComTel_ModeCommand) {
    case '1':
      Irrig_ActiveValvePattern = Irrig_ActiveValvePattern | B00000001;
      ComTel_ModeCommand = 0;
      break;
    case '2':
      Irrig_ActiveValvePattern = Irrig_ActiveValvePattern | B00000001 << 1;
      ComTel_ModeCommand = 0;
      break;
    case '3':
      Irrig_ActiveValvePattern = Irrig_ActiveValvePattern | B00000001 << 2;
      ComTel_ModeCommand = 0;
      break;
    case '4':
      Irrig_ActiveValvePattern = Irrig_ActiveValvePattern | B00000001 << 3;
      ComTel_ModeCommand = 0;
      break;
    case '5':
      Irrig_ActiveValvePattern = Irrig_ActiveValvePattern | B00000001 << 4;
      ComTel_ModeCommand = 0;
      break;
    case '6':
      Irrig_ActiveValvePattern = Irrig_ActiveValvePattern | B00000001 << 5;
      ComTel_ModeCommand = 0;
      break;
    case '7':
      Irrig_ActiveValvePattern = Irrig_ActiveValvePattern | B00000001 << 6;
      ComTel_ModeCommand = 0;
      break;
    case '8':
      Irrig_ActiveValvePattern = Irrig_ActiveValvePattern | B00000001 << 7;
      ComTel_ModeCommand = 0;
      break;
    default:
       break;
  }
  FcnsDeleteString(Irrig_ModeDisplayStringDetail, IRRIG_MODEDISPLSTRINGDET);    // prepare string "details" for status displayed in "manual"                             
  strcat(Irrig_ModeDisplayStringDetail, "valves: ");
  itoa(Irrig_ActiveValvePattern, buf, 2);
  strcat(Irrig_ModeDisplayStringDetail, buf);
  
}

//======================== Functions=====================================================================================

//-------------------------------------Irrig_ValveSched()----------------------------------------------------------------
// executes the irrigagion schedule according the durations given in Irrig_Durations[] and the valve indicator Irrig_AutoValvePoint; 
void Irrig_ValveSched() { 
  char buf[5];
#ifdef DEBUG_IRRIG_MAX
  Serial.print(F("valve "));
  Serial.println(Irrig_AutoValvePoint);
#endif  
  if (Irrig_Durations[Irrig_AutoValvePoint] == 0 && Irrig_AutoValvePoint < 8){      // irrigation time of valve x elapsed --> goto next valve
    Irrig_AutoValvePoint ++;                                                        // this is not a static here because it is used in the condition codes for determining the end of the cylce
  }
  else {
   Irrig_ActiveValvePattern = B00000001 << Irrig_AutoValvePoint;
   Irrig_Durations[Irrig_AutoValvePoint] --;
  } 
  FcnsDeleteString(Irrig_ModeDisplayStringDetail, IRRIG_MODEDISPLSTRINGDET);    // prepare string for status displayed in "mode"                             
  strcat(Irrig_ModeDisplayStringDetail, "V");
  itoa(Irrig_AutoValvePoint + 1, buf, 10);
  strcat(Irrig_ModeDisplayStringDetail, buf);
  strcat(Irrig_ModeDisplayStringDetail, " ");
  itoa(Irrig_Durations[Irrig_AutoValvePoint], buf, 10);
  strcat(Irrig_ModeDisplayStringDetail, buf);  
}

//---------------------------------------FcnsDeletString-----------------------------------------------------------------
// clears a char array; caution when using sizeof() for lengthcharArray: it yields the number of bytes, not the size of the array. Use it like this: sizof(arrayx)/sizeof(arrayx[0])

void FcnsDeleteString(char* charArray, int lengthcharArray) {
  for (int i; i < lengthcharArray; i ++ ) {
    charArray[i] = 0; 
  } 
}
