//==========================about this module==========================
// The module initializes ethernet communication.
// Other modules like ComTelnet and GetTime built on

//========================== known issues ==================================
// mac address shall be read from the device in order to make the software runable on all devices

//========================= basic definitions ComCom=======================
#define MAC_ADR 0xA8, 0x61, 0x0A, 0xAE, 0x14, 0x44 

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network.
// gateway and subnet are optional:
// byte mac[] = {
//  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

byte mac[] = {MAC_ADR};
const int ComCom_DefaultIpButton = 3; // see specification document for pin assignments

//===================== public global variables provided by ComCom=================

byte ComCom_Ip[COMCOM_IPADRNO][4] = {
                        {192, 168, 178, 47},  // ip
                        {192, 168, 178, 1},   // dns
                        {192, 168, 178, 1},   // gateway
                        {255, 255, 255, 0},   // subnet mask
                        {192, 168, 178, 1}    // time server                        
                      }; 

/* that is how the example on the arduino site looks like. Its disadvantage is that assignments during runtime are not possible
IPAddress ip(192, 168, 178, 47);
IPAddress myDns(192, 168, 178, 1);
IPAddress gateway(192, 168, 178, 1);
IPAddress subnet(255, 255, 255, 0);
*/

//====================== Initializations (to be included into void(setup)======================================
void ComCom_init() {
  byte buf;
  pinMode(ComCom_DefaultIpButton, INPUT);
  // You can use Ethernet.init(pin) to configure the CS pin
  Ethernet.init(10);  // Most Arduino shields
  //Ethernet.init(5);   // MKR ETH shield
  //Ethernet.init(0);   // Teensy 2.0
  //Ethernet.init(20);  // Teensy++ 2.0
  //Ethernet.init(15);  // ESP8266 with Adafruit Featherwing Ethernet
  //Ethernet.init(33);  // ESP32 with Adafruit Featherwing Ethernet

  // initialize the ethernet device
  //IPAddress ip(192, 168, 178, 47);
//  Ethernet.begin(mac, ip, myDns, gateway, subnet);
  if (!digitalRead(ComCom_DefaultIpButton)) {                           // if the button for "factory default IP adress" is not pushed, load the IP adresses and masks from the EEPROM
    for (int loopCtr = EEPROM_IpAdr; loopCtr < EEPROM_IpAdr + COMCOM_IPADRNO * 4; loopCtr ++) {
      ComCom_Ip[(loopCtr - EEPROM_IpAdr) / 4][(loopCtr - EEPROM_IpAdr) % 4] = EEPROM.read(loopCtr);
    }  
    Serial.println(F("False"));  
  }
  /*
#ifdef DEBUG_COMTELNET_MAX
  for (int i = 0; i < COMCOM_IPADRNO; i++) {
    for (int j = 0; j < 4; j++) {
      Serial.print(ComCom_Ip[i][j]);
    }
    Serial.println();    
  }      
#endif */
    Ethernet.begin(mac, ComCom_Ip[0], ComCom_Ip[1], ComCom_Ip[2], ComCom_Ip[3]);        // with addresses & mask either from definition above (factory default) or from user settings (EEPROM)
  // Check for Ethernet hardware present
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println(F("Ethernet shield was not found.  Sorry, can't run without hardware. :("));
    while (true) {
      delay(1); // do nothing, no point running without Ethernet hardware
    }
  }
  if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println(F("Ethernet cable is not connected."));
  }
  // debug area starts here: 

}
