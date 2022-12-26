
// ============================== RelayCtrl ====================================================
// actuate the relais Grove - 4-Channel SPDT Relay; as 8 channels are required two 4-channel boards are used
// the Ls4Bit board can be used with default I2C adress while the Ms4Bit board need to be flashed with a different adress RELAYCTRL_ADRMS4BITS. Flashing is done with only one board connected during development time


//============================== known issues ===============================================


// =====================definitions Irrig ==========================
Multi_Channel_Relay Ls4BitsRelay;    
#ifdef RELAYCTRL_8CHANNEL
  Multi_Channel_Relay Ms4BitsRelay;
#endif
const int RelayCtrl_mainPowerRelayPin = 5;    // see specification document for pin assignments

//===================== public global variables provided by RelayCtrl =================


//===================== global variables used from other modules===============================

extern byte Irrig_ActiveValvePattern;  
                                

//====================== Initializations (to be included into void(setup) ============================================================================
//Public
void RelayCtrl_init() { 
  Ls4BitsRelay.begin(RELAYCTRL_ADRLS4BITS); 
#ifdef RELAYCTRL_8CHANNEL
  Ms4BitsRelay.begin(RELAYCTRL_ADRMS4BITS);       
#endif 
  pinMode(RelayCtrl_mainPowerRelayPin, OUTPUT);
  digitalWrite(RelayCtrl_mainPowerRelayPin, LOW);
}

//======================Loop================================================================================================
//Public
void RelayCtrl_1s() {                                               // shall be called after Irrig and Diag_Mon because the 24V relays shall not switch under power (EMC prob with the ARDUINO)
static byte ValveRelaysOut, buf;
static boolean PowerRelayOut;   
static byte TransitionProgress;   


//------------- make sure the valve relays switch when the main power relay is off --------------------------------------------------------------------

  if (Irrig_ActiveValvePattern != ValveRelaysOut || TransitionProgress > 0) {
    if(TransitionProgress == 0) {
      PowerRelayOut = false;
      buf = Irrig_ActiveValvePattern;  
    }
    else if(TransitionProgress == 1){
     ValveRelaysOut =  buf;
    }
    else if (TransitionProgress == 2 && buf > 0) {                  // only activate main power relay in case a valve shall be activated
      PowerRelayOut = true; 
    }
    TransitionProgress ++; 
    if (TransitionProgress > 2) {
      TransitionProgress = 0;
    }
  }
  if (READ_DFCFLAG_DFC(Diag_DfcMonIrrTime.flags)) {          // overwrite PowerRelayOut in case fault path is set
    PowerRelayOut = false;    
  }

// ------------ write outputs ----------------------------------------------
  Ls4BitsRelay.channelCtrl(B00001111 & ValveRelaysOut);
#ifdef RELAYCTRL_8CHANNEL
  Ms4BitsRelay.channelCtrl(ValveRelaysOut >> 4);
#endif 

  digitalWrite(RelayCtrl_mainPowerRelayPin, PowerRelayOut);           // switch 230V 
  
//-------------debugging------------------------------------------
#ifdef DEBUG_RELAY_CTRL
  Serial.print(F("valvesIn "));
  Serial.println(Irrig_ActiveValvePattern);
  Serial.print(F("Main relais "));
  Serial.println(PowerRelayOut);
  Serial.print(F("valvesOut "));
  Serial.println(ValveRelaysOut);  
  Serial.println();
#endif
}
