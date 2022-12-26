// ============================== about this module====================================================
// It is the user interface where you can
// - set irrigation parameters
// - control the mode, e.g. "Automatic", "Manual", "Halt", "Resume", "Test", 
// - look up current status and history data
// As user front end a standard telnet app (install from playstore, e.g. terminus) is needed on your WLAN- / LAN-devices
// Connection data: IP adress as defined in ComCommon, port: 23 
// Implementation: Statemachine (nothing besides it)
//------------------------------------------------------------------------------------------------------------------------------------

//================================known issues=====================================================================
//  [deferred] replace char array for print messages during "mode" (save ram space); not so easy to realize 
//  get clear about what is the client.flush thing for
//  display settings before save
//  diagnosis output and clear fault memory command still to be implemented


// =====================definitions ComTelnet==========================
// telnet defaults to port 23
EthernetServer server(23);
EthernetClient ComTel_client;

//===================== public global variables provided by ComTelnet=================
                    
byte ComTel_SetupData[COMTEL_SETDATAARRSIZ] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };     // irrigation parameters got from user interface; {v1 .. v8, test dur, time, humidity}
#ifdef DEBUG_COMTELNET_BASIC
int ComTel_DebugNumberClientConn;       //number of client connections since power up or reset
#endif
                                            
//enum {off, autom, halt, test };  
char ComTel_ModeCommand = 0;                                       

//===================== global variables and functions used from other modules===============================

extern byte GetTime_Mez[3];  

//extern byte ComCom_Ip[COMCOM_IPADRNO][4]; 

extern char Irrig_ModeDisplayStringHeadl[IRRIG_MODEDISPLSTRINGHEADLENGTH]; 
extern char Irrig_ModeDisplayStringDetail[IRRIG_MODEDISPLSTRINGDET]; 
extern byte Irrig_IrrigHistory;
extern byte Irrig_HumidHistory[8];
extern byte Irrig_HistoryPtr;

extern void Diag_ClearFaultMemory();                                     


//=======================Telnet statemachine definitions===============================

typedef struct  {             // instances of type Stm_StateFcnsPtr host the pointers to the stm functions of a stm state
  boolean (*PCondExit)();     // as not many exit and transition codes are expected they shall be included into the condition code function; this is called every time control flow passes by
                              // the fcn shall return "true" for transition to other state ("false" for remain in current state)
  void (*PEntr)();            // entry code of the state is executed once after a transition to that state happended; it is independent of which transition to the state was triggered
  void (*PAct)();             // action code is called every time control flow passes by
} Stm_StateFcnsPtr;  

//define instances of Stm_StateFcnsPtr
//    type             instance name           adresses of the functions of the stm states
const Stm_StateFcnsPtr ComTel_StmPIdleFcns = { &ComTel_StmIdleCondExit, &ComTel_StmIdleEntr, &ComTel_StmIdleAct };
const Stm_StateFcnsPtr ComTel_StmPMenuFcns = { &ComTel_StmMenuCondExit, &ComTel_StmMenuEntr, &ComTel_StmMenuAct };
const Stm_StateFcnsPtr ComTel_StmPSetupFcns = { &ComTel_StmSetupCondExit, &ComTel_StmSetupEntr, &ComTel_StmSetupAct };
const Stm_StateFcnsPtr ComTel_StmPSetupIpFcns = { &ComTel_StmSetupIpCondExit, &ComTel_StmSetupIpEntr, &ComTel_StmSetupIpAct };
const Stm_StateFcnsPtr ComTel_StmPModeFcns = { &ComTel_StmModeCondExit, &ComTel_StmModeEntr, &ComTel_StmModeAct };
const Stm_StateFcnsPtr ComTel_StmPDiagFcns = { &ComTel_StmPDiagCondExit, &ComTel_StmPDiagEntr, &ComTel_StmPDiagAct };
// new state? just add the definition of the pointer struct accordingly

Stm_StateFcnsPtr *ComTel_StmP2Fcns = &ComTel_StmPIdleFcns; // define Pointer to struct of stm functions and initialize with the struct of the "Idle" state; 
                                                           // result: ComTel_StmP2Fcns ---> { &ComTel_StmIdleCondExit, &ComTel_StmIdleEntr, &ComTel_StmIdleAct }

//=======================Telnet communication definitions=====================================  
const char ComTel_Ansi_Home[] = "\u001B[2J"; //"u001B[2L"; // \u001B is the identifier for ANSI escape codes; for the codes see here: http://ascii-table.com/ansi-escape-sequences.php
//const char ComTel_SetupStrValve[] = "valve "; 
//const char ComTel_SetupStrByte[] = "byte ";
//const char ComTel_SetupStrInMin[] = " [min] ";            
#define CR 13 // carriage return = enter                  //see ASCII table 

int ComTel_CtrTimeout; 
#define COMTEL_TIMEOUTDUR 3000       // equals 5mins (5min * 60s/min * 10 (task = 100ms task, i.e 10 times per s))
#define COMTEL_TIMEOUT_CONNSTOP 70  // debug_connect the client.stop() fcn has a default timeout of 1s which means that the control flow could be held up for this duration 
                                    // debug_connect COMTEL_TIMEOUT_CONNSTOP is the no. of ms where client.stop() polls the client in a friendly mode to stop; thereafter it forces the stop 
                                   

//========================= state "Setup" definitions ===================================================================
int ComTel_StmSetupIndex;
int ComTel_StmSetupIndexPrev;
//String ComTel_SetupReadString = ""; 
char ComTel_SetupReadString[4] = ""; 
char ComTel_NextCharFromClient;                                     // updated during "Setup" only; contains Null-Character in case nothing received during client.read()     

//==========================state "Menu" definitions =======================================================================0


//====================== Initializations (to be included into void(setup) ============================================================================
//Public
void ComTel_init() {
  for (int i = 0; i < COMTEL_SETDATAARRSIZ; i ++) {
    ComTel_SetupData[i] = EEPROM.read(i + EEPROM_ComTel_SetupData);                    // get setup parameters from EEPROM
  }
#ifdef DEBUG_COMTELNET_MAX
  for (int i = 0; i < COMTEL_SETDATAARRSIZ; i ++ ) {
    Serial.println(ComTel_SetupData[i]);
  }
#endif  
   // start listening for clients
  server.begin();
  Serial.print(F("Chat server address:"));
  Serial.println(Ethernet.localIP()); 
}

//======================Loop================================================================================================

//=================================Telnet Statemachine======================================================================
// user interface via telnet. Please use in LAN only and do not open the port in your firewall! 
// just install some telnet client on your network device and call the server according to hints in ComCommon

//------------------------------core Stm--------------------------------------------
//Public
void ComTel_Stm_100ms() {                        //to be attached to the loop
  ComTel_NextCharFromClient = TelnetClientHandler(&ComTel_CtrTimeout, COMTEL_TIMEOUTDUR);   // get the next character from client or, if no new is in the cue, set it to the 0-Character; calculate the communication timeout counter
#ifdef DEBUG_COMTELNET_BASIC
//  Serial.println(ComTel_NextCharFromClient);
#endif
  if (ComTel_StmP2Fcns->PCondExit() == true) {  //call of condition+exit code of current state; just to remember it later ...: this calls the fcn AND checks whether fcn's return == true 
                                                //the fcn returns if it carried out a transition to another state; if so ComTel_StmP2Fcns points to the fcns of the new state now
    ComTel_StmP2Fcns->PEntr();                  // call the entry code of the new state
  }
  ComTel_StmP2Fcns->PAct();                     // call the action code of current or new state
}

//Private (all down form here)
// -------------------------------State "Idle" -----------------------------------------

void ComTel_StmIdleEntr() {   
#ifdef DEBUG_COMTELNET_BASIC                            // or https://www.renesas.com/us/en/products/gadget-renesas/reference/gr-kaede/library-ethernet-server
    Serial.println(F("IdlEntr"));
#endif                                     
  ComTel_client.stop(); 
}


boolean ComTel_StmIdleCondExit() {               // returns true if a transition is triggered
  if (ComTel_client) {             // for more on client connection see: https://forum.arduino.cc/?topic=710767#msg4775485
#ifdef DEBUG_COMTELNET_BASIC                            // or https://www.renesas.com/us/en/products/gadget-renesas/reference/gr-kaede/library-ethernet-server
    ComTel_DebugNumberClientConn ++;
#endif    
    ComTel_StmP2Fcns = &ComTel_StmPMenuFcns;        // debug_connect
    return true;                                    // debug_connect
  }
  else {
    return false;
  }
} 

void ComTel_StmIdleAct() {
  ComTel_client.stop(); 
#ifdef DEBUG_COMTELNET_BASIC
  static int j = 0; 
  if (j % 10 == 0) {
    Serial.println(F("IdleAct"));
  }
  j ++;  
#endif 
}



//-------------------------------State "Menu" ----------------------------------------------

void ComTel_StmMenuEntr() { 
  char buf[9];
  ComTel_client.flush();                             //Waits until all outgoing characters in buffer have been sent (not completely understood ...)
#ifdef DEBUG_COMTELNET_BASIC
  Serial.println(F("Menu Entr"));
#endif
  ComTel_client.println(ComTel_Ansi_Home);                  // clear screen on client
  ComTel_client.println();
  ComTel_client.print(F("Wenzler's Gartengießer V"));
  ComTel_client.print(IRRIGATION_SYS_VER);
  ComTel_client.print(F("."));
  ComTel_client.println(IRRIGATION_SYS_REV);
  ComTel_client.println(F("==========================="));
  ComTel_client.print(F("Gießtage "));
  for (int i = 0; i < 8; i++) {
   ComTel_client.print(bitRead(Irrig_IrrigHistory, (i + Irrig_HistoryPtr + 1)%8));
   ComTel_client.print(F("  ")); 
  }
  ComTel_client.println();
  ComTel_client.print(F("Feuchte  "));
  for (int i = 0; i < 8; i++) {
   ComTel_client.print(Irrig_HumidHistory[(i + Irrig_HistoryPtr + 1)%8]);
   ComTel_client.print(F(" ")); 
  }
  ComTel_client.println();
  ComTel_client.println();
  ComTel_client.println(F("Menu: [m]ode, [s]etup, setup[i]p, [d]iag, [q]uit")); 
  ComTel_client.println();
  //ComTel_client.println(*GetTime_PtrMezString(GetTime_Mez)); // print the time
  GetTime_MezAsCharArray(GetTime_Mez, buf);     
  ComTel_client.println(buf);        
}

boolean ComTel_StmMenuCondExit() {  
 // char command;
  if (!ComTel_client.connected() || ComTel_CtrTimeout == 0) {                // if connection to client got terminated OR no character received from client for ComTel_TimeoutDur go to "Idle"
                                                                             // only the .connected() routine returns false if the connection has been terminated by the client
    ComTel_StmP2Fcns = &ComTel_StmPIdleFcns;       // points to the fcns of state "Idle" now
    return true;
  }
  else {
 //   command = ComTel_NextCharFromClient;        // "NextCharChange" ComTel_client.read();                        // get command character from client
    switch (ComTel_NextCharFromClient) {
  case 'q':
    ComTel_StmP2Fcns = &ComTel_StmPIdleFcns;
//    ComTel_client.stop();                       moved to the Idle entry code
    ComTel_NextCharFromClient = 0;                // whenever the character is used for an action snub it
    return true;
    break;
  case 's':
    ComTel_StmP2Fcns = &ComTel_StmPSetupFcns;
    ComTel_NextCharFromClient = 0;
    return true;
    break;
  case 'i':
    ComTel_StmP2Fcns = &ComTel_StmPSetupIpFcns;
    ComTel_NextCharFromClient = 0;
    return true;
    break;
  case 'd':
    ComTel_StmP2Fcns = &ComTel_StmPDiagFcns;
    ComTel_NextCharFromClient = 0;
    return true;
    break;
  case 'm':
    ComTel_StmP2Fcns = &ComTel_StmPModeFcns;
    ComTel_NextCharFromClient = 0;
    return true;
    break;
  default:
    // if nothing else matches, do the default
    // default is optional
    return false; 
    break;
    }
  }    
}

void ComTel_StmMenuAct() {
  //Serial.println("Menu Act");
  /*
  static int i; 
  if (i % 10 == 0) {
    //byte *Ptr = GetTime_Mez; 
    String *help; 
    //help = GetTime_PtrMezCharArray(GetTime_Mez);
    ComTel_client.println(*GetTime_PtrMezCharArray(GetTime_Mez));
    ComTel_client.println(); 
  }
  
  i ++;
  */
}
//------------------------------------State "Mode"----------------------------------------------------

void ComTel_StmModeEntr() { 
  //ComTel_client.println("Mode: [a]uto, [o]ff, h[alt], [r]esume, [t]est, back2[m]enu"); 
#ifdef DEBUG_COMTELNET_BASIC
  Serial.println(F("ModeEntr"));
#endif 
}

boolean ComTel_StmModeCondExit() { 
  char command = 0;
  if (!ComTel_client.connected() || ComTel_CtrTimeout == 0) {                // if connection to client got terminated go to "Idle"
    ComTel_StmP2Fcns = &ComTel_StmPIdleFcns;       // points to the fcns of state "Idle" now
    return true;
  }
//  else if (ComTel_client.available() > 0) {     "NextCharChange" ComTel_client.read();
    else if (ComTel_NextCharFromClient != 0) {
     command = ComTel_NextCharFromClient;          //  "NextCharChange" ComTel_client.read(); 
     ComTel_ModeCommand = command;
#ifdef DEBUG_COMTELNET_MAX
     Serial.println(ComTel_ModeCommand);
#endif
    switch (command) {    
    case 'b':
      ComTel_StmP2Fcns = &ComTel_StmPMenuFcns;
      return true;
      break;
    case 'z':
      //
      return false;
      break;
    case 'h':
      //
      return false;
      break;
    default:
      // if nothing else matches, do the default
      // default is optional
      return false; 
      break;
      }
  return false;
  }   
}

void ComTel_StmModeAct() { 
  static int i = 16; 
  if (i % 16 == 0) {
    ComTel_client.println(ComTel_Ansi_Home);                  // clear screen on client
    ComTel_client.println(F("Mode: [a]uto [o]ff [p]ause [r]esume a[d]-hoc [t]est [m]anual [b]ack2menu [s]uspend+1d s[u]spend-1d ")); 
    ComTel_client.println(Irrig_ModeDisplayStringHeadl); 
    ComTel_client.println(Irrig_ModeDisplayStringDetail); 
#ifdef DEBUG_COMTELNET_BASIC
    Serial.println(F("ComTelModeAct"));
#endif 
  }  
//  if (ComTel_client.available() > 0) {
//      ComTel_ModeCommand = ComTel_client.read();                        // get next character from client; used in actiocn code as well
  i ++; 
}

//---------------------------------State "Setup"----------------------------------------------------
// 
void ComTel_StmSetupEntr() { 
  ComTel_StmSetupIndex = 0;
  ComTel_StmSetupIndexPrev = -1;                     // see action code
  ComTel_client.flush();                             //Waits until all outgoing characters in buffer have been sent.
#ifdef DEBUG_COMTELNET_BASIC
  Serial.println(F("Setup Entr"));
#endif
  ComTel_client.print(ComTel_Ansi_Home);                 //clear screen on the client (magic?...)
  PrintSetupData();
  ComTel_client.println(F("Setup: [s]ave, [a]bort/back")); 
}

boolean ComTel_StmSetupCondExit() {
  int loopCtr;
  //Serial.println("Setup cond");
  if (!ComTel_client.connected() || ComTel_CtrTimeout == 0) {                // if connection to client got terminated or timeout for receiving characters elapsed go to "Idle"
    ComTel_StmP2Fcns = &ComTel_StmPIdleFcns;       // points to the fcns of state "Idle" now
    return true;
  }
  else {
      switch (ComTel_NextCharFromClient) {
    case 's':
      for (loopCtr = EEPROM_ComTel_SetupData; loopCtr < EEPROM_ComTel_SetupData + COMTEL_SETDATAARRSIZ; loopCtr ++) {
        EEPROM.update(loopCtr, ComTel_SetupData[loopCtr]);
        //ComTel_client.println("saved");
      }
      ComTel_client.println(F("saved"));
      return false;
      break;
    case 'a':                                                                               // in case of "a" for abort, discard entries and get back to EEPROM values
      for (int i = 0; i < COMTEL_SETDATAARRSIZ; i ++) {
        ComTel_SetupData[i] = EEPROM.read(i + EEPROM_ComTel_SetupData);                    // get setup parameters from EEPROM
      }
      ComTel_StmP2Fcns = &ComTel_StmPMenuFcns; 
      return true;
      break;
    default:
      // if nothing else matches, do the default
      // default is optional
      return false; 
      break;
      }
  }  
}

void ComTel_StmSetupAct() {
  //char buf[57];
  //char buf2[4];
  static int stringpos = 0;
  if (ComTel_StmSetupIndex < COMTEL_SETDATAARRSIZ) {
    if (ComTel_StmSetupIndexPrev < ComTel_StmSetupIndex) {      //write the label of the new parameter to be aquired to the client
      ComTel_client.println();
      if (ComTel_StmSetupIndex < 8) {                           // get valve durations from 1 to 8
        ComTel_client.print(F("valve ")); 
        ComTel_client.print(ComTel_StmSetupIndex + 1); 
        ComTel_client.print(F(" [min] ")); 
      }
      else if (ComTel_StmSetupIndex == 8) {
        ComTel_client.print(F("test dur. [min] "));
      }
      else if (ComTel_StmSetupIndex == 9) {
        ComTel_client.print(F("time [hh]"));
      }
//      else {
//        ComTel_client.print(F("humid. 0-100 "));
//      }
      else if (ComTel_StmSetupIndex == 10) {
        ComTel_client.print(F("humid. 0-100 "));
      }
      else {
        ComTel_client.print(F("new lawn add. repetitions, max.4 "));
      }
      //ComTel_SetupReadString = "";                             //clear the string from last parameter
      for (int i=0; i < 4; i ++) {
        ComTel_SetupReadString[i] = '\0';
      }
      ComTel_StmSetupIndexPrev++;
    }    
    if (ComTel_NextCharFromClient == CR){                     //reading of parameter is done if "Enter" (= carriage return) is received from client      
      //ComTel_client.print(ComTel_SetupReadString);          // the terminus telnet client depicts the entered value so no need to; note: the linux telnet client doesn't do so
      ComTel_SetupData[ComTel_StmSetupIndex] = (byte) atoi(ComTel_SetupReadString); //ComTel_SetupReadString.toInt();
#ifdef DEBUG_COMTELNET_MAX
      Serial.println(ComTel_SetupReadString);
      Serial.println(ComTel_SetupData[ComTel_StmSetupIndex]);
#endif
      ComTel_StmSetupIndex ++;
      stringpos = 0;
      //Serial.println(ComTel_StmSetupIndex);
    }
    else if (ComTel_NextCharFromClient /*!= 0*/ > 47 && ComTel_NextCharFromClient < 58 && stringpos < 3) {        // as long as new characters "0" to "9" come in, add them to the parameter string; ignore if more than 3 digits come 
//      ComTel_SetupReadString += ComTel_NextCharFromClient;
#ifdef DEBUG_COMTELNET_MAX
      Serial.println(ComTel_NextCharFromClient);
      Serial.println(F("add char to string"));
#endif     
      ComTel_SetupReadString[stringpos] = ComTel_NextCharFromClient;
      stringpos ++;
    }
  }
    if (ComTel_StmSetupIndex == COMTEL_SETDATAARRSIZ && ComTel_StmSetupIndexPrev < ComTel_StmSetupIndex) {
      ComTel_client.println(F("done"));
      /*
      FcnsDeleteString(buf, sizeof(buf) / sizeof(buf[0])); //prepare the string for the valves' duration values print out on client
      for (int i = 0; i < 8; i ++) {
        strcat(buf, "V");
        itoa(i+1, buf2, 10); 
        strcat(buf, buf2);
        strcat(buf, " "); 
        itoa(ComTel_SetupData[i], buf2, 10);
        strcat(buf, buf2);
        strcat(buf, " ");
      }
      ComTel_client.println(buf);
      */
      PrintSetupData();      

//     ComTel_client.println("V1 %d", (int) ComTel_SetupData[0]); //, (int) ComTel_SetupData[0]);
      ComTel_StmSetupIndexPrev ++;
    }
    
    //if (ComTel_StmSetupIndex == 7) {
    //  ComTel_client.println(); 
    //}thisChar
    //ComTel_StmSetupIndex ++;
  //if (j % 10 == 0) {
  //  Serial.println(ComTel_SetupReadString);
  //}
  //j ++; 
  
}

//---------------------------------State "SetupIp"----------------------------------------------------
// telnet feature for the user to to change the ip addresses for the arduino, the subnet mask, dns, the time server and the gateway
void ComTel_StmSetupIpEntr() { 
  ComTel_StmSetupIndex = 0;
  ComTel_StmSetupIndexPrev = -1;                     // see action code
  ComTel_client.flush();                             //Waits until all outgoing characters in buffer have been sent.
#ifdef DEBUG_COMTELNET_BASIC
  Serial.println(F("SetupIp Entr"));
#endif
  ComTel_client.print(ComTel_Ansi_Home);                 //clear screen on the client (magic?...)
  ComTel_client.println(F("Setup: [s]ave, [a]bort/back")); 
}

boolean ComTel_StmSetupIpCondExit() {
  int loopCtr;
  int testvar;
  //Serial.println("Setup cond");
  if (!ComTel_client.connected() || ComTel_CtrTimeout == 0) {                // if connection to client got terminated or timeout for receiving characters elapsed go to "Idle"
    ComTel_StmP2Fcns = &ComTel_StmPIdleFcns;       // points to the fcns of state "Idle" now
    return true;
  }
  else {
      switch (ComTel_NextCharFromClient) {
    case 's':
      for (loopCtr = EEPROM_IpAdr; loopCtr < EEPROM_IpAdr + COMCOM_IPADRNO * 4; loopCtr ++) {             //index starts according EEPROM Layout, see IrrigationSys_ 
        EEPROM.update(loopCtr, ComCom_Ip[(loopCtr - EEPROM_IpAdr) / 4][(loopCtr - EEPROM_IpAdr) % 4]);   //indexes start from 0 here
      }
      ComTel_client.println(F("saved"));
      return false;
      break;
    case 'a':
      ComTel_StmP2Fcns = &ComTel_StmPMenuFcns; 
      return true;
      break;
    default:
      // if nothing else matches, do the default
      // default is optional
      return false; 
      break;
      }
  }  
}

void ComTel_StmSetupIpAct() {
  char buf[17];                                                 // one line: 4 x 3 digits + 4 space + end character
  char buf2[4];
  static int stringpos = 0;
  if (ComTel_StmSetupIndex < COMCOM_IPADRNO * 4) {              // Number of addresses or masks * 4 byte each
    if (ComTel_StmSetupIndexPrev < ComTel_StmSetupIndex) {      //write the label of the new parameter to be aquired to the client
      ComTel_client.println();
      ComTel_client.print(F("byte ")); 
      ComTel_client.print(ComTel_StmSetupIndex + 1); 
      ComTel_client.print(" "); 
            
      //ComTel_SetupReadString = "";                             //clear the string from last parameter
      for (int i=0; i < 4; i ++) {
        ComTel_SetupReadString[i] = '\0';
      }
      ComTel_StmSetupIndexPrev++;
    }    
    if (ComTel_NextCharFromClient == CR){                     //reading of parameter is done if "Enter" (= carriage return) is received from client      
      //ComTel_client.print(ComTel_SetupReadString);          // the terminus telnet client depicts the entered value so no need to; note: the linux telnet client doesn't do so
      ComCom_Ip[ComTel_StmSetupIndex / 4][ComTel_StmSetupIndex % 4] = (byte) atoi(ComTel_SetupReadString); //ComTel_SetupReadString.toInt();
#ifdef DEBUG_COMTELNET_MAX
      Serial.println(ComTel_SetupReadString);
      Serial.println(ComCom_Ip[ComTel_StmSetupIndex / 4][ComTel_StmSetupIndex % 4]);
#endif
      ComTel_StmSetupIndex ++;
      stringpos = 0;
      //Serial.println(ComTel_StmSetupIndex);
    }
    else if (ComTel_NextCharFromClient /*!= 0*/ > 47 && ComTel_NextCharFromClient < 58 && stringpos < 3) {               // as long as new characters between "0" and "9" come in, add them to the parameter string; ignore if more than 3 digits come 
//      ComTel_SetupReadString += ComTel_NextCharFromClient;
      ComTel_SetupReadString[stringpos] = ComTel_NextCharFromClient;
      stringpos ++;
    }
  }
  if (ComTel_StmSetupIndex == COMCOM_IPADRNO * 4 && ComTel_StmSetupIndexPrev < ComTel_StmSetupIndex) {  //if all parameters are entered print them on the terminal
    ComTel_client.println(F("done"));
    for (int i = 0; i <  COMCOM_IPADRNO; i++) {
      for (int j = 0; j < 4; j++) {
        ComTel_client.print(ComCom_Ip[i][j]);
        ComTel_client.print(F(" "));
#ifdef DEBUG_COMTELNET_MAX
        Serial.println(ComCom_Ip[i][j]);
#endif       
      }
      ComTel_client.println();
    }
    
    ComTel_StmSetupIndexPrev ++;     // this is for doing the printout only once (see if condition)
  }
      
}
//----------------------------------------------------------------State Diag---------------------------------------------------------------------------------------------------------
void ComTel_StmPDiagEntr() {
  ComTel_client.print(ComTel_Ansi_Home);  //clearscreen
#ifdef DEBUG_COMTELNET_BASIC
  ComTel_client.print(F("Connections since reset "));
  ComTel_client.println(ComTel_DebugNumberClientConn);
  ComTel_client.println();
#endif
  Diag_PrintFaultMemory();
}
boolean ComTel_StmPDiagCondExit() {
 if (!ComTel_client.connected() || ComTel_CtrTimeout == 0) {                // if connection to client got terminated or timeout for receiving characters elapsed go to "Idle"
    ComTel_StmP2Fcns = &ComTel_StmPIdleFcns;       // points to the fcns of state "Idle" now
    return true;
  }
  else {
      switch (ComTel_NextCharFromClient) {
    case 'c':                                               // clear fault memory and go back to menue
      Diag_ClearFaultMemory();
      ComTel_client.println(F("fault memory successfully cleared"));
      ComTel_StmP2Fcns = &ComTel_StmPMenuFcns; 
      return true;
      break;
    case 'b':                                           // back to menu without clearing fault memory
      ComTel_StmP2Fcns = &ComTel_StmPMenuFcns; 
      return true;
      break;
    case 'r':                                               // reset micro controller
      Diag_Watchdog(true);
      ComTel_client.println(F("device reset triggered"));
      ComTel_StmP2Fcns = &ComTel_StmPMenuFcns; 
      return true;
      break;
    default:
      // if nothing else matches, do the default
      // default is optional
      return false; 
      break;
      }
  }  
}
void ComTel_StmPDiagAct() {
  
}

//=============================================================== Functions===========================================================================================================
// private
char TelnetClientHandler(int* ctr, const int timeoutDurat) {                // handles incoming connections from clients; refuses to handle more than one client at the same time
  char buf;
  static EthernetClient newClient;
#ifdef DEBUG_COMTELNET_BASIC
  Serial.println(*ctr);
#endif
/*EthernetClient*/ newClient = server.accept();    //debug_connect     .accepts returns 0 if no new client has connected
#ifdef DEBUG_COMTELNET_BASIC
      Serial.print(F("newClient= "));
      Serial.println(newClient);
      Serial.print(F("Client= "));
      Serial.println(ComTel_client);
#endif
  if (newClient && ComTel_client.connected() /*> ComTel_client*/) {              // a new client came in
    newClient.setConnectionTimeout(COMTEL_TIMEOUT_CONNSTOP);  //debug_connect  see #define for more explaination                                                                                // as there is already one, stop the new one
    newClient.stop();
#ifdef DEBUG_COMTELNET_BASIC
      Serial.println(F("2 connOneStop"));
#endif
    }
    else if (newClient) {
    ComTel_client = newClient; 
    ComTel_client.setConnectionTimeout(COMTEL_TIMEOUT_CONNSTOP);  //debug_connect  see #define for more explaination
    //newClient = 0;                  // this will be done by newClient = server.accept() above
#ifdef DEBUG_COMTELNET_BASIC
    Serial.println(ComTel_NextCharFromClient);
#endif  
    }
// ---------------- read the char from client and handle the client timeout-----------------
  buf = 0; 
  if (ComTel_client.available()){
    buf = ComTel_client.read();
  }
  if (buf != 0 || (boolean)newClient) {              //using server.accept() may lead to a connected client but with no characters to read
                                                     // the timeout needs to be initialized once a client is connected regardless of whether chars can be read
    *ctr = timeoutDurat;
    return buf;  
  }
  else {
    if (*ctr > 0)  {
      (*ctr) --;                     // higher priority of -- than *x !
    }
    return 0;
  } 
}

//------------------------------------------------------------------------------------------------------------------------------------------
void PrintSetupData() {
  for (int i = 0; i < 8; i ++) {
        ComTel_client.print(F("V"));
        ComTel_client.print(i+1);
        ComTel_client.print(F(" "));
        ComTel_client.print(ComTel_SetupData[i]);
        ComTel_client.print(F("  "));
  }
  ComTel_client.println();   
  ComTel_client.print(F("test dur. [min] "));
  ComTel_client.print(ComTel_SetupData[8]);
  ComTel_client.print(F("  "));
  ComTel_client.print(F("time [hh] "));
  ComTel_client.print(ComTel_SetupData[9]);
  ComTel_client.print(F("  "));
  ComTel_client.print(F("humid. "));
  ComTel_client.print(ComTel_SetupData[10]);
  ComTel_client.println();
  ComTel_client.print(F("new lawn add. repetitions "));
  ComTel_client.println(ComTel_SetupData[11]);
  ComTel_client.println();
}
