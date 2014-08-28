/*  (Dev)   -  (Pi)
    SDA     -  SDA
    SCL     -  SCL
    GND     -  GND
    VCC     -  3.3V
    Note: Check your pin out
    Note: Make sure you connect the PI's 3.3 V line to the HTU21DF boards Vcc 'IN' line not the 3.3v 'OUT' 
    
    How to compile, @ command line type
    
        gcc -Wall -lm -o htu21dev4 ./htu21dev4.c

    the '-lm' is required for 'math.h'
    
    for constants such as O_RWRD or I2C_M_RD checkout i2c.h & i2c-dev.h
    this also contains the definition of 'struct i2c_msg' so if you want to see what is
    possible check it out. 
    also have a look at
    
>>>>>  https://www.kernel.org/doc/Documentation/i2c/i2c-protocol <<<<<<<<<< NB! read it 
    
    There are no examples of the 'Write User Register' available, so back to basics and RTFM to
    create it from the above documentation.
    
    In general communication functions return an integer < 0 on failure 0 for success and > 0 if a 'handle' is
    being returned. If a value of a conversion is required it is return via a variable passed to the
    function 'by-ref' ie a pointer. look in 'main' for 'MyTemp' and 'MyHum'.
    
    Conversion functions return a double
    
    I have mixed float and double types, should change everything to double (math.h needs it most)
    this is more for consistency and style then any other reason.
    
    PS there are better dew point formulae.
        
    Use as you see fit.
    
    Eric Maasdorp 2014-08-28
*/
     
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <math.h>
#include "htu21dev4.h"     

#define sleepms(ms)  usleep((ms)*1000)   
#define I2CBus             "/dev/i2c-1"      //New Pi's 
//#define I2CBus             "/dev/i2c-0"    //Old, but not stale Pi's

// Returns a file id for the port/bus
int i2c_Open(char *I2CBusName){
  int fd;
  //Open port for reading and writing
  if ((fd = open(I2CBusName, O_RDWR)) < 0){
    printf ("\n");
    printf ("%s : Failed to open the i2c bus, error : %d\n",__func__,errno);
    printf ("Check to see if you have a bus: %s\n",I2CBusName); 
    printf ("This is not a slave device problem, I can not find the bus/port with which to talk to the device\n"); 
    printf ("\n");
    // Only one of the following lines should be used
    // the second line allows you to retry another bus, PS disable all the printf 's 
    exit(1);      //Use this line if the function must terminate on failure
    //return fd;  //Use this line if it must return to the caller for processing
    }
    else{
     return fd;
    }
}

void i2c_commonsenderror(int fd,uint8_t DeviceAddress){
   printf("\n");
    printf("This means that device with address :0x%0x failed to receive this command\n",DeviceAddress);
    printf("This command was preceded by others if they worked\n");
    printf("and this failed, then possible causes are delay timing to short (overclock stuffing timing up)\n");
    printf("or bus unstable ,wire length to long,power supply unstable, terminating resistors missing or incorrect.\n");
    printf("\n");
}    

//START: htu21df specific code

int htu21df_crcdata(const uint8_t *Data, uint16_t *ConvertedValue){
  uint32_t dataandcrc;
  // Generator polynomial: x**8 + x**5 + x**4 + 1 = 1001 1000 1
  *ConvertedValue = 0;

  const uint32_t poly = 0x98800000;
  int i;
  
  //Check how many bytes are there (expect 3 got 4???)  
  int nElements = sizeof(Data) / sizeof(Data[0]); 
  //What am I missing?
  //printf("Elements:%i\n",nElements);
  
  if (nElements != 4) return -1;        //Expected 3 why are there 4
  if (Data == NULL) return -1;
  dataandcrc = (Data[0] << 24) | (Data[1] << 16) | (Data[2] << 8);
  for (i = 0; i < 24; i++) {
    if (dataandcrc & 0x80000000UL) dataandcrc ^= poly;
    dataandcrc <<= 1;
  }
  // 2 Low bits are status, mask them 'C' > 1100b
  *ConvertedValue = ((Data[0] << 8) | Data[1]) & 0xFFFC;
  return (dataandcrc != 0);
}

int htu21df_ReadUserReg(int fd, int *retValue){
  uint8_t replymessage[2];
  int rc;
  struct i2c_rdwr_ioctl_data messagebuffer;
  uint8_t deviceAction=HTU21DF_READREG;

  //Build a user register read command
  //Requires a one complete message containing a command
  //and anaother complete message for the rely 
  struct i2c_msg read_user_reg[2]={
    {HTU21DF_I2CADDR,0,1,&deviceAction},
    {HTU21DF_I2CADDR,I2C_M_RD,1,replymessage}
  };
  
  messagebuffer.nmsgs = 2;                   //Two message/action
  messagebuffer.msgs = read_user_reg;        //load the 'read_user_reg' message into the buffer
  rc = ioctl(fd, I2C_RDWR, &messagebuffer);  //Send the buffer to the bus and returns a send status
  if (rc < 0 ){
    printf("\n");
    printf("%s :htu21df User Reg Read command failed with error :%d\n",__func__,errno);
    printf("This means that device with address :0x%0x failed to receive this command\n",HTU21DF_I2CADDR);
    printf("This command was preceded by a reset if that worked\n");
    printf("and this failed, then possible causes are Delay timing to short (overclock stuffing timing up)\n");
    printf("or bus unstable ,wire length,power supply unstable, terminating resistors.\n");
    printf("\n");
    // Only one of the following lines should be used
    //exit(1);       //Use this line if the function must terminate on failure
    return rc;       //Use this line if it must return to the caller for processing
  }
  *retValue = replymessage[0];
  return 0;
}

int htu21df_WriteUserReg(int fd,int NewValue){
  int UserRegVal;
  uint8_t datatosend[2];
  int rc;
  struct i2c_rdwr_ioctl_data messagebuffer;

  if ( htu21df_ReadUserReg(fd,&UserRegVal) != 0) return -1;

  //Preserve the reserved bits  
  UserRegVal &=0x38;
  UserRegVal |=(NewValue & 0xC7);  //Dont allow modification of bits 3,4,5
  
  //Unlike the read commands the write
  //requires the command and data to be sent in
  //one message  
  datatosend[0]=HTU21DF_WRITEREG;
  datatosend[1]=UserRegVal;

  //Build a user register write  command
  struct i2c_msg write_user_reg[1]={
    {HTU21DF_I2CADDR,0,2,datatosend}
  };
  
  messagebuffer.nmsgs = 1;                   //One message/action
  messagebuffer.msgs = write_user_reg;       //load the 'writ_user_reg' message into the buffer
  rc = ioctl(fd, I2C_RDWR, &messagebuffer);  //Send the buffer to the bus and returns a send status
  //printf("RC:%i\n",rc);
  if (rc < 0 ) return -1;
  return 0;
}

int htu21df_init(int fd){
  int rc;
  int UserReg;
  struct i2c_rdwr_ioctl_data messagebuffer;
  
  uint8_t deviceAction=HTU21DF_RESET;

  //Build a reset command for the htu21df
  struct i2c_msg reset[1] = {
    {HTU21DF_I2CADDR,0,1,&deviceAction}
  };
  
  messagebuffer.nmsgs = 1;                   //Only one message/action
  messagebuffer.msgs = reset;                //load the 'reset' message into the buffer
  rc = ioctl(fd, I2C_RDWR, &messagebuffer);  //Send the buffer to the bus and returns a send status
  if (rc < 0 ){
    printf("\n");
    printf("%s :htu21df Reset command 'sending' failed with error :%d\n",__func__,errno);
    printf("This means that device with address :0x%0x failed to receive this command\n",HTU21DF_I2CADDR);
    printf("Possible that the device is not connected to the bus specified, or the device is not 'live' due to wiring\n");
    printf("\n");
    // Only one of the following lines should be used
    // the second line allows you to retry, PS disable all the printf 's 
    //exit(1);       //Use this line if the function must terminate on failure
    return rc;       //Use this line if it must return to the caller for processing
  }

  // wait for the HTU21 to reset
  sleepms(HTU21DF_ResetDelay);
  
  if (htu21df_ReadUserReg(fd,&UserReg) == 0){
    /*
    if ( UserReg != 0x02){
      printf("\n");
      printf("%s :htu21df User Reg should have contained 2 but contained :%i\n",__func__,rc);
      printf("By this time you should be able to figure this one out yourself\n");
      printf("Hint it may not be an actual fault! RTFM\n");
      printf("\n");
      //exit(1);       //Use this line if the function must terminate on failure
      return 0;       //Use this line if it must return to the caller for processing
      }
     */ 
      return 0;
  }else{
    return -1;
    //exit (1);
  }
}

int htu21df_getValue(int fd, float *Value, const uint8_t YourBiddingMaster ){
  uint8_t replymessage[2]; //Appears this gets redimentioned in the ioctl call??
  int rc;
  uint16_t deviceValue;
  struct i2c_rdwr_ioctl_data messagebuffer;
  
  uint8_t deviceAction=0;
  
  switch (YourBiddingMaster)
  {
    case HTU21DF_READTEMP:
      deviceAction=HTU21DF_READTEMP;
      break;
    case HTU21DF_READHUM:
      deviceAction=HTU21DF_READHUM;
      break;
    default:
      return -1;
      //exit (1);
      break;
  }

  //Build a convert_start command for the htu21df
  struct i2c_msg convert_start[1] = {
    {HTU21DF_I2CADDR,0,1,&deviceAction}
  };
  
  messagebuffer.nmsgs = 1;                   //One message/action
  messagebuffer.msgs = convert_start;        //load the 'convert_start' message into the buffer
  rc = ioctl(fd, I2C_RDWR, &messagebuffer);  //Send the buffer to the bus and returns a send status
  //printf("RC:%i\n",rc);
  if (rc < 0 ){                              
    i2c_commonsenderror(fd,HTU21DF_I2CADDR);      
    return rc;
    //exit (1);
  }
  else{
    //Create a read 3 byte message
    struct i2c_msg read_value[1] = {
      {HTU21DF_I2CADDR,I2C_M_RD,3,replymessage}
    };
    int i=0;
    sleepms(HTU21DF_MinDelay);
    do{
      //Try 20 times pausing for HTU21DF_RetryDelay ie 20 * 5 ms + HTU21DF_MinDelay > 116 ms
      //Physical max times should be 50 ms but assuming usleep() does not work correctly during overclocking
      //required delay could be  50*1000MHz/700MHz = 72 ms which is less than 116 ms
      //dont stress max wasted time would be < HTU21DF_RetryDelay '5 ms'
      //dont reduce this time to much as you still have the bus speed to contend with
      //NO point in checking faster than the bus can operate.
      sleepms(HTU21DF_RetryDelay);
      messagebuffer.nmsgs = 1;
      messagebuffer.msgs = read_value;
      //Send the message, if data is ready for collection then collect
      rc = ioctl(fd,I2C_RDWR, &messagebuffer);
      //If not rc will be < 0 and we pause and try again        
      //printf ("Index:%i, RC:%i\t%i\t%i\t%i\n",i,rc,replymessage[2],replymessage[1],replymessage[0]);
      i++;
    }while (rc<0 && i <20);
    
    if (rc < 0 ){
      //printf("%s: I2C_RDWR Error %d\n",__func__,errno);
      return rc;
      //exit (1);
    }    
  }
  if (htu21df_crcdata(replymessage,&deviceValue)){
    printf("%s:CRC Failed\n",__func__);
    return -1;
  }

  switch (YourBiddingMaster)
  {
    case HTU21DF_READTEMP:
      *Value = ((deviceValue / 65536.0) * 175.72) - 46.85;  
      break;
    case HTU21DF_READHUM:
      *Value = ((deviceValue / 65536.0) * 125.0) - 6.0;    
      break;
    default:
      return -1;
      //exit (1);
      break;
  }
  //printf("Device temp 0x%x\n", deviceValue);
  return 0;
}

float htu21df_compensatedRH(float RH,float TempCell){
  return ( RH+((25-TempCell)*-0.15) ) ;
}

float htu21df_pptamb(float Temp){
  float TempValue=htu21df_ACo-(htu21df_BCo/(Temp+htu21df_CCo));
  double ppTamb =  pow (10,TempValue);
  return ppTamb;
}

float htu21df_DewPoint(float Temp, float Hum){
  double TempValue=htu21df_ACo-(htu21df_BCo/(Temp+htu21df_CCo));
  double ppTamb =  pow (10,TempValue);
  double DewP= -(( htu21df_BCo /  ((log10 (Hum*ppTamb/100))-htu21df_ACo))+htu21df_CCo);
  return DewP;
}

//END: htu21df specific code


int main(int argc, char **argv){
//  int rc;
  int fd = i2c_Open(I2CBus); //program will terminate within function if bus not present.
  float myTemp,myHum;

  int htu21rc = htu21df_init(fd);
  printf("\nDevice accessed via File descriptor:%0i\t at Address:0x%0x\t has returned:%0i\n",fd,HTU21DF_I2CADDR,htu21rc);   

  if (htu21rc == 0){
    int UserRegVal;
    htu21df_ReadUserReg(fd,&UserRegVal);
    printf("User Reg\tBefore\t:0x\%0x\n",UserRegVal);
    //htu21df_WriteUserReg(fd,0x03);  //try 0x03 my chip default is 0x02
    //sleepms(100);
    htu21df_ReadUserReg(fd,&UserRegVal);
    printf("User Reg\tAfter\t:0x\%0x\n\n",UserRegVal);
    
    printf ("Return:%i\tTemperature\t:%.2f C\n",htu21df_getValue(fd,&myTemp,HTU21DF_READTEMP),myTemp);
    printf ("Return:%i\tHumidity\t:%.2f%%\n",htu21df_getValue(fd,&myHum,HTU21DF_READHUM),myHum);
    float ComRH=htu21df_compensatedRH(myHum,myTemp);
    printf ("Compensated\tHumidity\t:%.2f%%\n",ComRH);
    printf ("Partial  \tPressure\t:%.2fpp\n",htu21df_pptamb(myTemp));
    printf ("Dew point\tTemperature\t:%.2f C\n\n",htu21df_DewPoint(myTemp, ComRH));
  }
  close (fd);   
  return 0;
}

