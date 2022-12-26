/*
 * 
IrrigationSys_5.4 
- known issue; F-Macro shall not be used for client.print, for help see here: https://werner.rothschopf.net/microcontroller/202011_arduino_webserver_optimize_en.htm
               as it works fine althoug the overhead I don't touch this now
- change; In case of no valid NTP time packet, the local time is incremented; before that the time got invalid and so frequently the irrigation did not work due to NTP issues
- change; diagnosis of GetTime is sensitive to expeced but not received NTP packages, while used to be sensitive to the fault value 99:99
- change; new option in diag menu: Reset µC from telnet user front end
- change; GetTime; in case no valid NTP time packet processed for ~ 3 weeks time a device reset is triggered by watchdog


IrrigationSys_5.3
- Bugfix: main relay does not open in between two valves of a sequence

IrrigationSys_5.2
- RelayCtrl: switch 24V relais only during 230V relais off (EMC issue)
 
IrrigationSys_5.1
- Humid changed to a 0 - 3V sensor

IrrigationSys_5.0
- bugfix for the "connection refused" issue merged from IrrigationSys_4.1deb_1
- F()-macro used to reduce ram 
- print of setup data improved
- discard of setup data in case of [a]bort implemented; (back to data from EEPROM)


IrrigationSys_4.9
- long term test results based on 4.8: 
  - diag fcns ok
  - irrig history fcn ok
  - new lawn repetitions ok
  - setup: display of current setup values required
  - observation: "connection refused" details see mind map
    - H1: port scan from LAN or Internet or attack from internet connects with the client
      - connection counter implemented 
  - retest required: starting ad hoc cycle from off and check that state is off afterwards; test passed --> ok
 
  

IrrigationSys_4.8
- Caution: in irrig some weird copy / paste fault happened; retest carefully or compare with V4.7
- Things left from 4.7
- ComTelnet: irrigation History output implemented, not tested


IrrigationSys_4.7
- Relay control: bug fixed
- repetition cycles implemented in auto
- open point: ad hoc from autoWait still reasonable? It could cause trouble with the start times
- test results
  1) after finishing an ad hoc cycle the stat was = auto; not sure if I had started it from off or from auto --> retest
  2) test with start time 19:00, 20 mins duration and 3 repetitions
  Result: failed; no repetition observed; state always auto, no reset (as Diag_monTotalIrrigTime contains reasonable time); no fault entry; bug fixed but not tested
- definitions in Irrig for irrigation history done (no algo yet)

IrrigationSys_4.6
- DFC Monitoring (Modul Diag) implemented and testd
- ComTelnet: In Setup: Parameter für Anzahl der Wiederholungen eingeführt
- Diag: monitoring function implemented and tested
- Relay Control: Bug in main relay control causes cyclic reset, likely from the WD
- yet to come: new lawn cycle repetition

IrrigationSys_4.5
-Watchdog implemented and tested
-power up plaus diagnosis implemented
-RAM size reduced by using the F() Macro

IrrigationSys_4.4
- Diag EEPROM handling tested; diag base functions finished

IrrigationSys_4.3
- Diag generic functions finished but EEPROM handling
- Diag EEPROM handling under construction
- Diag HMI finishend and tested

IrrigationSys_V4.2
- diagnosis bugs of V4.1 fixed and tested

 IrrigationSys_V4.1
- diagnosis fcns (buggy) deactivated in scheduler
- address of upper relay board (valve 5 to 8) written to board; 8-channel mode activated 

 
IrrigationSys_V4.0
- bugs in manual mode fixed (no valves switched on; once on ought to swich on all of them switched on)

IrrigationSys_V3.3
- Diag implemented for common functions and for Humid; build successful; smoke test of non diag fcns passed; 
  - EEPROM handling still to be implemented
  - VDD5V diagnosis would be nice to have in addition
  - diag stuff not tested yet not tested at all
- see also pending stuff from comments in V3.2

IrrigationSys_V3.2
- Deep setup of IP adresses implemented in ComCom and ComTelnet; GetTime impl still missing
- Reading MAC address from the device is yet to be implemented
- 74% of RAM is occupied and still features like som diagnosis and the "new lawn" feature are missing... 


IrrigationSys_V3.1
- GetTime: successful trial (deactivated after testing) for assigning the ip adress for udp calls during runtime; search for "trial"

IrrigationSys_V3.0
- Irrig: humidity condition added for transition from wait to auto irrig
- dsq struct removed

IrrigationSys_V3.0
- after the problem with compiling starting from V2.0 below, all contents were copied into new files and activated one by one --> ok without any code change; Hypothesis: bug of the editor, as this shows strange formatting issues sometimes
- first tests show positive results


Trial_DefSequenceV1_2
- just a copy from 1.1



Trial_DefSequence
V1.1
- based on Sched V2.0 as below
- File by file content copy and paste from V2.0
- TrialDefSequenceV1.0: All "debugInit" comments turned back into code and compiled successfully
- still successful for DebugComCom, but as soon as I  took in parts of DebugComTelnet the same issue happened; turning back the things into comments --> problem persists ... 
- next trial: remove Module by module and stub interfaces


IrrigationSys_V2.0
V2.0
- RelayCtrl (control the valves) added
- state and transition corrections @ Irrig
- most of fcns converted into comments with key word "debugInit"



IrrigationSys_V1.0
V1.0
- all components but the i²c multipexer implemented
- diagnosis yet to be developed
- smaller issues are listed in the tabs

 */
