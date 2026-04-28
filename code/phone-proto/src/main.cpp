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
const uint8_t NUMBER_BAIRD[] = {1, 10, 9}; // "101"
const uint8_t NUMBER_JK[]   = {1, 1, 10};  // "110"

// Panasonic KX-TA824 Timings
#define T_PULSE_BREAK 40   // On-hook (Relay OFF / Pin HIGH)
#define T_PULSE_MAKE 60    // Off-hook (Relay ON / Pin LOW)
#define T_DIGIT_GAP 400    
#define T_HOOK_Hookswitch 400   
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
  LOCAL_HOOKSWITCH1,
  LOCAL_PICKUP,     // 1. Offhook Local
  LOCAL_DIAL_HOST,  // 2. Dial Baird
  WAIT_FOR_HOST,    // 3. Wait 5s
  LOCAL_HOOKSWITCH2,      // 4. Hookswitch Local
  LOCAL_DIAL_3,     // 4.5 dial 3
  LOCAL_HOOKWAIT,   // 5. start hangup
  LOCAL_HANGUP,     // 6. Hangup & Release Local
  HOST_HOOKSWITCH,   // 7. Control Host
  HOST_PICKUP,      // 8. Control Host & Offhook
  HOST_DIAL_JK,     // 9. Dial JK
  WAIT_FOR_JK,      // 10. Wait 3s
  HOST_Hookswitch2,       // 11. Final Hookswitch
  HOST_DIAL_3,      // 11.5 dial 3
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
  Serial.print("mux select:");
  Serial.println(index);
  for (int i = 0; i < sizeof(line_ctrls)/sizeof(line_ctrls[0]); i++) {
    digitalWrite(line_ctrls[i], (index >> i) & 0x01);
  }
}

void set_hook(uint8_t index, bool offhook) {
  // Active-Low: LOW = Energized = Off-hook
  Serial.print("hook state:");
  Serial.println(offhook);
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
    case LOCAL_HOOKSWITCH1:
      Serial.println("LOCAL_HOOKSWITCH1");
      set_hook(task.active_index, true); // End Hookswitch
      task.state = LOCAL_PICKUP;
      task.next_ms = curtime + T_STABILIZE;
      break;

    case LOCAL_PICKUP:
      Serial.println("LOCAL_PICKUP");
      for (uint8_t i = 0; i < sizeof(NUMBER_BAIRD)/sizeof(NUMBER_BAIRD[0]); i++) {
        task.dial_buf[i] = NUMBER_BAIRD[i];
      }
      task.dial_pos = 0;
      task.pulses_left = task.dial_buf[0];
      task.state = LOCAL_DIAL_HOST;
      task.next_ms = curtime + T_STABILIZE;
      break;

    case LOCAL_DIAL_HOST:
      Serial.println("LOCAL_DIAL_HOST");  
      if (process_dialing(task.active_index)) {
        task.state = WAIT_FOR_HOST;
        task.next_ms = curtime + 5000;
      }
      break;

    case WAIT_FOR_HOST:
      Serial.println("WAIT_FOR_HOST");
      set_hook(task.active_index, false); // Start Hookswitch
      task.state = LOCAL_HOOKSWITCH2;
      task.next_ms = curtime + T_HOOK_Hookswitch;
      break;

    case LOCAL_HOOKSWITCH2:
      Serial.println("LOCAL_HOOKSWITCH2");
      set_hook(task.active_index, true); // End Hookswitch
      task.state = LOCAL_DIAL_3;
      task.next_ms = curtime + T_STABILIZE;
      task.dial_buf[2] = 3;
      
      task.dial_pos = 2;
      task.pulses_left = task.dial_buf[2];
      break;

    case LOCAL_DIAL_3:
      Serial.println("LOCAL_DIAL_3");
      if (process_dialing(task.active_index)) {
        task.state = LOCAL_HOOKWAIT;
        task.next_ms = curtime + T_STABILIZE;
      }
      break;
      
    
    case LOCAL_HOOKWAIT:
      Serial.println("LOCAL_HOOKWAIT");
      set_hook(task.active_index, false); // Hangup
      task.state = LOCAL_HANGUP;
      task.next_ms = curtime + 2000;
      break;

    case LOCAL_HANGUP:
      Serial.println("LOCAL_HANGUP");
      line_ctrl_mux_select(0); // Release Mux
      task.state = HOST_PICKUP;
      task.next_ms = curtime + T_STABILIZE;
      break;

    case HOST_PICKUP:
      Serial.println("HOST_PICKUP");
      // line_ctrl_mux_select(task.active_index);
      // set_hook(task.active_index, true);
      task.state = HOST_HOOKSWITCH;

      task.active_index = BAIRD_INDEX;
      line_ctrl_mux_select(task.active_index);
      task.next_ms = curtime + T_HOOK_Hookswitch;
      set_hook(task.active_index, false);
      break;

    case HOST_HOOKSWITCH:
      Serial.println("HOST_HOOKSWITCH");
      set_hook(task.active_index, true);
      for (uint8_t i = 0; i < sizeof(NUMBER_JK)/sizeof(NUMBER_JK[0]); i++) {
        task.dial_buf[i] = NUMBER_JK[i];
      }
      task.dial_pos = 0;
      task.pulses_left = task.dial_buf[0];
      task.state = HOST_DIAL_JK;
      task.next_ms = curtime + T_STABILIZE;
      break;


    case HOST_DIAL_JK:
      Serial.println("HOST_DIAL_JK");
      if (process_dialing(task.active_index)) {
        task.state = WAIT_FOR_JK;
        task.next_ms = curtime + 3000;
      }
      break;

    case WAIT_FOR_JK:
      Serial.println("WAIT_FOR_JK");
      set_hook(task.active_index, false); // Start Hookswitch
      task.state = HOST_Hookswitch2;
      task.next_ms = curtime + T_HOOK_Hookswitch;
      break;

    case HOST_Hookswitch2:
      Serial.println("HOST_Hookswitch");
      set_hook(task.active_index, true); // End Hookswitch
      task.state = HOST_DIAL_3;
      task.next_ms = curtime + T_STABILIZE;
      task.dial_buf[2] = 3;
      
      task.dial_pos = 2;
      task.pulses_left = task.dial_buf[2];
      break;

    case HOST_DIAL_3:
      Serial.println("HOST_DIAL_3");
      if (process_dialing(task.active_index)) {
        task.state = CLEANUP;
        task.next_ms = curtime + T_STABILIZE;
      }
      break;

    case CLEANUP:
      Serial.println("CLEANUP");
      line_ctrl_mux_select(0); // Release Line Control
      set_hook(task.active_index, false); // End Hookswitch
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
      task.state = LOCAL_HOOKSWITCH1;
      line_ctrl_mux_select(task.active_index);
      task.next_ms = curtime + T_HOOK_Hookswitch;
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

  Serial.begin(115200);
}

void loop() {
  curtime = millis();
  check_buttons();
  run_sequence();
}