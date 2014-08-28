//All the following is from the htu21df manual
#define HTU21DF_I2CADDR    0x40
#define HTU21DF_READTEMP   0xF3 //No Hold
#define HTU21DF_READHUM    0xF5 //No Hold
#define HTU21DF_WRITEREG   0xE6
#define HTU21DF_READREG    0xE7
#define HTU21DF_RESET      0xFE

static const int HTU21DF_ResetDelay = 15;
static const int HTU21DF_MinDelay   = 11; //Should be 16-RetryDelay (16 or so for hum 50 for temp)
static const int HTU21DF_RetryDelay = 5; 

static const float htu21df_ACo=8.1332;
static const float htu21df_BCo=1762.39;
static const float htu21df_CCo=235.66;

int         i2c_Open             (char *I2CBusName);
void        i2c_commonsenderror  (int fd, uint8_t DeviceAddress);
int         htu21df_crcdata      (const uint8_t *Data, uint16_t *ConvertedValue);
int         htu21df_ReadUserReg  (int fd, int *retValue);
int         htu21df_WriteUserReg (int fd,int NewValue);
int         htu21df_init         (int fd);
int         htu21df_getValue     (int fd, float *Value, const uint8_t YourBiddingMaster );
float       htu21df_compensatedRH(float RH,float TempCell);
float       htu21df_pptamb       (float Temp);
float       htu21df_DewPoint     (float Temp, float Hum);