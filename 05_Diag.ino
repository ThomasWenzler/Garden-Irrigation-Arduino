// ============================== Diag ====================================================
// 1) provision of diagnosis infrastructure functions: 
//    - Definition of an array of pointers Diag_dfcs[] that point to all diagnostic fault codes (DFC) of the system; it is used by infrastructure fcns in order to enable loop computing 
//    - Diag_ClearFaultMemory() clears all fault flags and fault counters of the system's DFCs
//    - Diag_TrigCycle() defines the diagnostic cycle of 1d and takes action at the time a new cycle starts
//    - Diag_PrintFaultMemory() prints the fault memory over network or serial bus
//    - Diag_DebounceUpDown() for debouncing of faults 
// 2) Plausibility diagnosis for power up / reset number
//    - The DFC indicates whether a unreasonable number of resets or power up cycles happened within one diagnostic cycle
//    - see Diag_init(), Diag_TrigCycle (architecture is not so good here)
// 3) Watch dog and functional monitoring
//    - purpose: avoid uncontrolled spoilage of water in case of random defects or sw bugs with random failure occurance; 
//    - purpose is not: functional safety; see Hazard and Risk assessment in the design document
//    - enables the periferal HW watch dog in the µC and triggers it periodically from the last function called in the 1s periodic task
//    - (not implemented) monitors the correct execution order and frequency of the periodic functions and sets the safe state if a fault is detected
//    - monitors the sum of irrigation time, checks it against the parameters and sets the safe state if a fault is detected
//    - controls a digital out pin for the relay which controls the primary side of the 24V supply of the valves 

//============================== known issues ===============================================
// is the healed flag really needed?

//===================== global variables used from other modules===============================
extern dfc GetTime_DfcPlaus;
extern dfc Humid_DfcScg; 
extern dfc Humid_DfcScb;
extern byte GetTime_Mez[3];           // day time {h, m, s}
extern byte ComTel_SetupData[COMTEL_SETDATAARRSIZ]; // irrigation parameters from user interface / EEPROM 
extern byte Irrig_ActiveValvePattern;   
extern EthernetClient ComTel_client;
#ifdef DEBUG_COMTELNET_BASIC
extern int ComTel_DebugNumberClientConn;
#endif

// =====================definitions Diag ==========================
#define DIAG_TIMEHRTRIGCYC 0       // hh of hh.mm at which the diag cycle gets triggered
#define DIAG_TIMEMINTRIGCYC 0      // mm of hh.mm at which the diag cycle gets triggered
                              // so with this setting it triggers at midnight
#define DIAG_LENGTHDFCLABEL 11   //max. number of characters of a diagnosis label (clear text for what is under diagnose) = DIAG_LENGTHDFCLABEL - 1    
#define DIAG_DFCPLAUS_POWUP_THRESH 1  //threshold for debouncing the numbers of power ups per cycle (=1d)  
#define DIAG_DFCMON_IRRTIME_THRESH 20 //threshold in s for debouncing unplausible total irrigation time   
#define DIAG_MAX_IRR_TIME_PER_DAY 3000  //mins = 5h for monitoring against fault based excessive irrigation                       
                              


byte Diag_resetCurrCyc;       //number of power ups occured in current cycle (cycle duration = 1d); limited to 10, see Diag_init()
dfc Diag_DfcPlausPowUp = {0, DIAG_DFCPLAUS_POWUP_THRESH,0,0 };       // diagnostic fault code for unplausible numbers of power up in cycle
dfc Diag_DfcMonIrrTime = {0, DIAG_DFCMON_IRRTIME_THRESH,0,0 };       // diagnostic fault code for unplausible total irrigation time
dfc* Diag_dfcs[DIAG_NODFCS] = {&GetTime_DfcPlaus, &Humid_DfcScg, &Humid_DfcScb, &Diag_DfcPlausPowUp, &Diag_DfcMonIrrTime };          // array elements point to dfc (struct-) variables; don't forget to add them here if you define a new dfc type variable
const char Diag_dfcLabel[DIAG_NODFCS][DIAG_LENGTHDFCLABEL] = {{"time plaus"}, {"humid scg"}, {"humid scb"}, {"PowUpPlaus"}, {"MonIrrTime"}};  // !! length of string < DIAG_LENGTHDFCLABEL !!
unsigned int Diag_monTotalIrrigTime;                                 // total actual irrig time calculated from valve actuation pattern

//===================== public global variables provided by Diag =================

//boolean Diag_MainPowerRelay = false; // controls the main power Relay, no longer needed from V5.3 on


//==========================init==============================================================
//public
void Diag_init() {
  for (int i = 0; i < DIAG_NODFCS; i++) {
        Diag_dfcs[i]->flags = EEPROM.read(EEPROM_DiagDfcs + 2 * i);                //read fault memory from EEPROM 
        Diag_dfcs[i]->faultCount = EEPROM.read(EEPROM_DiagDfcs + 2 * i + 1);
      }
  Diag_resetCurrCyc = EEPROM.read(EEPROM_Diag_resetCurrCyc);
  if (Diag_DfcPlausPowUp.faultCount < 10) {                     //limit the EEPROM write cycles to 10 a day 
    Diag_resetCurrCyc ++; 
    EEPROM.update(EEPROM_Diag_resetCurrCyc, Diag_resetCurrCyc);  // write counter back to EEPROM immediately as it was too late once a power down or a reset occures 
    Diag_DebounceUpDown(&Diag_DfcPlausPowUp, true);               // debounce up the DFC 
  }
}
//public 
//-----------------------------Enable the watchdog------------------------------
//put it to the very beginning of the init 
void Diag_WdInit() {                        //enable the watchdog timer at init
  Watchdog.enable(4000);
}

//============================Loop===============================================================
void Diag_CycTask() {
  Diag_SetResetFaultFlag(Diag_dfcs);
  Diag_TrigCycle(GetTime_Mez, Diag_dfcs);
}


//============================functions for diagnosis and fault memory management ==============================================================0

//private----------------------------- clear cycle flag -------------------------------------------------
// whith a new cycle the cycle flag gets cleared
// purpose of the cycle flag: a) TRUE: diagnosis was executed in cycle; diagnosis result (fault or fault free) refers to this cycle; b)fault counter shall incremented max. 1 time per cycle
// to be called cyclically
// status: partly implemented, tested

void Diag_TrigCycle(byte *Mez, dfc *dfcs[]) {       // diag cycle = 24 hours; it means that fault counters <= number of days from last fault memory clear
  static boolean latch = false;
  if (Mez[0] == DIAG_TIMEHRTRIGCYC && Mez[1]== DIAG_TIMEMINTRIGCYC) {              // clear all cycle flags at midnight
    if (!latch) {
      latch = true;
      Diag_DebounceUpDown(&Diag_DfcPlausPowUp, false);                  // debounce down of the DFCPlaus Pow Up at the end of the cycle
      for (int i = 0; i < DIAG_NODFCS; i++) {
        CLR_DFCFLAG_CYC(dfcs[i]->flags);
        EEPROM.update(EEPROM_DiagDfcs + 2 * i,     dfcs[i]->flags);               //write diag info to EEPROM once every day 
        EEPROM.update(EEPROM_DiagDfcs + 2 * i + 1, dfcs[i]->faultCount);
      }
    }
  }
  else {
    latch = false;
  }
}
//_____________________________________________________________________________________________________________

//public---------------------------- clear fault memory--------------------------------------------------------
//Resets all DFC to fault free state; resets all DFCs' fault counter; reset all DFCs' debounce counter
// to be called one time per user command

void Diag_ClearFaultMemory() {
  for (byte i = 0; i < DIAG_NODFCS; i++) {
    Diag_dfcs[i]->flags = 0;
    Diag_dfcs[i]->faultCount = 0;
    Diag_dfcs[i]->debounceC = 0;extern char ComTel_ModeCommand;       // command character from user interface ComTelnet updated in state "Mode"  
    EEPROM.update(EEPROM_DiagDfcs + 2 * i,     Diag_dfcs[i]->flags);               //write diag info to EEPROM 
    EEPROM.update(EEPROM_DiagDfcs + 2 * i + 1, Diag_dfcs[i]->faultCount);
  }
#ifdef DEBUG_COMTELNET_BASIC
  ComTel_DebugNumberClientConn = 0;
#endif  
}
// public --------------------------print fault memory--------------------------------------------------------
void Diag_PrintFaultMemory() {
  for (byte i = 0; i < DIAG_NODFCS; i++) {
    ComTel_client.println(Diag_dfcLabel[i]);
    ComTel_client.print(F("flags "));
    ComTel_client.println(Diag_dfcs[i]->flags, 2);
    ComTel_client.print(F("fault count "));
    ComTel_client.println(Diag_dfcs[i]->faultCount);
    ComTel_client.println();
  }
  ComTel_client.println(F("[b]ack to menu [c]lear fault memory [r]eset device"));
}
//_________________________________________________________________________________________________________________

//private--------------------------set and heal DFC -------------------------------------------------------------------------------
//sets/resets fault flag depending on debouncing state
// to be called cyclically
// status: tested
void Diag_SetResetFaultFlag(dfc * dfcs[]) {
  for (byte i = 0; i < DIAG_NODFCS; i++) {
    if (dfcs[i]->debounceC >= dfcs[i]->debounceThres) {       //in case debouncing threshold is reached: a) set fault flag
            SET_DFCFLAG_DFC(dfcs[i]->flags); 
            CLR_DFCFLAG_HEA(dfcs[i]->flags);
            if (!READ_DFCFLAG_CYC(dfcs[i]->flags)) {         // increment the fault counter only once in cycle even the fault reaches debouncing threshold multiple time in cycle
              (dfcs[i]->faultCount)  ++; 
            }
            SET_DFCFLAG_CYC(dfcs[i]->flags);                     
    }
    if (dfcs[i]->debounceC == 0) {
      if (READ_DFCFLAG_DFC(dfcs[i]->flags)) {                 // set healed flag if fault flag was set before
        SET_DFCFLAG_HEA(dfcs[i]->flags);
      }
      CLR_DFCFLAG_DFC(dfcs[i]->flags);                       // healing means clearing fault flag
    }
  }

#ifdef DEBUG_DIAG
  for (byte i = 0; i < DIAG_NODFCS; i++) {
    Serial.println();
    Serial.print(i);
    Serial.print(F("flags "));
    Serial.println(dfcs[i]->flags, 2);
    Serial.print(F("debounceCtr "));
    Serial.println(dfcs[i]->debounceC);
    Serial.print(F("Thres "));
    Serial.println(dfcs[i]->debounceThres); 
    Serial.print(F("faultC "));
    Serial.println(dfcs[i]->faultCount);
    }
#endif    
}
//__________________________________________________________________________________________________________________________________

//public------------------------debounce up or down ------------------------------------------------------------------------------
//--controls the debounce counter based on the instantaneous fault condition
// to be called event based in at the related diagnosis spot
// status: tested
void Diag_DebounceUpDown(dfc* thisDfc, boolean instFaultCond) {          //if the instantaneous fault condition is present right at the time of the call, incement the debounce counter
  if (instFaultCond) {
    if(thisDfc->debounceC < thisDfc->debounceThres) {
      (thisDfc->debounceC) ++;    
    }
  }
  else if(thisDfc->debounceC > 0) {                                      // else decrement the debounce counter
    (thisDfc->debounceC) --;
  }
}

//=============================================Functions for monitoring=============================================================
// public
//Monitoring of irrigation against failures that cause unintended irrigation (water spoiling); 
//principle: actual irrigation time is calculated roughly from valve command byte and compared with sum of time from settings; monitoring period = 1d

void Diag_MonIrrigTime() {                       //Monitoring of irrigation against failures that cause unintended irrigation (water spoiling); 
  
static unsigned int Diag_monTotalIrrigTime;            //in min                // total actual irrig time calculated from valve actuation pattern
static unsigned int actIrrigTimeSecs;
  if (Irrig_ActiveValvePattern > 0) {     // in order to keep the monitoring simple it is neglected that more than one valve could be actuated wrongly at a time
    actIrrigTimeSecs ++;                  // time counter in s
    if (actIrrigTimeSecs > 59) {
      Diag_monTotalIrrigTime ++;          // total actual irrigation time in min
      actIrrigTimeSecs = 0;
    } 
  }
  if (GetTime_Mez[0] == DIAG_TIMEHRTRIGCYC && GetTime_Mez[1]== DIAG_TIMEMINTRIGCYC){  //reset the acutal total daily irrigation time ad midnight
    Diag_monTotalIrrigTime = 0;
  }
  Diag_DebounceUpDown(&Diag_DfcMonIrrTime, Diag_monTotalIrrigTime > DIAG_MAX_IRR_TIME_PER_DAY); // dfc control
/* no longer needed from V5.3 on
  if (Irrig_ActiveValvePattern > 0 && !READ_DFCFLAG_DFC(Diag_DfcMonIrrTime.flags)) {    
      Diag_MainPowerRelay = true;                         //activate the main relay if the irrigation scheduler wants it to && no fault present from this monitoring DFC
  }
  else {
    Diag_MainPowerRelay = false;
  }
*/

#ifdef DEBUG_DIAG
Serial.print(F("Mon TotalIrrig time "));
Serial.println(Diag_monTotalIrrigTime);
Serial.print(F("MaxTime "));
Serial.println(DIAG_MAX_IRR_TIME_PER_DAY);
#endif
}

// public
// fcn for WD handling; rising edge arg==true suppresses WD reset and lead to µC reset by intention; to be sparseley used only if no other mean than cure by reset
void Diag_Watchdog(boolean trigUcReset) {
  static boolean trigUcResetLatch;
  if (trigUcReset) {
      trigUcResetLatch = true;
  }
  if (!trigUcResetLatch) {
    Watchdog.reset();   // reset the watchdog timer periodically
  }
}
