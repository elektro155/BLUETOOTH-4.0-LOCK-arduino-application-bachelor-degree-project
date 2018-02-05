#include <RTClib.h>
#include <Wire.h>
#include "MicroLCD.h"
#include <Eeprom24C32_64.h>
#include <Arduino.h>
#include <String.h>
#include <SoftwareSerial.h>
#include "keys.h"

#define OPEN 1 //for setting state of relay
#define CLOSE 0 //for setting state of relay
#define BUZZER 3 //buzzer to arduino pin 3
#define RESPONSE_DELAY 500 //response after action to Android 
#define EEPROM_ADDRESS 0x50 //address of 24C32N

////////////////////////////////CLASSES AND OBJECTS////////////////////////////////////////

LCD_SSD1306 lcd1; /* for SSD1306 OLED module */
DateTime now (__DATE__, __TIME__);
RTC_DS1307 RTC;
SoftwareSerial mySerial(12, 13); // RX, TX for UART
static Eeprom24C32_64 eeprom(EEPROM_ADDRESS); //Eeprom driver

class Mem{  //class for easy access to eeprom memory
  private:
    const word buzzer_address = 0x10;  //1B, word = unsigned int(2B)
    const word delay_address = 0x13;  //2B
    const word password_address = 0x2A;  //8B

  public:
    /////////////////////setters///////////////////////
    void setBuzzer(bool a){
      eeprom.writeByte(buzzer_address,a);
      delay(10);
    }
    void setDelay(unsigned int a){
      byte data[2]={0,0};
      data[0] = (byte) (a & 0xFF);  //little endian, LSB
      data[1] = (byte) ((a >> 8) & 0xFF); //MSB
      eeprom.writeBytes(delay_address,2,data);
      delay(10);
    }
    void setPassword(String ps){
      byte data[8]={0};
      for (int i =0;i<8;i++){
        data[i]=(char)ps[i];
      }
      eeprom.writeBytes(password_address,8,data);
      delay(10);
    }
    /////////////////////getters//////////////////////
    bool getBuzzer(){
      return eeprom.readByte(buzzer_address);
    }
    unsigned int getDelay(){
      byte out[2]={0};
      eeprom.readBytes(delay_address, 2, out);
      return (unsigned int)out[1]*256+out[0]; //little endian
    }
    String getPassword(){
      String out = "testtext";
      byte dataOut[8]={0};
      eeprom.readBytes(password_address,8,dataOut);
      for(int i=0;i<8;i++){
        out[i]=(char)dataOut[i];
      }      
      return out;
    } 
};
Mem mem; //object of it

///////////////////////struct for containing data from EEPROM///////////////////////////
struct SavedData{
  bool saved_buzzer;
  unsigned int saved_delay;
  String saved_password;
};
SavedData saved;

//////////////////////////////////driving relay//////////////////////////////////////////
void relay(bool state){
  if (state == 0) digitalWrite(2, HIGH);  
  if (state == 1) digitalWrite(2, LOW);  
}

/////////////////////////////////BUZZER/////////////////////////////////////////////////
void buzzerTone(){
  if(saved.saved_buzzer){  //if buzzer is on
    tone(BUZZER, 1000); // Send 1KHz sound signal...
    delay(100);        // ...for 100 msec
    noTone(BUZZER);     // Stop sound...
    delay(10);
  }
}

///////////////////////////////////displaying time on OLED///////////////////////////////
void displayTime (){   
    lcd1.setCursor(0, 0);    
    lcd1.print(now.year());
    lcd1.print('/');
    if(now.month()<10) lcd1.print("0");
    lcd1.print(now.month());
    lcd1.print('/');
    if(now.day()<10) lcd1.print("0");
    lcd1.print(now.day());
    lcd1.print("   ");
    if(now.hour()<10) lcd1.print("0");
    lcd1.print(now.hour());
    lcd1.print(':');
    if(now.minute()<10) lcd1.print("0");
    lcd1.print(now.minute());
    lcd1.print(':');
    if(now.second()<10) lcd1.print("0");
    lcd1.print(now.second());
    lcd1.print("\n\r");
}

////////////////////////////////////////Opening the door//////////////////////////////////
void toggleKey(){
    static bool sw = true;
    if(sw){
      lcd1.draw(key_down,32,32,64,32); //void LCD_SSD1306::draw(const PROGMEM byte* buffer, byte x, byte y, byte width, byte height)
      sw = false;
    }else{
      lcd1.draw(key_up,32,32,64,32); //void LCD_SSD1306::draw(const PROGMEM byte* buffer, byte x, byte y, byte width, byte height)
      sw = true;
    }
}

void delayWithKeys(int delayTime){
    for(int i=0;i<delayTime;i++){
      delay(1);
      if(i%400==0){
        toggleKey();
      }
    }
    lcd1.draw(key_empty,32,32,64,32); //clear the area
}

void opening(int duration, bool ifOpen){
  if(duration>=(10+110+RESPONSE_DELAY) && duration<=20000){
    if(ifOpen){
          relay(OPEN);
          buzzerTone(); //adds 110 ms delay if buzzer is on
          lcd1.setCursor(0, 2);  //x,y  
          lcd1.print("OPENING            "); //OLED         
          mySerial.println("opened"); //UART, opened
          
          if(saved.saved_buzzer){ //if buzzer on then subtract the delay given by the buzzer
            delayWithKeys(duration-110);    
          }else{
            delayWithKeys(duration);
          }
          
          relay(CLOSE);
          Serial.print("DOOR HAS BEEN OPENED\n"); //BT, application disconnects   
          lcd1.setCursor(0, 2);  //x,y  
          lcd1.print("OPEN BY ANDROID APP"); //OLED 
          mySerial.println("closed"); //UART, closed          
    }else{
          lcd1.setCursor(0, 2);  //x,y 
          lcd1.print("WRONG PASSWORD     "); //OLED
          mySerial.println("wrong password"); //UART, error wrong code
          delay(RESPONSE_DELAY);          
          Serial.print("WRONG PASSWORD\n"); //BT, must be for breaking connection
          delay(1000); //fixed delay
          lcd1.setCursor(0, 2);  //x,y  
          lcd1.print("OPEN BY ANDROID APP"); //OLED          
    }
  }else{ //for diagnostic purpose when the wrong time was set
          lcd1.setCursor(0, 2);  //x,y  
          lcd1.print("WRONG TIME         "); //OLED
          if (duration<(10+110+RESPONSE_DELAY)){  //time too short, less than 620 ms
             mySerial.println("delay too short"); //UART, error time too short
          }
          else if (duration>20000){  //time too long, more than 620 + 200000 ms
             mySerial.println("delay too long"); //UART, error time too long
          }
          delay(2000); //fixed time
    }
}

//////////////////////////////////initialization of module///////////////////////////////

void setup() {
   //RELAY init//
   pinMode(2, OUTPUT); 
   digitalWrite(2, HIGH);  //to prevent opening lock at power-on

   //bluetooth init//
   Serial.begin(115200);  //init communication with bluetooth module 
   Serial.setTimeout(50);  //to make BLE work fast, time of reading form Android in ms
   
   //EEPROM init//
   Wire.begin();
   eeprom.initialize();
   
   //RTC init//
   RTC.begin();
   
   //Getting settings from EEPROM//
   saved.saved_buzzer = mem.getBuzzer();
   saved.saved_delay = mem.getDelay();
   saved.saved_password = mem.getPassword();

   //OLED init//
   lcd1.begin(); //oled
   lcd1.clear(); //oled
   lcd1.setCursor(0, 2);  //x,y  
   lcd1.print("OPEN BY ANDROID APP"); //OLED

   //UART init//
   mySerial.begin(4800);
   mySerial.setTimeout(200);  //to make UART work faster
   mySerial.println("device is ON");
  
   //FINISHED//
   buzzerTone();
}

///////////////////////////////MAIN LOOP/////////////////////////////////////////////////

void loop() {
  ////////////////////////////REFRESHING RTC//////////////////////////
  now = RTC.now(); 
  displayTime();  
  
  ////////////////////////////////////////////////////////////////////
  String password; //variable for storing password from Android application
  String UARTcodeIn; //variable for storing command from mySerial UART

    //////////////////////////opening by android app/////////////////////////////////////
    if(Serial.available())
    {
        password=Serial.readString(); 
        password.remove(0,5); //remove "pswd:" at the beginning     
        
        if (password==saved.saved_password){
            opening(saved.saved_delay,true);
        }else{
            opening(saved.saved_delay,false);
        }
    }

    /////////////////////uart commands receiving///////////////////////////////////////// 
    if(mySerial.available())
    {
        UARTcodeIn=mySerial.readString();

        ///////////////////////opening by PC console////////////////////////
      
        if(UARTcodeIn=="open\r\n"){  //open
            opening(2000,true); 

        /////////////////////checking if device is on//////////////////////

        }else if(UARTcodeIn=="isON\r\n"){ //asking whether device is on
            mySerial.println("device is ON"); //"Yes, I'm ON"
            
        ///////////////////////getter commands/////////////////////////////   
         
        }else if(UARTcodeIn=="get delay\r\n"){  //get current delay
            mySerial.println("delay:");//command that delay value will be sent
            mySerial.println((String)mem.getDelay()); //send current delay

        }else if(UARTcodeIn=="get buzzer\r\n"){  //get buzzer state
            mySerial.println("buzzer:");//command that buzzer state will be sent
            mySerial.println((String)mem.getBuzzer()); //send current delay  
             
        }else if(UARTcodeIn=="get password\r\n"){  //get password
            mySerial.println("password:");//command that password will be sent
            mySerial.println(mem.getPassword()); //send current delay  
                
        ///////////////////////setter commands/////////////////////////////
        
        }else if(UARTcodeIn=="set delay\r\n"){   //set delay time
            while(!mySerial.available()){}  //wait for command
            UARTcodeIn=mySerial.readString();
            unsigned int tm =UARTcodeIn.toInt();
            if (tm<(10+110+RESPONSE_DELAY)){  //time too short, less than 620 ms
               mySerial.println("delay too short"); //UART, error time too short
            }
            else if (tm>20000){  //time too long, more than 620 + 200000 ms
               mySerial.println("delay too long"); //UART, error time too long
            }else{              
                  mem.setDelay(tm); //save value to EEPROM
                  saved.saved_delay = mem.getDelay(); //save to struct
                  mySerial.println("delay changed"); //delay changed successfully
            }    

        }else if(UARTcodeIn=="set buzzer\r\n"){  //set buzzer
              while(!mySerial.available()){}  //wait fot command
              UARTcodeIn=mySerial.readString();
              bool bz = (bool)UARTcodeIn.toInt();
                  mem.setBuzzer(bz); //save value to EEPROM
                  saved.saved_buzzer = mem.getBuzzer(); //save to struct
                  mySerial.println("buzzer changed"); //changing buzzer ok 
              if(saved.saved_buzzer) buzzerTone(); //beep if has been set to on state
                 
        }else if(UARTcodeIn=="set password\r\n"){  //set password
              while(!mySerial.available()){}  //wait fot command
              UARTcodeIn=mySerial.readString();
              String newPassword = UARTcodeIn;
              newPassword.remove(newPassword.length()-2,2); //remove the last two signs that is \r\n (in case of PC YAT console)
              if(newPassword!=NULL && newPassword.length()>0 && newPassword.length() <=8){                 
                   mem.setPassword(newPassword); //save to EEPROM
                   saved.saved_password = mem.getPassword();//save to struct    
                   mySerial.println("password changed"); //changing password ok               
              }else if(newPassword.length()>8){
                   mySerial.println("password too long"); //password too long
              }else if(newPassword == NULL || newPassword.length() == 0){
                   mySerial.println("password too short"); //password too short
              }
        }else if(UARTcodeIn=="set time\r\n"){  //set time
              uint16_t year = 2017;
              uint8_t month = 12;
              uint8_t day = 29;
              uint8_t hour = 16;
              uint8_t min = 2;
              uint8_t sec = 40;
              int daysInMonth = 31;
              String newTime="";
              
              mySerial.println("enter year");
              while(!mySerial.available()){}  //wait fot command
              UARTcodeIn=mySerial.readString();
              newTime = UARTcodeIn;
              newTime.remove(newTime.length()-2,2); //remove the last two signs that is \r\n (in case of PC YAT console)
              if(newTime.toInt()>1999&&newTime.toInt()<2100) year = newTime.toInt();
              else mySerial.println("wrong input data");

              mySerial.println("enter month (number)");
              while(!mySerial.available()){}  //wait fot command
              UARTcodeIn=mySerial.readString();
              newTime = UARTcodeIn;
              newTime.remove(newTime.length()-2,2); //remove the last two signs that is \r\n (in case of PC YAT console)
              if(newTime.toInt()>0&&newTime.toInt()<=12) month = newTime.toInt();
              else mySerial.println("wrong input data");

              mySerial.println("enter day");
              while(!mySerial.available()){}  //wait fot command
              UARTcodeIn=mySerial.readString();
              newTime = UARTcodeIn;
              newTime.remove(newTime.length()-2,2); //remove the last two signs that is \r\n (in case of PC YAT console)
                if(month == 1 || month == 3 || month ==5 || month ==7 || month == 8 || month == 10 || month ==12) daysInMonth = 31;
                else if (month == 4 || month == 6 || month == 9 || month == 11) daysInMonth = 30;
                else if (month == 2){
                  if((year % 4 == 0) && (year % 100 != 0) || (year % 400 == 0)) daysInMonth = 29;
                  else daysInMonth = 28;
                }
              if(newTime.toInt()>0&&newTime.toInt()<=daysInMonth) day = newTime.toInt();
              else mySerial.println("wrong input data");

              mySerial.println("enter hour");
              while(!mySerial.available()){}  //wait fot command
              UARTcodeIn=mySerial.readString();
              newTime = UARTcodeIn;
              newTime.remove(newTime.length()-2,2); //remove the last two signs that is \r\n (in case of PC YAT console)
              if(newTime.toInt()>=0&&newTime.toInt()<24) hour = newTime.toInt();
              else mySerial.println("wrong input data");

              mySerial.println("enter minutes");
              while(!mySerial.available()){}  //wait fot command
              UARTcodeIn=mySerial.readString();
              newTime = UARTcodeIn;
              newTime.remove(newTime.length()-2,2); //remove the last two signs that is \r\n (in case of PC YAT console)
              if(newTime.toInt()>=0&&newTime.toInt()<60) min = newTime.toInt();
              else mySerial.println("wrong input data");

              mySerial.println("enter seconds");
              while(!mySerial.available()){}  //wait fot command
              UARTcodeIn=mySerial.readString();
              newTime = UARTcodeIn;
              newTime.remove(newTime.length()-2,2); //remove the last two signs that is \r\n (in case of PC YAT console)
              if(newTime.toInt()>=0&&newTime.toInt()<60) sec = newTime.toInt();
              else mySerial.println("wrong input data");

              RTC.adjust(DateTime(year, month, day, hour, min, sec));
              mySerial.println("time has been set");  
                               
        }else{
              mySerial.println("WRONG UART COMMAND"); //wrong command
              lcd1.setCursor(0, 2);  //x,y  
              lcd1.print("WRONG UART COMMAND "); //OLED
              delay(1000);
              lcd1.setCursor(0, 2);  //x,y  
              lcd1.print("OPEN BY ANDROID APP"); //OLED  
        }
    }//uart commands receiving     
}


