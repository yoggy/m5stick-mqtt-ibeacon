#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#include <SPI.h>
#include <U8g2lib.h>

#include <PubSubClient.h>

#include <string>
#include <sstream>
#include <vector>

#include "config.h"

#define pin_ir      17
#define pin_led     19
#define pin_button  35
#define pin_buzzer  26
#define pin_grove_y 13
#define pin_grove_w 25

U8G2_SH1107_64X128_1_4W_HW_SPI u8g2(U8G2_R1, 14, 27, 33);

BLEScan* ble_scan;
WiFiClient wifi_client;
PubSubClient mqtt_client(mqtt_host, mqtt_port, nullptr, wifi_client);

class IBeaconInfo {
  public:
    std::string addr;
    int rssi;
    int major;
    int minor;
    int power;
    std::string uuid;

    static IBeaconInfo create(BLEAdvertisedDevice &dev)
    {
      IBeaconInfo info;

      info.addr = dev.getAddress().toString().c_str();
      info.rssi = dev.getRSSI();

      std::string data = dev.getManufacturerData();
      char uuid[80];
      sprintf(uuid, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x"
              , data[4], data[5], data[6], data[7], data[8], data[9], data[10], data[11], data[12], data[13]
              , data[14], data[15], data[16], data[17], data[18], data[19]);
      info.uuid = uuid;

      info.major = (int)(((data[20] & 0xff) << 8) + (data[21] & 0xff));
      info.minor = (int)(((data[22] & 0xff) << 8) + (data[23] & 0xff));
      info.power = (signed char)(data[24] & 0xff);

      return info;
    }

    static bool is_ibeacon(BLEAdvertisedDevice &dev)
    {
      std::string data = dev.getManufacturerData();
      if (data.length() == 25 && data[0] == 0x4c && data[1] == 0x00 && data[2] == 0x02 && data[3] == 0x15) {
        return true;
      }
      return false;
    }

    std::string to_json() const
    {
      std::stringstream ss;
      ss << "{";
      ss << "\"" << "addr" << "\"" << ":" << "\"" << addr << "\"";
      ss << ",";
      ss << "\"" << "rssi" << "\"" << ":" << rssi;
      ss << ",";
      //      ss << "\"" << "uuid" << "\"" << ":" << "\"" << uuid << "\"";
      //      ss << ",";
      //      ss << "\"" << "major" << "\"" << ":" << major;
      //      ss << ",";
      //      ss << "\"" << "minor" << "\"" << ":" << minor;
      //      ss << ",";
      ss << "\"" << "power" << "\"" << ":" << power;
      ss << "}";

      return ss.str();
    }
};

std::vector<IBeaconInfo> ibeacons;

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  public:
    void onResult(BLEAdvertisedDevice dev) {
      if (IBeaconInfo::is_ibeacon(dev) == true) {
        IBeaconInfo info = IBeaconInfo::create(dev);

        bool found = false;
        for (auto i = ibeacons.begin(); i != ibeacons.end(); ++i) {
          if (i->addr == info.addr) {
            found = true;
            break;
          }
        }
        if (found == false) {
          ibeacons.push_back(info);
        }
      }
    }
};

void msg(const std::string &str)
{
  u8g2.firstPage();
  do {
    u8g2.drawStr(0, 16, str.c_str());
  } while (u8g2.nextPage());
  Serial.println(str.c_str());
}

void setup()
{
  Serial.begin(115200);

  u8g2.begin();
  u8g2.setFont(u8g2_font_7x14_tf);

  pinMode(pin_ir, OUTPUT);
  pinMode(pin_led, OUTPUT);
  pinMode(pin_button, INPUT_PULLUP);
  pinMode(pin_buzzer, OUTPUT);

  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(wifi_ssid, wifi_password);
  int count = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    switch (count) {
      case 0:
        msg("|");
        break;
      case 1:
        msg("/");
        break;
      case 2:
        msg("-");
        break;
      case 3:
        msg("\\");
        break;
    }
    count = (count + 1) % 4;
  }
  msg("WiFi connected!");
  delay(1000);

  bool rv = false;
  if (mqtt_use_auth == true) {
    rv = mqtt_client.connect(mqtt_client_id, mqtt_username, mqtt_password);
  }
  else {
    rv = mqtt_client.connect(mqtt_client_id);
  }
  if (rv == false) {
    msg("mqtt connecting failed...");
    reboot();
  }
  msg("MQTT connected!");
  delay(1000);

  BLEDevice::init("");
  ble_scan = BLEDevice::getScan();
  ble_scan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), true);
  ble_scan->setActiveScan(true);
}

void loop()
{
  if (!mqtt_client.connected()) {
    Serial.println("MQTT disconnected...");
    reboot();
  }
  mqtt_client.loop();
  
  ibeacons.clear();
  ble_scan->start(3); // unit:seconds

  for (auto i = ibeacons.begin(); i != ibeacons.end(); ++i) {
    Serial.println(i->to_json().c_str());
    mqtt_client.publish(mqtt_publish_topic, i->to_json().c_str());
  }

  u8g2.firstPage();
  do {
    u8g2.drawStr(0, 16, "mqtt-ibeacons");

    std::stringstream ss;

    ss.str("");
    ss << "topic:" << mqtt_publish_topic;
    u8g2.drawStr(0, 32, ss.str().c_str());

    ss.str("");
    if (ibeacons.size() > 0) {
      ss << "found:" << ibeacons.size();
    }
    else {
      ss << "not found";
    }
    u8g2.drawStr(0, 48, ss.str().c_str());

    for (auto i = ibeacons.begin(); i != ibeacons.end(); ++i) {
      if (i->rssi > -50) {
        u8g2.drawStr(0, 64, i->addr.c_str());
        break;
      }
    }

  } while (u8g2.nextPage());
}

void reboot() {
  Serial.println("REBOOT!!!!!");
  delay(1000);

  ESP.restart();
}
