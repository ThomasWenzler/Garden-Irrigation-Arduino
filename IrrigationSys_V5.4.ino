
//=================================about this module=========================================
//the module is a (very) basic time based task scheduler; processes from the functional modules are called witin the task containers
//includes and EEPROM Layout are done only here

//==============================================================================================================

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>  
#include <EEPROM.h>
#include <multi_channel_relay.h>
#include <Adafruit_SleepyDog.h>    //watchdog


//=============Defines================================================================
//--------------------------Module Diag ---------------------------------------------
#define DIAG_NODFCS 5       //total number of DFCs (diagnostic fault codes)
//--------------------------Module ComCom-----------------------------------------------
#define COMCOM_IPADRNO 5 // number of ip adresses or masks used for communication
//------------------------Module ComTelnet--------------------------------------------
#define COMTEL_SETDATAARRSIZ 12    
enum{enum_testDur = 8, enum_startTime = 9, enum_humid = 10, enum_repet = 11};  

//------------------------Module Irrig -------------------------------------------------
#define IRRIG_MODEDISPLSTRINGHEADLENGTH 31
#define IRRIG_MODEDISPLSTRINGDET 18  
//------------------------Module Scheduler---------------------------------------------
#define IRRIGATION_SYS_VER 5
#define IRRIGATION_SYS_REV 4
//------------------------Module RelayCtrl--------------------------------------------.

#define RELAYCTRL_8CHANNEL
#define RELAYCTRL_ADRLS4BITS  0x11
#define RELAYCTRL_ADRMS4BITS  0x12

//------------------------debug -----------------------------------------------
//#define DEBUG_DIAG
//#define DEBUG_COMTELNET_MAX
//#define DEBUG_COMTELNET_BASIC
//#define DEBUG_GETTIME_MAX
//#define DEBUG_GETTIME_BASIC
//#define DEBUG_IRRIG_MAX
//#define DEBUG_IRRIG_BASIC
//#define DEBUG_HUMID_BASIC
//#define DEBUG_RELAY_CTRL

//#define RUNTIME     //activate runtime measurement

//============Typedefs and defines for Diagnose =================================================


typedef struct {
  byte debounceC;             //symmetrical debouncing by default; i.e. same counter used for debouncing of setting fault flag and healing fault
  byte debounceThres;
  byte faultCount;
  byte flags; 
} dfc;
#define DFCFLAG_DFC B00000001   //position of the DFC flag in the byte
#define DFCFLAG_CND B00000010   // fault condition fulfilled
#define DFCFLAG_CYC B00000100   // position of the cycle flag in the byte
#define DFCFLAG_HEA B00001000   // position of the healed flag in the byte

#define SET_DFCFLAG_DFC(flags) ((flags) = (flags) | DFCFLAG_DFC)
#define CLR_DFCFLAG_DFC(flags) ((flags) = (flags) & ~DFCFLAG_DFC)
#define SET_DFCFLAG_CND(flags) ((flags) = (flags) | DFCFLAG_CND)
#define CLR_DFCFLAG_CND(flags) ((flags) = (flags) & ~DFCFLAG_CND)
#define SET_DFCFLAG_CYC(flags) ((flags) = (flags) | DFCFLAG_CYC)
#define CLR_DFCFLAG_CYC(flags) ((flags) = (flags) & ~DFCFLAG_CYC)
#define SET_DFCFLAG_HEA(flags) ((flags) = (flags) | DFCFLAG_HEA)
#define CLR_DFCFLAG_HEA(flags) ((flags) = (flags) & ~DFCFLAG_HEA)

#define READ_DFCFLAG_DFC(flags) ((boolean)((flags) & DFCFLAG_DFC))
#define READ_DFCFLAG_CND(flags) ((boolean)((flags) & DFCFLAG_CND))
#define READ_DFCFLAG_CYC(flags) ((boolean)((flags) & DFCFLAG_CYC))
#define READ_DFCFLAG_HEA(flags) ((boolean)((flags) & DFCFLAG_HEA))


//============EEPROM layout==================================================

#define EEPROM_START_ADR 0
#define EEPROM_ComTel_SetupData EEPROM_START_ADR
#define EEPROM_AutoBeforePowerOff (EEPROM_ComTel_SetupData + COMTEL_SETDATAARRSIZ)                          // don't forget the brackets here as the literal will be replaced just by the text. The precompiler does not calculate!
#define EEPROM_IpAdr (EEPROM_AutoBeforePowerOff + 1)  //Sizeof(AutoBeforePowerOff) = 1 byte 
#define EEPROM_DiagDfcs (EEPROM_IpAdr + COMCOM_IPADRNO * 4)   // COMCOM_PIADRNO addresses with 4 bytes each
#define EEPROM_Diag_resetCurrCyc (EEPROM_DiagDfcs + DIAG_NODFCS * 2)     // 2 bytes of each DFC (flags and fault count)
//define EEPROM_next (EEPROM_Diag_resetCurrCyc + 1)                      // Diag_resetCurrCyc is 1 byte long

//============ Scheduler definitions =====================================================
unsigned long RtosTimeNext, RtosTimePrev = 0;
unsigned int Count1000ms;
boolean UlOverflow = false; 
#ifdef RUNTIME
int Schedule_rtbuf100ms[10]; 
#endif

 
void setup() {
  // put your setup code here, to run once:
  RtosTimeNext = millis() + 100;
  Count1000ms = 0;
  init_processes();                                                               
  // the init_rtos_task is always the last instruction in the setup
  init_rtos_tasks();
}

void loop() {
  // put your main code here, to run repeatedly:
  unsigned long timestamp; 
  // put your main code here, to run repeatedly:
//task_Bg();

 //regular time tasks and processes

   if (millis() > RtosTimeNext && (RtosTimeNext > RtosTimePrev || millis() < RtosTimePrev)) {  // handling of overflow of the unsigned long: 
                                                                                               // at first the RtosTimeNext encounters a overflow after adding 100
                                                                                               // without overflow handling of course millis() would be greater immediately as it has not yet encoutered the ov
                                                                                               // a wrong trigger is avoided as RtosTimeNext > RtosTimePrev yields false after the ov and millis() has not yet encountered the ov
                                                                                               // once millis() encounters the ov as well the last condition yields true
#ifdef RUNTIME                                                                                              // finally with the next task trigger RtosTimeNext > RtosTimePrev becomes true --> end of ov handling 
    timestamp = millis();
#endif
    task_100ms();
#ifdef RUNTIME
    RT_100ms(timestamp, Schedule_rtbuf100ms, sizeof(Schedule_rtbuf100ms)/sizeof(Schedule_rtbuf100ms[0])); 
#endif
    RtosTimePrev = RtosTimeNext;
    RtosTimeNext = RtosTimeNext + 100;
    Count1000ms ++;
    if (Count1000ms == 10) {
#ifdef RUNTIME
      timestamp = millis();
#endif
      task_1000ms();
      Count1000ms = 0;
#ifdef RUNTIME
      Print_Runtime(Schedule_rtbuf100ms, sizeof(Schedule_rtbuf100ms) / sizeof(Schedule_rtbuf100ms[0]), Rt_1000ms(timestamp));
#endif
    }
  } 

}

//============== task containers ==================================

void init_processes() { 
  // put your init tasks here
    Serial.begin(9600);
   while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
   }
  Diag_init();
  Diag_WdInit();
  ComCom_init();
  ComTel_init();
  GetTime_init();
  Humid_init();
  Irrig_init();
  RelayCtrl_init(); 
}

void task_100ms() { 
  // add your 100ms processes here
  ComTel_Stm_100ms();
}

void task_1000ms() { 
  // add your 1000ms processes here
  GetTime_Loop();
  Irrig_Stm_1s();
  Humid_1s(); 
  Diag_CycTask();
  Diag_MonIrrigTime();                     // keep Irrig_Stm_1s() before Diag_MonIrrigTime()
  Diag_Watchdog(false);
  RelayCtrl_1s();                   // keep Diag_MonIrrigTime() before RelayCtrl_1s()
//  Serial.println("1stask");
}

void task_Bg() {
  // add the background processes here
}


void init_rtos_tasks() { /*debugInit
  task_1000ms();
  task_100ms(); */
}

//============ Runtime Measurement ==========================================


#ifdef RUNTIME


int Rt_1000ms(unsigned long starttime){
  return (int) (millis() - starttime);
}
void RT_100ms(unsigned long starttime, int* rtbuf, int lengthb){
  static int i = 0;
  rtbuf[i] = (int) (millis() - starttime);
  i = (i + 1) % lengthb;
}

void Print_Runtime(int* rtbuf, int lengthbuf, int rt1000ms){
  int mean, maxim;
  mean = 0;
  maxim = 0; 
  for (int i = 0; i < lengthbuf; i++){
      mean = mean + rtbuf[i];
      mean = mean +  Schedule_rtbuf100ms[i]; 
      maxim = max(maxim, rtbuf[i]); 
  }
  mean = mean / lengthbuf; 
  Serial.print(F("mean ")); 
  Serial.println(mean);
  Serial.print(F("max "));
  Serial.println(maxim);
  Serial.print(F("Rt1k "));
  Serial.println(rt1000ms);
}

#endif 
