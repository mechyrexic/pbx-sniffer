#include <Arduino.h>
#include <SPI.h>
// TODO use relative timestamps

//#define LINE_CTRL_PIN D2
//#define OFFHOOK_CTRL_PIN D3
#define NUM_PHONES 24
#define NUM_TRANSFER_BTNS 16
#define NUM_ADCS 3
#define SAMPLE_BUFFER_SIZE 32
#define PHONE_DIGITS 10
#define SPI_F 2340

const uint8_t dial_test_pin = PA1;

size_t curtime = 0;

// binary, eg phone 3 is indexed from PC9 & PC8
const uint8_t line_ctrls[] =
{
  PC9,
  PC8,
  PB8,
  PC6,
  PB9
};

// regular, each index is each phone
const uint8_t offhook_ctrls[] = 
{
  PB6,
  PB11,
  PC7,
  PA9,
  PB2,
  PA8,
  PB1,
  PB10,
  PB15,
  PB4,
  PB14,
  PB5,
  PB13,
  PB3,
  PA10,
  PC4,
  PA2,
  PA3,
  PC0,
  PC3,
  PC1,
  PC2,
  PB0,
  PA4
};

//bool transfer_btn_pressed[NUM_TRANSFER_BTNS] = {0};

SPISettings adc_settings = SPISettings(SPI_F, MSBFIRST, SPI_MODE0);

const uint8_t adc_cs[] =
{
  PA12,
  PA11,
  PB12
};

uint16_t adc_samples[NUM_PHONES][SAMPLE_BUFFER_SIZE] = {0};

const uint8_t transfer_shiftout_pin = PC10;
const uint8_t transfer_shiftld_pin = PC11;
const uint8_t transfer_shiftclk_pin = PC12;
const uint8_t transfer_shiftinh_pin = PD2;

size_t dial_offonhook_ms = 100;
size_t dial_onhook_ms = 100;
size_t dial_number_wait = 400;
size_t dial_hookswitch_ms = 400;
size_t dial_transfer_wait = 2000;
size_t dial_delays[4] = {dial_offonhook_ms, dial_hookswitch_ms, dial_transfer_wait,0};

const uint8_t baird_number[PHONE_DIGITS] = {1, 10, 8, 0, 0, 0, 0, 0, 0, 0};
const uint8_t jkaudio_number[PHONE_DIGITS] = {1, 10, 9, 0, 0, 0, 0, 0, 0, 0};
const uint8_t baird_phone = 8;
const uint8_t jkaudio_phone = 9;

struct phone
{
  size_t next_update_ms = 0;
  uint8_t cur_dial_num = 0;
  uint8_t cur_dial_pos = 0;
  uint8_t cur_dial_context = 0;
  uint8_t dial_context_queue[PHONE_DIGITS+4] = {0};
  uint8_t dial_queue[PHONE_DIGITS+4] = {0};
  bool offhook = false;
  bool dialing = false;
};

phone phones[NUM_PHONES] = {0};

void line_ctrl_mux_select(uint8_t phone_index)
{
  for (size_t i = 0; i < sizeof(line_ctrls)/sizeof(line_ctrls[0]); i++)
  {
    bool bit = (phone_index >> i) & 1;
    if (bit)
    {
      digitalWrite(line_ctrls[i], HIGH);
    }
    else
    {
      digitalWrite(line_ctrls[i], LOW);
    }
  }
}

void queue_dial(const uint8_t number[PHONE_DIGITS], uint8_t phone_index)
{

  if (phone_index > NUM_PHONES) return; // bounds check

  phone& phone = phones[phone_index];

  if(phone.dialing) return;

  for (size_t i = 1; i < sizeof(number[0])*PHONE_DIGITS+1; i++)
  {
    phone.dial_context_queue[i] = 0;
    phone.dial_queue[i] = number[i-1];
    Serial.println(number[i-1]);
  }


  phone.dial_context_queue[0] = 1;
  phone.dial_queue[0] = 1;  
  phone.dial_context_queue[PHONE_DIGITS+1] = 2;
  phone.dial_queue[PHONE_DIGITS+1] = 1;
  phone.dial_context_queue[PHONE_DIGITS+2] = 1;
  phone.dial_queue[PHONE_DIGITS+2] = 1;
  phone.dial_context_queue[PHONE_DIGITS+3] = 0;
  phone.dial_queue[PHONE_DIGITS+3] = 3;
  Serial.println(phone.dial_queue[PHONE_DIGITS+4]);
  for (size_t i = 0; i < PHONE_DIGITS+4; i++){
    Serial.print(i);
    Serial.print("C:");
    Serial.print(phone.dial_context_queue[i]);
    Serial.print(" N:");
    Serial.println(phone.dial_queue[i]);
  }
  phone.cur_dial_num = phone.dial_queue[0];
  phone.cur_dial_pos = 0;
  phone.cur_dial_context = phone.dial_context_queue[0];
  phone.dialing = true;
  phone.offhook = true;
  
  phone.next_update_ms = curtime + dial_number_wait;
  // Serial.print("Dial phone ");
  // Serial.println(phone_index);
  digitalWrite(offhook_ctrls[phone_index], LOW);
}

void process_dialing()
{
  for (size_t phone_index = 0; phone_index < NUM_PHONES; phone_index++)
  {
    phone &curphone = phones[phone_index];
    if (!curphone.dialing || curphone.next_update_ms > curtime)
    {
      //digitalWrite(CTRL_LINE_PIN, LOW);
      continue;
    }

    line_ctrl_mux_select(phone_index);
    Serial.println();

    curphone.offhook = !curphone.offhook;
    if (curphone.cur_dial_context == 2){
      curphone.cur_dial_num--;
      curphone.next_update_ms = curtime + dial_delays[curphone.cur_dial_context];
    } else {
      if (!curphone.offhook)
      {
        digitalWrite(offhook_ctrls[phone_index], HIGH);
        digitalWrite(LED_BUILTIN, HIGH);
        curphone.next_update_ms = curtime + dial_delays[curphone.cur_dial_context];
        Serial.print(curtime);
        Serial.print(", ");
        Serial.print(curphone.next_update_ms);
        Serial.print(" Going off hook, position ");
        Serial.print(curphone.cur_dial_pos);
        Serial.print(" delay ");
        Serial.println(dial_delays[curphone.cur_dial_context]);
      }
      else
      {
        digitalWrite(offhook_ctrls[phone_index], LOW);
        digitalWrite(LED_BUILTIN, LOW);
        curphone.cur_dial_num--;
        curphone.next_update_ms = curtime + dial_delays[curphone.cur_dial_context];
        Serial.print(curtime);
        Serial.print(", ");
        Serial.print(curphone.next_update_ms);
        Serial.print(" Going on hook, position ");
        Serial.print(curphone.cur_dial_pos);
        Serial.print(" context ");
        Serial.print(curphone.cur_dial_context);
        Serial.print(" number ");
        Serial.print(curphone.cur_dial_num);
        Serial.print(" delay ");
        Serial.println(dial_delays[curphone.cur_dial_context]);
      }
    }
    if (!curphone.cur_dial_num)
    {
      while (!curphone.cur_dial_num){
      curphone.cur_dial_pos++;
      curphone.cur_dial_context = curphone.dial_context_queue[curphone.cur_dial_pos];
      curphone.cur_dial_num = curphone.dial_queue[curphone.cur_dial_pos];
      curphone.next_update_ms = curtime + dial_number_wait;

      Serial.print(" Going to next number, position ");
      Serial.print(curphone.cur_dial_pos);
      Serial.print(" context ");
      Serial.print(curphone.cur_dial_context);
      Serial.print(" number ");
      Serial.print(curphone.cur_dial_num);
      Serial.print(" delay ");
      Serial.println(dial_delays[curphone.cur_dial_context]);
      }

    }

    if (curphone.cur_dial_pos >= PHONE_DIGITS+4 || !curphone.cur_dial_num)
    {
      curphone.dialing = false;
      digitalWrite(offhook_ctrls[phone_index], LOW);
      digitalWrite(LED_BUILTIN, LOW);
      line_ctrl_mux_select(0);
      curphone.offhook = false;

      //hacky solution to sequence call to baird
      // future solution should read the respective adc value to detect when the JKaudio/control station
      // line goes active.
      if ((phone_index != jkaudio_phone) && (phone_index != baird_phone)){
        queue_dial(baird_number, jkaudio_phone);
      }
    }

  }
  
}

void select_adc(uint8_t id)
{
  for (size_t i = 0; i < NUM_ADCS; i++)
  {
    if (i == id)
    {

      // digitalWrite(adc_cs[i], HIGH);
      // uint16_t wait_time = 100000/SPI_F;
      // uint32_t start_time = micros();
      // while (micros()-start_time < wait_time);
      digitalWrite(adc_cs[i], LOW);
    }
    else
    {
      digitalWrite(adc_cs[i], HIGH);
    }
  }
}

void process_adcs()
{
  SPI.beginTransaction(adc_settings);
  for (size_t sample_idx = 0; sample_idx < SAMPLE_BUFFER_SIZE; sample_idx++)
  {
    for (size_t phone_idx = 0; phone_idx < 8; phone_idx++)
    {
      for (size_t adc_idx = 0; adc_idx < NUM_ADCS; adc_idx++)
      {
        select_adc(adc_idx);
        // start adc transfer
        SPI.transfer(0x1);

        uint8_t transmit_data = (1 << 7) | (phone_idx << 4);
        uint8_t recv_2msb = SPI.transfer(transmit_data);
        uint8_t recv_8lsb = SPI.transfer(0x0);
        SPI.transfer(0x0);



        uint16_t raw_sample = ((recv_2msb & 0x3) << 8) | recv_8lsb;


        // uint16_t transmit_data = (1 << 7) | (1<<6) | (phone_idx << 0);
        // uint16_t raw_sample = SPI.transfer16(transmit_data);
        // SPI.transfer(0x0);


        adc_samples[8*adc_idx + phone_idx][sample_idx] = raw_sample;
      }
    }
  }
  SPI.endTransaction();
}

void process_transfer_btns()
{
  // lower test pin for scope trigger
  digitalWrite(dial_test_pin, LOW);

  // load button states into register
  digitalWrite(transfer_shiftclk_pin, HIGH);
  digitalWrite(transfer_shiftinh_pin, HIGH);
  digitalWrite(transfer_shiftclk_pin, LOW);
  digitalWrite(transfer_shiftclk_pin, HIGH);
  digitalWrite(transfer_shiftld_pin, LOW);

  digitalWrite(transfer_shiftclk_pin, LOW);
  digitalWrite(transfer_shiftclk_pin, HIGH);

  // shift out states
  digitalWrite(transfer_shiftld_pin, HIGH);
  digitalWrite(transfer_shiftclk_pin, HIGH);
  digitalWrite(transfer_shiftclk_pin, LOW);

  //end inhibit
  digitalWrite(transfer_shiftclk_pin, HIGH);
  digitalWrite(transfer_shiftinh_pin, LOW);

  //shift out data
  for (size_t i = 0; i < NUM_TRANSFER_BTNS; i++)
  {
    digitalWrite(transfer_shiftclk_pin, HIGH);
    digitalWrite(transfer_shiftclk_pin, LOW);
    //read in the middle of the clock cycle

    if (!(digitalRead(transfer_shiftout_pin)^(!(int)((float)i/NUM_TRANSFER_BTNS+0.5))))
    {
      queue_dial(jkaudio_number, i);
    }
  }

  // raise test pin for scope end
  digitalWrite(dial_test_pin, HIGH);
}

void process_serial_transfer()
{
  Serial.print('[');
  for (size_t i = 0; i < NUM_PHONES; i++)
  {
    Serial.print((phones[i].offhook));
  }
  Serial.write(char('|'));

  // Serial.write((uint8_t*)adc_samples, sizeof(adc_samples));
  
  // for(size_t i = 0; i < NUM_PHONES; i++ ){
  //   for(size_t k = 0; k <SAMPLE_BUFFER_SIZE; k++){
  //     Serial.print(adc_samples[i][k]);
  //     Serial.print(",");
  //   }
  //   Serial.print("|");
  // }
  // for (size_t i = 0; i < NUM_PHONES; i++) 
  // {
  //   Serial.write(((uin8_t*)adc_samples, sizeof(adc_samples)));
  //   Serial.write(char('|'));
  // }
  // Serial.print(((uint8_t*)adc_samples[3],sizeof(adc_samples[3])));
  Serial.print("]\n");
}

void setup() 
{
  pinMode(LED_BUILTIN, OUTPUT);

  for (size_t i = 0; i < sizeof(line_ctrls)/sizeof(line_ctrls[0]); i++)
  {
    pinMode(line_ctrls[i], OUTPUT);
  }

  for (size_t i = 0; i < sizeof(offhook_ctrls)/sizeof(offhook_ctrls[0]); i++)
  {
    pinMode(offhook_ctrls[i], OUTPUT);
    digitalWrite(offhook_ctrls[i], HIGH);
  }


  for (size_t i = 0; i < sizeof(adc_cs)/sizeof(adc_cs[0]); i++)
  {
    pinMode(adc_cs[i], OUTPUT);
  }

  pinMode(transfer_shiftout_pin, INPUT);
  pinMode(transfer_shiftld_pin, OUTPUT);
  pinMode(transfer_shiftclk_pin, OUTPUT);
  pinMode(transfer_shiftinh_pin, OUTPUT);
  pinMode(dial_test_pin, OUTPUT);
  digitalWrite(dial_test_pin, HIGH);

  Serial.begin(115200);
  SPI.begin();

  //uint8_t test_queue[PHONE_DIGITS] = {1, 10, 1, 0, 0, 0, 0, 0, 0, 0};
  //queue_dial(test_queue, phones[0]);
}

void loop() 
{
  curtime = millis();
  // process_adcs();
  process_transfer_btns();
  process_dialing();
  // process_serial_transfer();
}
