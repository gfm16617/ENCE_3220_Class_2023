#include <Arduino.h>
#include <avr/power.h>

#define PF0   A5
#define PF1   A4
#define PF4   A3
#define PF5   A2
#define PF6   A1
#define PF7   A0

#define DIGIT_1   13  // PC7
#define RED_LED   5   // PC6
#define DIGIT_4   10  // PB6
#define DATA      9   // PB5 -> DS
#define LATCH     8   // PB4 -> ST_CP
#define BUZZER    6   // PD7
#define DIGIT_2   12  // PD6
#define GREEN_LED 4   // PD4

#define PD5       NotMappedPort_PD5 // Not Mapped on Leonardo Board
#define COMMS_TX  1                 // PD3 -> TX
#define COMMS_RX  0                 // PD2 -> RX
#define BUTTON_1  2                 // PD1 -> Start/Stop Timer and Stop Buzzer 
#define BUTTON_2  3                 // PD0 -> Inc Timer
#define DIGIT_3   11                // PB7

#define PB3   NotMappedPort_PB3   // MISO -> Not Mapped on Leonardo Board
#define PB2   NotMappedPort_PB2   // MOSI -> Not Mapped on Leonardo Board
#define PB1   NotMappedPort_PB1   // SCK -> Not Mapped on Leonardo Board
#define PB0   NotMappedPort_PB0   // SS -> Not Mapped on Leonardo Board
#define CLOCK 7                   // PE6 -> SH_CP

// 7-Seg Display Variables
unsigned char gtable[]=
{0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f,0x77,0x7c
,0x39,0x5e,0x79,0x71,0x00};
byte gCurrentDigit;

// Volatile Variables
volatile unsigned char gISRFlag1   = 0;
volatile unsigned char gISRFlag2   = 0;
volatile unsigned char gBuzzerFlag = 0;

// Timer Variables
#define       DEFAULT_COUNT 30     // default value is 30secs
volatile int  gCount        = DEFAULT_COUNT;
unsigned char gTimerRunning = 0; 

unsigned int gReloadTimer1 = 62499; // corresponds to 1 second
byte         gReloadTimer3 = 200;   // display refresh time
unsigned int gReloadTimer4 = 100;   // corresponds to 0.4ms

#define BUFF_SIZE 20
char  gIncomingChar;
char  gCommsMsgBuff[BUFF_SIZE];
int   iBuff = 0;
byte  gPackageFlag = 0;
byte  gProcessDataFlag = 0;

unsigned char gDemoFlag = 0;

/**
 * @brief Setup peripherals and timers
 * @param
 * @return
 */
void setup() {

  if(F_CPU == 16000000) clock_prescale_set(clock_div_1);
  
  // LEDs Pins
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);

  // LEDs -> Timer Stopped
  digitalWrite(RED_LED, HIGH);
  digitalWrite(GREEN_LED, HIGH);

  // Button Pins
  pinMode(BUTTON_1, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_1), buttonISR1, RISING);
  pinMode(BUTTON_2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_2), buttonISR2, RISING);

  // Buzer Pins
  pinMode(BUZZER, OUTPUT);

  // Buzzer -> Off
  digitalWrite(BUZZER,LOW);

  // 7-Seg Display
  pinMode(DIGIT_1, OUTPUT);
  pinMode(DIGIT_2, OUTPUT);
  pinMode(DIGIT_3, OUTPUT);
  pinMode(DIGIT_4, OUTPUT);

  // Shift Register Pins
  pinMode(LATCH, OUTPUT);
  pinMode(CLOCK, OUTPUT);
  pinMode(DATA, OUTPUT);

  dispOff();  // turn off the display

  // UART Pins
  pinMode(COMMS_RX, INPUT);
  pinMode(COMMS_TX, OUTPUT);

  // timer4 should run faster than baudrate/8
  // 9600/8 = 1200 chars per second
  // 1/1200 = 0.833 ms
  Serial1.begin(9600);
  //Serial.begin(115200);  // Enable this for debug purposes

  // Initialize Timer1 (16bit) -> Used for clock
  // OCR1A = (F_CPU / (N * f_target)) - 1
  // Speed of Timer1 = (16e6 / (256 * 1)) - 1 = 62499
  noInterrupts();
  TCCR1A = 0;
  TCCR1B = 0;
  OCR1A = gReloadTimer1; // compare match register 16MHz/256
  TCCR1B |= (1<<WGM12);   // CTC mode
  // Start Timer by setting the prescaler -> done using the start button
  //TCCR1B |= (1<<CS12);    // 256 prescaler 
  TIMSK1 |= (1<<OCIE1A);  // enable timer compare interrupt
  interrupts();

  // Initialize Timer3 (16bit) -> Used to refresh display
  // Speed of Timer3 = 16MHz/64 = 250 KHz
  TCCR3A = 0;
  TCCR3B = 0;
  OCR3A = gReloadTimer3;            // max value 2^16 - 1 = 65535
  TCCR3A |= (1<<WGM31);
  TCCR3B = (1<<CS31) | (1<<CS30);   // 64 prescaler
  TIMSK3 |= (1<<OCIE3A);
  interrupts();                     // enable all interrupts

  // Initialize Timer4 (10bit) -> Used for Serial Comms
  // Speed of Timer4 = 16MHz/64 = 250 KHz
  TCCR4A = 0;
  TCCR4B = 0;
  OCR4A = gReloadTimer4;            // max value 2^10 - 1 = 1023
  TCCR4A |= (1<<WGM41);
  TCCR4B = (1<<CS41) | (1<<CS40);   // 64 prescaler
  TIMSK4 |= (1<<OCIE4A);
  interrupts();                     // enable all interrupts
}

/**
 * @brief Shifts the bits through the shift register
 * @param num
 * @param dp
 * @return
 */
void display(unsigned char num, unsigned char dp)
{
  digitalWrite(LATCH, LOW);
  shiftOut(DATA, CLOCK, MSBFIRST, gtable[num] | (dp<<7));
  digitalWrite(LATCH, HIGH);
}

/**
 * @brief Turns the 7-seg display off
 * @param
 * @return
 */
void dispOff()
{
   digitalWrite(DIGIT_1, HIGH);
   digitalWrite(DIGIT_2, HIGH);
   digitalWrite(DIGIT_3, HIGH);
   digitalWrite(DIGIT_4, HIGH);
}

/**
 * @brief Button 2 ISR
 * @param
 * @return
 */
void buttonISR2()
{
  // Increment Clock
  gCount++;
}

/**
 * @brief Button 1 ISR
 * @param
 * @return
 */
void buttonISR1()
{ 
  // Set ISR Flag
  gISRFlag1 = 1;
}

/**
 * @brief Timer 2 ISR
 * @param TIMER2_COMPA_vect
 * @return
 */
ISR(TIMER3_COMPA_vect)   // Timer2 interrupt service routine (ISR)
{
  dispOff();  // turn off the display
 
  switch (gCurrentDigit)
  {
    case 1: //0x:xx
      display( int((gCount/60) / 10) % 6, 0 );   // prepare to display digit 1 (most left)
      digitalWrite(DIGIT_1, LOW);  // turn on digit 1
      break;
 
    case 2: //x0:xx
      display( int(gCount / 60) % 10, 1 );   // prepare to display digit 2
      digitalWrite(DIGIT_2, LOW);     // turn on digit 2
      break;
 
    case 3: //xx:0x
      display( (gCount / 10) % 6, 0 );   // prepare to display digit 3
      digitalWrite(DIGIT_3, LOW);    // turn on digit 3
      break;
 
    case 4: //xx:x0
      display(gCount % 10, 0); // prepare to display digit 4 (most right)
      digitalWrite(DIGIT_4, LOW);  // turn on digit 4
      break;

    default:
      break;
  }
 
  gCurrentDigit = (gCurrentDigit % 4) + 1;
}

/**
 * @brief Timer 1 ISR
 * @param TIMER1_COMPA_vect
 * @return
 */
ISR(TIMER1_COMPA_vect)  // Timer1 interrupt service routine (ISR)
{
  gCount--;

  if(gCount == 0)
  {
      // Stop Timer
      stopTimer1();
      
      // Raise Alarm
      gBuzzerFlag = 1;
      gTimerRunning = 0;
  }
}

/**
 * @brief Timer 4 ISR
 * @param TIMER4_COMPA_vect
 * @return
 */
ISR(TIMER4_COMPA_vect)  // Timer4 interrupt service routine (ISR)
{
  if(Serial1.available()>0)
  {
    gISRFlag2 = 1;
  }
}

/**
 * @brief Stop Timer 1
 * @param
 * @return
 */
void stopTimer1()
{
  // Stop Timer
  TCCR1B &= 0b11111000; // stop clock
  TIMSK1 = 0; // cancel clock timer interrupt
}

/**
 * @brief Start Timer 1
 * @param
 * @return
 */
void startTimer1()
{
  // Start Timer
  TCCR1B |= (1<<CS12);    // 256 prescaler
  TIMSK1 |= (1<<OCIE1A);  // enable timer compare interrupt
}

/**
 * @brief Turn On Buzzer
 * @param
 * @return
 */
void activeBuzzer()
{
  unsigned char i;
  unsigned char sleepTime = 1; // ms
  
  for(i=0;i<10;i++)
  {
    digitalWrite(BUZZER,HIGH);
    delay(sleepTime);//wait for 1ms
    digitalWrite(BUZZER,LOW);
    delay(sleepTime);//wait for 1ms
  }
}

char compareArray(char a[], char b[], int size)
{
  int i;
  char result = 1;  // default: the arrays are equal
  
  for(i = 0; i<size; i++)
  {
    if(a[i]!=b[i])
    {
      result = 0;
      break;
    }
  }
  return result;
}

/**
 * @brief Main Loop
 * @param
 * @return
 */
void loop() 
{
  char  auxMsgBuff[BUFF_SIZE];
  int auxCount = 0;
  unsigned char auxDigit = '0';
  
  // Attend Button 2 ISR
  if(gISRFlag1 == 1)
  {
    // Reset ISR Flag
    gISRFlag1 = 0;

    if(gTimerRunning == 0)
    {
      // Start Timer
      gTimerRunning = 1;

      if(gCount == 0)
        gCount = DEFAULT_COUNT;

      if(gBuzzerFlag == 1)
      {
        gBuzzerFlag = 0;

        // LEDs -> Timer Stopped
        digitalWrite(RED_LED, HIGH);
        digitalWrite(GREEN_LED, HIGH);
      }
      else
      {
        startTimer1();
        // LEDs -> Timer Running
        digitalWrite(RED_LED, LOW);
        digitalWrite(GREEN_LED, HIGH);
      }
    }
    else
    {
      // Stop Timer
      stopTimer1();
      gTimerRunning = 0;

      // LEDs -> Timer Running
      digitalWrite(RED_LED, HIGH);
      digitalWrite(GREEN_LED, HIGH);
    }
  }

  // Attend gBuzzerFlag
  if(gBuzzerFlag == 1)
  {
    // Make Noise...
    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);
    activeBuzzer();
  }

  // Attend Timer4 flag - receive commands through serial
  if(gISRFlag2 == 1)
  {    
    // Reset ISR Flag
    gISRFlag2 = 0;

    // Read serial
    gIncomingChar = Serial1.read();

    // If normal character from package
    if(gPackageFlag == 1)
    {
      gCommsMsgBuff[iBuff] = gIncomingChar;
      iBuff++;

      // Safety mechanism in case "\n" is never sent
      if(iBuff == BUFF_SIZE)
      {
        gPackageFlag = 0;
        gProcessDataFlag = 1;
      }
    }

    // If start of the package
    if(gIncomingChar == '$')
    {    
      gPackageFlag = 1;  // Signal start of package
      
      // Clear Buffer
      for(int i=0; i<BUFF_SIZE; i++)
      {
        gCommsMsgBuff[i] = 0;
      }

      // set gCommsMsgBuff Index to zero
      iBuff = 0;
    }

    // If end of package
    if( (gIncomingChar == '\n') && (gPackageFlag == 1) )
    {
      // Signal end of package
      gPackageFlag = 0;
      gProcessDataFlag = 1;
    }
  }

  // Process serial commands
  if(gProcessDataFlag == 1)
  {
    gProcessDataFlag = 0;
    //Serial.print(gCommsMsgBuff);  // TMP

    // Get Command
    if(compareArray(gCommsMsgBuff, "SET", 3) == 1)
    {
      // Get Function
      auxMsgBuff[0] = gCommsMsgBuff[4];
      auxMsgBuff[1] = gCommsMsgBuff[5];
      auxMsgBuff[2] = gCommsMsgBuff[6];
      auxMsgBuff[3] = gCommsMsgBuff[7];

      if(compareArray(auxMsgBuff, "ALRM", 4) == 1)
      {
        // Get Value
        if(gCommsMsgBuff[9] == '0')
        {
          // turn off alarm function
          gBuzzerFlag = 0;
        }else
        {
          // turn on alarm function
          gBuzzerFlag = 1;
        }
      }
      
      if(compareArray(auxMsgBuff, "RLED", 4) == 1)
      {
        // Get Value
        if(gCommsMsgBuff[9] == '0')
        {
          // turn off red led function
          digitalWrite(RED_LED, LOW);
        }else
        {
          // turn on red led function
          digitalWrite(RED_LED, HIGH);
        }
      }
      
      if(compareArray(auxMsgBuff, "GLED", 4) == 1)
      {
        // Get Value
        if(gCommsMsgBuff[9] == '0')
        {
          // turn off green led function
          digitalWrite(GREEN_LED, LOW);
        }else
        {
          // turn on green led function
          digitalWrite(GREEN_LED, HIGH);
        }
      }
      
      if(compareArray(auxMsgBuff, "TMRS", 4) == 1)
      {
        auxCount = 0;
        // Get Time and conver to second
        auxCount += int(gCommsMsgBuff[9] - '0')*10*60;  // 0x:xx
        auxCount += int(gCommsMsgBuff[10] - '0')*60;    // x0:xx
        auxCount += int(gCommsMsgBuff[12] - '0')*10;    // xx:0x
        auxCount += int(gCommsMsgBuff[13] - '0');       // xx:x0
        // convert to seconds
        // affect gCount
        //Serial.println("Set Timer Value");
        gCount = auxCount;
      }
    }
    
    if(compareArray(gCommsMsgBuff, "STR", 3) == 1)
    {
      // Start timer function
      startTimer1();
    }
  
    if(compareArray(gCommsMsgBuff, "STP", 3) == 1)
    {
      // Stop timer function
      stopTimer1();
    }

    if(compareArray(gCommsMsgBuff, "INC", 3) == 1)
    {
      // Increment Timer
      gCount++;
    }

    if(compareArray(gCommsMsgBuff, "GET", 3) == 1)
    {
      // Send clock status
      Serial1.print("$");
      
      auxDigit = int((gCount/60) / 10) % 6;
      Serial1.print(String(auxDigit));
      
      auxDigit = int(gCount / 60) % 10;
      Serial1.print(String(auxDigit));

      Serial1.print(":");
      
      auxDigit = (gCount / 10) % 6;
      Serial1.print(String(auxDigit));
      
      auxDigit = gCount % 10;
      Serial1.print(String(auxDigit));
      Serial1.print("\n");
    }
    // ------
  }
}
