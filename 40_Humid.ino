
// ============================== Humid ====================================================
// Aquisition and diagnosis of soil humidity

//============================== known issues ===============================================
// 

// =====================definitions Humid ==========================
#define SCGTHRES 5                    //threshold short to ground (0 .. 1024)
#define SCBTHRES 950                  //threshold short to batt (0 .. 1024)
#define SCG_DEBOUNCE_THRES  20        //as the Diag_DebounceUpDown() is called in 1s task 20 means 20s of debouncing
#define SCB_DEBOUNCE_THRES  21        //dto.
int Humid_AnalogPinSoil1 = A0;        // use analog pin A0 to read in humidity voltage; see specification document for pin assignments
#define HUMID_GAIN 21               // ADC: 5V <> 1024; 3V sensor --> 1024 / 5V * 3V = 614,4; Humid_Gain: 100% = raw * 21 / 2⁷  


 

//===================== public global variables provided by Humid =================

int Humid_SoilHumidPercent;    // soil humidity in a range of 0 to 100%
dfc Humid_DfcScg;               //diagnosis short circuit to ground of humidity sensor (sensor pin)
dfc Humid_DfcScb;               //dto. short to VDD 5v

//===================== global variables used from other modules===============================


//==========================init==============================================================
void Humid_init() {
  Humid_DfcScg.debounceThres = SCG_DEBOUNCE_THRES;
  Humid_DfcScb.debounceThres = SCB_DEBOUNCE_THRES;  
}

//============================Loop===============================================================

void Humid_1s(){
  int readAnalogPinSoil1_Raw;
  readAnalogPinSoil1_Raw = min(614, analogRead(Humid_AnalogPinSoil1));
  Humid_SoilHumidPercent = min(100, (readAnalogPinSoil1_Raw * HUMID_GAIN) >> 7);      // see def. of HUMID_GAIN above
  #ifdef DEBUG_HUMID_BASIC
  Serial.print(F("Humid "));
  Serial.println(readAnalogPinSoil1_Raw);
  Serial.print(Humid_SoilHumidPercent); 
  Serial.println(F("%")); 
#endif     
  Diag_DebounceUpDown(&Humid_DfcScg, readAnalogPinSoil1_Raw < SCGTHRES);      //short to ground diagnosis debouncing
  Diag_DebounceUpDown(&Humid_DfcScb, readAnalogPinSoil1_Raw > SCBTHRES);      //short to batt diagnosis debouncing
}
