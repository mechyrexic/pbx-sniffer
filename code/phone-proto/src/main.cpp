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

#define MAX_DIAL_LEN 4 // Enough for "101", "111", etc.
#define BAIRD_INDEX 11

// Extension Numbers (PBX Logic: '0' is 10 pulses)
const uint8_t NUMBER_BAIRD[] = {1, 10, 1}; // "101"
const uint8_t NUMBER_JK[]   = {1, 10, 9};  // "109"

// Panasonic KX-TA824 Timings
#define T_PULSE_BREAK 60   // On-hook (Relay OFF / Pin HIGH)
#define T_PULSE_MAKE 40    // Off-hook (Relay ON / Pin LOW)
#define T_DIGIT_GAP 800    
#define T_HOOK_Hookswitch 600   
#define T_STABILIZE 500    

const uint8_t line_ctrls[] = { PC9, PC8, PB8, PC6, PB9 };
const uint8_t offhook_ctrls[] = {
  PB6, PB11, PC7, PA9, PB2, PA8, PB1, PB10, PB15, PB4, PB14, PB5,
  PB13, PB3, PA10, PC4, PA2, PA3, PC0, PC3, PC1, PC2, PB0, PA4
};

// Shift Register Pins
const uint8_t transfer_shiftld_pin = PC11;
const uint8_t transfer_shiftout_pin = PC10;
const uint8_t transfer_shiftclk_pin = PC12;
const uint8_t transfer_shiftinh_pin = PD2;

enum SeqState {
  IDLE,
  LOCAL_PICKUP,     // 1. Offhook Local
  LOCAL_DIAL_HOST,  // 2. Dial Baird
  WAIT_FOR_HOST,    // 3. Wait 5s
  LOCAL_HOOKSWITCH,      // 4. Hookswitch Local
  LOCAL_HANGUP,     // 5 & 6. Hangup & Release Local
  HOST_PICKUP,      // 7 & 8. Control Host & Offhook
  HOST_DIAL_JK,     // 9. Dial JK
  WAIT_FOR_JK,      // 10. Wait 3s
  HOST_Hookswitch,       // 11. Final Hookswitch
  CLEANUP           // 12. Release everything
};

struct PhoneTask {
  SeqState state = IDLE;
  uint32_t next_ms = 0;
  uint8_t dial_buf[MAX_DIAL_LEN];
  uint8_t dial_pos = 0;
  uint8_t pulses_left = 0;
  bool is_break = false;
  uint8_t active_index = 0;
};

PhoneTask task;
uint32_t curtime = 0;

void line_ctrl_mux_select(uint8_t index) {
  for (int i = 0; i < sizeof(line_ctrls)/sizeof(line_ctrls[0]); i++) {
    digitalWrite(line_ctrls[i], (index >> i) & 0x01);
  }
}

void set_hook(uint8_t index, bool offhook) {
  // Active-Low: LOW = Energized = Off-hook
  digitalWrite(offhook_ctrls[index], offhook ? LOW : HIGH);
}

bool process_dialing(uint8_t index) {
  if (task.pulses_left == 0) {
    task.dial_pos++;
    if (task.dial_pos >= 3) return true; // Done
    task.pulses_left = task.dial_buf[task.dial_pos];
    task.next_ms = curtime + T_DIGIT_GAP;
    return false;
  }

  if (task.is_break) {
    set_hook(index, true); // Close (Off-hook)
    task.is_break = false;
    task.pulses_left--;
    task.next_ms = curtime + T_PULSE_MAKE;
  } else {
    set_hook(index, false); // Open (On-hook)
    task.is_break = true;
    task.next_ms = curtime + T_PULSE_BREAK;
  }
  return false;
}

void run_sequence() {
  if (task.state == IDLE || curtime < task.next_ms) return;

  switch (task.state) {
    case LOCAL_PICKUP:
      line_ctrl_mux_select(task.active_index);
      set_hook(task.active_index, true);
      memcpy(task.dial_buf, NUMBER_BAIRD, 3);
      task.dial_pos = 0;
      task.pulses_left = task.dial_buf[0];
      task.state = LOCAL_DIAL_HOST;
      task.next_ms = curtime + T_STABILIZE;
      break;

    case LOCAL_DIAL_HOST:
      if (process_dialing(task.active_index)) {
        task.state = WAIT_FOR_HOST;
        task.next_ms = curtime + 5000;
      }
      break;

    case WAIT_FOR_HOST:
      set_hook(task.active_index, false); // Start Hookswitch
      task.state = LOCAL_HOOKSWITCH;
      task.next_ms = curtime + T_HOOK_Hookswitch;
      break;

    case LOCAL_HOOKSWITCH:
      set_hook(task.active_index, true); // End Hookswitch
      task.state = LOCAL_HANGUP;
      task.next_ms = curtime + T_STABILIZE;
      break;

    case LOCAL_HANGUP:
      set_hook(task.active_index, false); // Hangup
      line_ctrl_mux_select(0); // Release Mux
      task.state = HOST_PICKUP;
      task.next_ms = curtime + 500;
      break;

    case HOST_PICKUP:
      task.active_index = BAIRD_INDEX;
      line_ctrl_mux_select(task.active_index);
      set_hook(task.active_index, true);
      memcpy(task.dial_buf, NUMBER_JK, 3);
      task.dial_pos = 0;
      task.pulses_left = task.dial_buf[0];
      task.state = HOST_DIAL_JK;
      task.next_ms = curtime + T_STABILIZE;
      break;

    case HOST_DIAL_JK:
      if (process_dialing(task.active_index)) {
        task.state = WAIT_FOR_JK;
        task.next_ms = curtime + 3000;
      }
      break;

    case WAIT_FOR_JK:
      set_hook(task.active_index, false); // Start Hookswitch
      task.state = HOST_Hookswitch;
      task.next_ms = curtime + T_HOOK_Hookswitch;
      break;

    case HOST_Hookswitch:
      set_hook(task.active_index, true); // End Hookswitch
      task.state = CLEANUP;
      task.next_ms = curtime + T_STABILIZE;
      break;

    case CLEANUP:
      line_ctrl_mux_select(0); // Release Line Control
      task.state = IDLE;
      break;
  }
}

void check_buttons() {
  if (task.state != IDLE) return;

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
      task.active_index = i;
      task.state = LOCAL_PICKUP;
      break;
    }
  }
}

void setup() {
  for (auto p : line_ctrls) pinMode(p, OUTPUT);
  for (auto p : offhook_ctrls) {
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH); // On-hook by default
  }
  
  pinMode(transfer_shiftout_pin, INPUT);
  pinMode(transfer_shiftld_pin, OUTPUT);
  pinMode(transfer_shiftclk_pin, OUTPUT);
  pinMode(transfer_shiftinh_pin, OUTPUT);
  
  digitalWrite(transfer_shiftinh_pin, HIGH);
  digitalWrite(transfer_shiftld_pin, HIGH);
}

void loop() {
  curtime = millis();
  check_buttons();
  run_sequence();
}