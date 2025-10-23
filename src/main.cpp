// ESP32-C3 TWAI Receiver / Echo Node (NORMAL)
// Transceiver: SN65HVD230 (6-pin). Pins: TX=GPIO4->CTX(DIN), RX=GPIO5<-CRX(RO)
// Role: ACK every valid frame and send a concise echo reply for visibility.

#include <Arduino.h>
#include "driver/twai.h"

// ===== User config =====
static int TWAI_TX_GPIO = 4;   // ESP32-C3 -> SN65 CTX (DIN)
static int TWAI_RX_GPIO = 5;   // SN65 CRX (RO) -> ESP32-C3

// Match the master's bitrate
static twai_timing_config_t tcfg = TWAI_TIMING_CONFIG_250KBITS(); // or _500KBITS()

// Accept all frames
static twai_filter_config_t fcfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

// Alerts to observe
static const uint32_t ALERTS =
  TWAI_ALERT_RX_DATA |
  TWAI_ALERT_TX_SUCCESS |
  TWAI_ALERT_TX_FAILED |
  TWAI_ALERT_BUS_OFF |
  TWAI_ALERT_BUS_RECOVERED |
  TWAI_ALERT_ERR_ACTIVE |
  TWAI_ALERT_ERR_PASS |
  TWAI_ALERT_BUS_ERROR |
  TWAI_ALERT_RX_QUEUE_FULL |
  TWAI_ALERT_RX_FIFO_OVERRUN |
  TWAI_ALERT_ARB_LOST;

static const uint32_t ECHO_RESP_ID_SAME = 1;     // 1: reply with same ID as RX; 0: use fixed ID below
static const uint32_t FIXED_RESP_ID     = 0x321; // used if ECHO_RESP_ID_SAME == 0

// ===== Stats =====
uint32_t rxCount=0, txCount=0, ackCount=0, txFail=0, busErr=0, busOff=0;

const char* stateToStr(twai_state_t s){
  switch(s){
    case TWAI_STATE_STOPPED: return "STOPPED";
    case TWAI_STATE_RUNNING: return "RUNNING";
    case TWAI_STATE_BUS_OFF: return "BUS_OFF";
    default: return "UNKNOWN";
  }
}

void printStatus(const char* tag){
  twai_status_info_t st{};
  if (twai_get_status_info(&st)==ESP_OK){
    Serial.printf("[STATUS] %s: state=%s to_tx=%lu to_rx=%lu tx_fail=%lu bus_err=%lu tx_err=%lu rx_err=%lu\n",
      tag, stateToStr(st.state),
      (unsigned long)st.msgs_to_tx, (unsigned long)st.msgs_to_rx,
      (unsigned long)st.tx_failed_count, (unsigned long)st.bus_error_count,
      (unsigned long)st.tx_error_counter, (unsigned long)st.rx_error_counter);
  }
}

void dumpAlerts(uint32_t a){
  if (!a) return;
  Serial.print("[ALERT] ");
  if (a & TWAI_ALERT_RX_DATA)         Serial.print("RX_DATA ");
  if (a & TWAI_ALERT_TX_SUCCESS)     { Serial.print("TX_SUCCESS "); ackCount++; }
  if (a & TWAI_ALERT_TX_FAILED)      { Serial.print("TX_FAILED ");  txFail++; }
  if (a & TWAI_ALERT_BUS_OFF)        { Serial.print("BUS_OFF ");    busOff++; }
  if (a & TWAI_ALERT_BUS_RECOVERED)   Serial.print("BUS_RECOVERED ");
  if (a & TWAI_ALERT_ERR_ACTIVE)      Serial.print("ERR_ACTIVE ");
  if (a & TWAI_ALERT_ERR_PASS)        Serial.print("ERR_PASS ");
  if (a & TWAI_ALERT_BUS_ERROR)      { Serial.print("BUS_ERROR ");  busErr++; }
  if (a & TWAI_ALERT_RX_QUEUE_FULL)   Serial.print("RX_Q_FULL ");
  if (a & TWAI_ALERT_RX_FIFO_OVERRUN) Serial.print("RX_FIFO_OVR ");
  if (a & TWAI_ALERT_ARB_LOST)        Serial.print("ARB_LOST ");
  Serial.println();
}

void recoverIfBusOff(){
  twai_status_info_t st{};
  if (twai_get_status_info(&st)==ESP_OK && st.state==TWAI_STATE_BUS_OFF){
    Serial.println("[RECOVERY] BUS_OFF -> initiating");
    twai_initiate_recovery();
    const uint32_t deadline = millis() + 1500;
    while (millis() < deadline){
      uint32_t a=0;
      if (twai_read_alerts(&a, pdMS_TO_TICKS(50))==ESP_OK && a){
        dumpAlerts(a);
        if (a & TWAI_ALERT_BUS_RECOVERED){
          Serial.println("[RECOVERY] Recovered");
          break;
        }
      }
      yield();
    }
  }
}

bool startNormal(){
  twai_general_config_t g =
    TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TWAI_TX_GPIO, (gpio_num_t)TWAI_RX_GPIO, TWAI_MODE_NORMAL);
  g.tx_queue_len = 32;
  g.rx_queue_len = 64;
  g.alerts_enabled = ALERTS;
  g.clkout_divider = 0;

  Serial.printf("[TWAI] Install RX/Echo - TX=%d RX=%d bitrate=%s\n",
                TWAI_TX_GPIO, TWAI_RX_GPIO, (tcfg.brp==32?"250k":"500k/other"));
  if (twai_driver_install(&g, &tcfg, &fcfg)!=ESP_OK){ Serial.println("[TWAI] install FAIL"); return false; }
  if (twai_start()!=ESP_OK){ Serial.println("[TWAI] start FAIL"); twai_driver_uninstall(); return false; }
  Serial.println("[TWAI] started (NORMAL)");
  recoverIfBusOff();
  printStatus("start");
  return true;
}

void printMsg(const char* tag, const twai_message_t& m){
  Serial.printf("%s id=0x%X %s %s dlc=%u  ",
    tag, m.identifier, m.extd?"(EXT)":"(STD)", m.rtr?"(RTR)":"     ", m.data_length_code);
  for (int i=0;i<m.data_length_code;i++) Serial.printf("%02X ", m.data[i]);
  Serial.println();
}

bool txEchoReply(const twai_message_t& rx){
  twai_message_t tx{};
  tx.identifier = ECHO_RESP_ID_SAME ? rx.identifier : FIXED_RESP_ID;
  tx.extd       = rx.extd;
  tx.rtr        = 0;
  tx.data_length_code = 8;
  // “ECHO” header + copy first 4 bytes (or fill 0)
  tx.data[0]='E'; tx.data[1]='C'; tx.data[2]='H'; tx.data[3]='O';
  for (int i=4;i<8;i++) tx.data[i] = (i-4 < rx.data_length_code) ? rx.data[i-4] : 0;

  esp_err_t e = twai_transmit(&tx, pdMS_TO_TICKS(200));
  if (e==ESP_OK){ txCount++; printMsg("[TX echo]", tx); return true; }
  if (e==ESP_ERR_TIMEOUT) Serial.println("[TX echo] queue timeout");
  else                    Serial.printf("[TX echo] error=%d\n",(int)e);
  return false;
}

void healthEvery(uint32_t sec=5){
  static uint32_t next=0;
  if (millis() < next) return;
  next = millis() + sec*1000UL;
  Serial.printf("[HEALTH] rx=%lu tx=%lu ack=%lu txFail=%lu busErr=%lu busOff=%lu\n",
    (unsigned long)rxCount, (unsigned long)txCount, (unsigned long)ackCount,
    (unsigned long)txFail, (unsigned long)busErr, (unsigned long)busOff);
  printStatus("periodic");
}

void setup(){
  Serial.begin(115200);
  delay(200);
  Serial.println("\nTWAI RECEIVER / ECHO (NORMAL) — ACK + reply");
  Serial.println("Wiring: TX=GPIO4->CTX, RX=GPIO5<-CRX, Bitrate=250 kbps (match master)");
  if (!startNormal()){
    Serial.println("FATAL: start failed"); while(true){ delay(1000); }
  }
}

void loop(){
  // 1) Handle alerts (RX notify, BUS_OFF/RECOVERED, errors, ACKs)
  uint32_t a=0;
  if (twai_read_alerts(&a, pdMS_TO_TICKS(10))==ESP_OK && a){
    dumpAlerts(a);
    if (a & TWAI_ALERT_BUS_OFF) recoverIfBusOff();
  }

  // 2) Drain RX and echo reply
  twai_message_t m{};
  while (twai_receive(&m, 0) == ESP_OK){
    rxCount++;
    printMsg("[RX]", m);
    if (!m.rtr) txEchoReply(m);            // For data frames, send echo
    // (Optional) If m.rtr==1, you may choose to respond with data here.
  }

  // 3) Periodic health
  healthEvery(5);
  yield();
}
