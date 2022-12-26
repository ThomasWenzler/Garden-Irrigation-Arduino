// ===================== about this module===============================================
// The module provides time as global variable GetTime_Mez and a function to convert it into a string;  will update every GETTIME_UPD_DUTYCYC seconds; in case the received raw time is invalid, time is incremented locally 
// Implementation: time is obtained by sending a request over UDP to the time server in the local network (here: Fritz Box)
// it builds on the basic ethernet functions of module ComCommon
// the core code for UDP handling and time calculations was copied from the arduino UDP example (created 4 Sep 2010 by Michael Margolis modified 9 Apr 2012 by Tom Igoe) and then modified here 
// https://github.com/arduino-libraries/MKRNB/blob/master/examples/GPRSUdpNtpClient/GPRSUdpNtpClient.ino
// https://networklessons.com/cisco/ccie-routing-switching/cisco-network-time-protocol-ntp
//----------------------------------------------------------------------------------------------------

// ==================== known issues ======================================================================
// ip adress from EEPROM not implemented yet

// ============================= basic definitions ================================================
IPAddress GetTime_TimeServer(192, 168, 178, 1); // Fritz Box im LAN
//byte GetTime_TimeServer[] = { 192,168,178,1 };   // trial (successful) to replace the definition by IPAdress GetTime_...; this is a way to modify the ip adress during runtime which is not possible when using instanciation by "IPAdress  "
//IPAddress GetTime_TimeServer(); // trial

const byte GetTime_offsetMezUtz = 2; // MEZ = (UTZ + GetTime_offsetMezUtz) % 24
const unsigned int localPort = 2390;      // local port to listen for UDP packets

#define GETTIME_UPD_DUTYCYC 32    // Dutiy cycle for getting time from time server
#define GETTIME_NOPACK_KNOCKOUT 65700   // (* GETTIME_UPD_DUTYCYC (=32) is equal to 3 weeks time; after 3 weeks time no NTP package parsed successufully, the device shall reset

#define GETTIME_DFCPLAUS_THRESH 6           // fault debounce events; after GETTIME_DFCPLAUS_THRESH times no valid NTP package parsed (GETTIME_DFCPLAUS_THRESH * GETTIME_UPD_DUTYCYC in seconds) the fault flag will be set

//===================== public global variables provided by GetTime ==============================================

byte GetTime_Mez[3]; // { h, m, s }; contains { 99, 99, 99 } in case of invalid time
dfc GetTime_DfcPlaus = {0,GETTIME_DFCPLAUS_THRESH,0,0};     //diagnostic fault code type, see typedef in IrrigationSys_Vx.y

//===================== global variables used from other modules===============================

// extern type xyz; none


//================================ internal definitions ====================================================
const int GetTime_NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte GetTime_packetBuffer[ GetTime_NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

EthernetUDP Udp; // why does the class identifier not appear in that nice orange colour?

//==============================================Init=================================================================
void GetTime_init() {                                 //attach it after ComCom_init()
//  IPAddress GetTime_TimeServer(192, 168, 178, 1); //trial
//  delay(5000); //trial
  Udp.begin(localPort);
//  GetTimeResultMez(false);                           // this writes invalid values to the time variable GetTime_Mez[3] as first valid time is available only after about 11s.
  GetTime_Mez[0] = 99;
  GetTime_Mez[1] = 99;
  GetTime_Mez[2] = 99; 
}

//============================================= Loop=================================================================
// public 
void GetTime_Loop() {             // reccommended to be put into the 1s task; it will trigger a NTP request every 10s and 1s after the trigger it listens whether a package has come in
  static int GetTime_CountSchedScale = 0;
  static int GetTime_CountKnockOut = 0;
  static boolean GetTime_ReadUdpNow = false;

  GetTime_CountSchedScale ++;
  //Serial.println(GetTime_CountSchedScale);
  if (GetTime_ReadUdpNow == true) {                                     // read UDP answer and interpret its content
#ifdef DEBUG_GETTIME_BASIC
      Serial.println(F("parsePacket"));
#endif
    // wait to see if a reply is available
    if ( Udp.parsePacket() ) {
      //Serial.println("packet received");
      // We've received a packet, read the data from it
      Udp.read(GetTime_packetBuffer, GetTime_NTP_PACKET_SIZE); // read the packet into the buffer
      GetTimeResultMez(true);
      GetTime_ReadUdpNow = false;           
    }
    else {
      GetTimeResultMez(false);
    }
  }
  if (GetTime_CountSchedScale % GETTIME_UPD_DUTYCYC == 0) {                     // trigger a NTP request every GETTIME_UPD_DUTYCYC'th call
    GetTime_sendNTPpacket(GetTime_TimeServer); // send an NTP packet to a time server
    if (GetTime_ReadUdpNow) {                           // if the flag is still set at this time the expected NTP packet could not be received or processed
      GetTime_CountKnockOut ++;
      if (GetTime_CountKnockOut >= GETTIME_NOPACK_KNOCKOUT){
        Diag_Watchdog(true);                                        //reset the device
      }
    }
    else {
      GetTime_CountKnockOut = 0;
    }
    Diag_DebounceUpDown(&GetTime_DfcPlaus, GetTime_ReadUdpNow == true);  //plausibility diagnosis        condition: no valid NTP package parsed since last NTP request
    GetTime_ReadUdpNow = true;
#ifdef DEBUG_GETTIME_BASIC
    Serial.println(F("SendNTPp"));
#endif
    // wait to see if a reply is available
  }
}

// =================================fcns==================================================================
//private                      interprets the received UDP package from time server; Writes time into the byte GetTime_Mez[3]: [h, m, s]; 
void GetTimeResultMez(boolean rawTimIsvalid) { 
  static unsigned long secsSince1900;
  if (rawTimIsvalid) {
    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:
    unsigned long highWord = word(GetTime_packetBuffer[40], GetTime_packetBuffer[41]);
    unsigned long lowWord = word(GetTime_packetBuffer[42], GetTime_packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    secsSince1900 = highWord << 16 | lowWord;
    //Serial.print("Seconds since Jan 1 1900 = " );
    //Serial.println(secsSince1900);
  }
  else {                                                            // if unexpectedly no valid raw time package, then get it from the "model" 
    secsSince1900 ++;
  }
  // now convert NTP time into everyday time:
  //Serial.print("Unix time = ");
  // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
  const unsigned long seventyYears = 2208988800UL;
  // subtract seventy years:
  unsigned long epoch = secsSince1900 - seventyYears;
  // print Unix time:
  //Serial.println(epoch);
  // print the hour, minute and second:
#ifdef DEBUG_GETTIME_MAX
  Serial.print(F("The UTC time is "));       // UTC is the time at Greenwich Meridian (GMT)
#endif
  //Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
  //Serial.print(':');
  GetTime_Mez[0] = (byte)(((epoch  % 86400UL) / 3600 + GetTime_offsetMezUtz) % 24);  // write MEZ hh to global variable
#ifdef DEBUG_GETTIME_MAX
  Serial.print(GetTime_Mez[0]); 
  Serial.print(':');
#endif
  if ( ((epoch % 3600) / 60) < 10 ) {
    // In the first 10 minutes of each hour, we'll want a leading '0'
#ifdef DEBUG_GETTIME_MAX
    Serial.print('0');
#endif
  }
  //Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
  GetTime_Mez[1] = (byte)((epoch  % 3600UL) / 60);     
#ifdef DEBUG_GETTIME_MAX
  Serial.print(GetTime_Mez[1]); 
  Serial.print(':');
#endif
  if ( (epoch % 60) < 10 ) {
    // In the first 10 seconds of each minute, we'll want a leading '0'
#ifdef DEBUG_GETTIME_MAX
    Serial.print('0');
#endif
  }
  //Serial.println(epoch % 60); // print the second
  GetTime_Mez[2] = (byte)(epoch % 60UL); 
#ifdef DEBUG_GETTIME_MAX
  Serial.println(GetTime_Mez[2]);              
#endif
}
  
//public **************** returns a pointer to a string with the time in hh:mm:ss ***********************           
void GetTime_MezAsCharArray(byte *Mez, char* buf) {
  //char buf[9];  //make sure to allocate space for the zero terminator
  //snprintf(buf,sizeof(buf),"%02d:%02d:%02d",Mez[0],Mez[1],Mez[2]);       AAARGH! sizeof() is a compile-time function. So it will assume 1 instead of the buf[] real length. 
  snprintf(buf,9,"%02d:%02d:%02d",Mez[0],Mez[1],Mez[2]);
  //Serial.println(buf);  //prints 03:04:54
}
/*
String * GetTime_PtrMezString(byte *Mez){
  static String timeString;
  timeString = "";
  timeString += String(Mez[0]);
  if (Mez[1] < 10){
    timeString += ':0';  // adds '0' as a leading digits for minutes if < 10
  }
  else {
    timeString += ':';
  }
  timeString += String(Mez[1]);
  timeString += ':';
  timeString += String(Mez[2]);
  return &timeString;  
}
*/
//private
unsigned long GetTime_sendNTPpacket(IPAddress& address)  //Send a standard NTP request
//unsigned long GetTime_sendNTPpacket(byte* address)  //trial (successful)
{
  //Serial.println("1");
  // set all bytes in the buffer to 0
  memset(GetTime_packetBuffer, 0, GetTime_NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  //Serial.println("2");
  GetTime_packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  GetTime_packetBuffer[1] = 0;     // Stratum, or type of clock
  GetTime_packetBuffer[2] = 6;     // Polling Interval
  GetTime_packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  GetTime_packetBuffer[12]  = 49;
  GetTime_packetBuffer[13]  = 0x4E;
  GetTime_packetBuffer[14]  = 49;
  GetTime_packetBuffer[15]  = 52;

  //Serial.println("3");

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  //Serial.println("4");
  Udp.write(GetTime_packetBuffer, GetTime_NTP_PACKET_SIZE);
  //Serial.println("5");
  Udp.endPacket();
  //Serial.println("6");
}
