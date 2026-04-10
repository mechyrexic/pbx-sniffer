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

bool transfer_btn_pressed[NUM_TRANSFER_BTNS] = {0};

SPISettings adc_settings;

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

size_t dial_offhook_ms = 100;
size_t dial_onhook_ms = 100;
size_t dial_number_wait = 400;

struct phone
{
  size_t next_update_ms = 0;
  uint8_t cur_dial_num = 0;
  uint8_t cur_dial_pos = 0;
  uint8_t dial_queue[PHONE_DIGITS] = {0};
  bool offhook = false;
  bool dialing = false;
};

phone phones[NUM_PHONES] = {0};

void line_ctrl_mux_write(uint32_t value, uint8_t phone_index)
{
  for (size_t i = 0; i < sizeof(line_ctrls)/sizeof(line_ctrls[0]); i++)
  {
    bool bit = (phone_index >> i) & 1;
    if (bit)
    {
      digitalWrite(line_ctrls[i], value);
    }
  }
}

void queue_dial(uint8_t number[PHONE_DIGITS], uint8_t phone_index)
{

  if (phone_index > NUM_PHONES) return; // bounds check

  phone& phone = phones[phone_index];

  for (size_t i = 0; i < sizeof(number[0])*PHONE_DIGITS; i++)
  {
    phone.dial_queue[i] = number[i];
  }
  phone.cur_dial_num = number[0];
  phone.cur_dial_pos = 0;
  phone.dialing = true;
  phone.offhook = true;

  size_t curtime = millis();
  phone.next_update_ms = curtime + dial_number_wait;

  digitalWrite(offhook_ctrls[phone_index], LOW);
}

void process_dialing()
{
  for (size_t phone_index = 0; phone_index < NUM_PHONES; phone_index++)
  {
    size_t curtime = millis();
    phone &curphone = phones[phone_index];
    if (!curphone.dialing || curphone.next_update_ms > curtime)
    {
      //digitalWrite(CTRL_LINE_PIN, LOW);
      return;
    }

    line_ctrl_mux_write(phone_index, HIGH);

    curphone.offhook = !curphone.offhook;

    if (!curphone.offhook)
    {
      digitalWrite(offhook_ctrls[phone_index], HIGH);
      digitalWrite(LED_BUILTIN, HIGH);
      curphone.next_update_ms = curtime + dial_onhook_ms;
    }
    else
    {
      digitalWrite(offhook_ctrls[phone_index], LOW);
      digitalWrite(LED_BUILTIN, LOW);
      curphone.cur_dial_num--;
      curphone.next_update_ms = curtime + dial_offhook_ms;
    }

    if (!curphone.cur_dial_num)
    {
      curphone.cur_dial_pos++;
      curphone.cur_dial_num = curphone.dial_queue[curphone.cur_dial_pos];
      curphone.next_update_ms = curtime + dial_number_wait;
    }

    if (curphone.cur_dial_pos >= PHONE_DIGITS || !curphone.cur_dial_num)
    {
      curphone.dialing = false;
      digitalWrite(offhook_ctrls[phone_index], LOW);
      digitalWrite(LED_BUILTIN, LOW);
      line_ctrl_mux_write(phone_index, LOW);
    }

  }
  
}

void select_adc(uint8_t id)
{
  for (size_t i = 0; i < NUM_ADCS; i++)
  {
    if (i == id)
    {
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
  for (size_t sample_idx = 0; sample_idx < SAMPLE_BUFFER_SIZE; sample_idx++)
  {
    for (size_t phone_idx = 0; phone_idx < NUM_PHONES/NUM_ADCS; phone_idx++)
    {
      SPI.beginTransaction(adc_settings);
      for (size_t adc_idx = 0; adc_idx < NUM_ADCS; adc_idx++)
      {
        select_adc(adc_idx);
        // start adc transfer
        SPI.transfer(0x1);

        uint8_t transmit_data = (1 << 7) | (phone_idx << 4);
        uint8_t recv_2msb = SPI.transfer(transmit_data);
        uint8_t recv_8lsb = SPI.transfer(0x0);

        uint16_t raw_sample = ((recv_2msb & 0x3) << 8) | recv_8lsb;

        adc_samples[phone_idx*adc_idx][sample_idx] = raw_sample;

      }
      SPI.endTransaction();
    }
  }
}

void process_transfer_btns()
{
  // load button states into register
  digitalWrite(transfer_shiftinh_pin, HIGH);
  digitalWrite(transfer_shiftld_pin, LOW);

  digitalWrite(transfer_shiftclk_pin, HIGH);

  // shift out states
  digitalWrite(transfer_shiftinh_pin, LOW);
  digitalWrite(transfer_shiftld_pin, HIGH);

  digitalWrite(transfer_shiftclk_pin, LOW);

  for (size_t i = 0; i < NUM_TRANSFER_BTNS; i++)
  {
    digitalWrite(transfer_shiftclk_pin, HIGH);
    transfer_btn_pressed[i] = (bool)digitalRead(transfer_shiftout_pin);
    digitalWrite(transfer_shiftclk_pin, LOW);
  }
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
  }

  pinMode(transfer_shiftout_pin, INPUT);
  pinMode(transfer_shiftld_pin, OUTPUT);
  pinMode(transfer_shiftclk_pin, OUTPUT);
  pinMode(transfer_shiftinh_pin, OUTPUT);

  adc_settings = SPISettings(2340000, MSBFIRST, SPI_MODE0);

  SPI.begin();

  //uint8_t test_queue[PHONE_DIGITS] = {1, 10, 1, 0, 0, 0, 0, 0, 0, 0};
  //queue_dial(test_queue, phones[0]);
}

void loop() 
{
  process_adcs();
  process_transfer_btns();
  process_dialing();
}
