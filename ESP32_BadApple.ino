// Bad Apple for ESP32 with OLED SSD1306 | 2018 by Hackerspace-FFM.de | MIT-License.
#include "FS.h"
#include "SPIFFS.h"
#include "SSD1306.h"
#include "heatshrink_decoder.h"

// Board Boot SW GPIO 0
#define BOOT_SW 0

// Frame counter
#define ENABLE_FRAME_COUNTER

// Disable heatshrink error checking
#define DISABLE_HS_ERROR

// Hints:
// * Adjust the display pins below
// * After uploading to ESP32, also do "ESP32 Sketch Data Upload" from Arduino

// SSD1306 display I2C bus
// For Heltec
// #define I2C_SCL 15
// #define I2C_SDA 4
// #define RESET_OLED 16

// For Wemos Lolin32 ESP32
#define I2C_SCL 4
#define I2C_SDA 5

#define OLED_BRIGHTNESS 16

// MAX freq for SCL is 4 MHz, However, Actual measured value is 892 kHz . (ESP32-D0WDQ6 (revision 1))
// see Inter-Integrated Circuit (I2C)
// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2c.html
#define I2C_SCLK_FREQ 4000000
#ifdef I2C_SCLK_FREQ
SSD1306 display (0x3c, I2C_SDA, I2C_SCL, GEOMETRY_128_64, I2C_ONE, I2C_SCLK_FREQ);
#else
SSD1306 display (0x3c, I2C_SDA, I2C_SCL);
#endif

// Enable I2C Clock up 892kHz to 1.31MHz (It Actual measured value with ESP32-D0WDQ6 (revision 1))
// #define ENABLE_EXTRA_I2C_CLOCK_UP

#if HEATSHRINK_DYNAMIC_ALLOC
#error HEATSHRINK_DYNAMIC_ALLOC must be false for static allocation test suite.
#endif

static heatshrink_decoder hsd;

// global storage for putPixels
int32_t curr_x = 0;
int32_t curr_y = 0;

// global storage for decodeRLE
int32_t runlength = -1;
int32_t c_to_dup = -1;

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("Failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if(levels){
                listDir(fs, file.name(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

volatile unsigned long lastRefresh = 0;
// 30 fps target rate = 33.333us
#define FRAME_DERAY_US 33333UL
#ifdef ENABLE_FRAME_COUNTER
int32_t frame = 0;
#endif
uint32_t* pImage;
uint32_t b = 0x01;

volatile bool isButtonPressing = false;

void ARDUINO_ISR_ATTR isr() {
    lastRefresh = micros();
    isButtonPressing = (digitalRead(BOOT_SW) == LOW);
}

void putPixels(uint32_t c, int32_t len) {
  uint32_t d1;
  uint32_t d2;

  while(len--) {
    // Direct Draw OLED buffer
    // OLED Buffer Image Rotate 90 Convert X-Y and Byte structure
    {
      // 4 dot(Direct Access 4 byte, 32 bit)
      d1 = 0;
      d2 = 0;
      if (c == 0xff) {
        d1  = b; d1 <<= 8;
        d1 |= b; d1 <<= 8;
        d1 |= b; d1 <<= 8;
        d1 |= b;

        d2 = d1;
      } else if (c != 0x00) {
        if ((c & 0xF0) != 0x00) {
          d1  = (c & 0x10) ? b : 0; d1 <<= 8;
          d1 |= (c & 0x20) ? b : 0; d1 <<= 8;
          d1 |= (c & 0x40) ? b : 0; d1 <<= 8;
          d1 |= (c & 0x80) ? b : 0;
        }

        if ((c & 0x0F) != 0x00) {
          d2  = (c & 0x01) ? b : 0; d2 <<= 8;
          d2 |= (c & 0x02) ? b : 0; d2 <<= 8;
          d2 |= (c & 0x04) ? b : 0; d2 <<= 8;
          d2 |= (c & 0x08) ? b : 0;
        }
      }

      if (b == 0x01) {
        *pImage++ = d1;
        *pImage   = d2;
      } else if (c != 0x00) {
        *pImage++ |= d1;
        *pImage   |= d2;
      } else {
        pImage++;
      }
      pImage++;
    }

    curr_x++;
    if(curr_x == 128/8) {
      curr_x = 0;
      pImage -= 128/4;

      b <<= 1;
      if(b == 0x100) {
        // Next Page
        pImage += 128/4;
        b = 0x01;

        curr_y++;
        if(curr_y == 8) {
          curr_y = 0;
          pImage = (uint32_t*)display.buffer;

          // Update Display frame
          display.display();
          //display.clear();

          if(!isButtonPressing) {
            // 30 fps target rate = 33.333us
            lastRefresh += FRAME_DERAY_US;
#ifdef ENABLE_FRAME_COUNTER
            // Adjust 33.334us every 3 frame
            if ((++frame % 3) == 0) lastRefresh++;
#endif
            while(micros() < lastRefresh) ;
          }
        }
      }
    }
  }
}

void decodeRLE(uint32_t c) {
    if(c_to_dup == -1) {
      if((c == 0x55) || (c == 0xaa)) {
        c_to_dup = c;
      } else {
        putPixels(c, 1);
      }
    } else {
      if(runlength == -1) {
        if(c == 0) {
          putPixels(c_to_dup & 0xff, 1);
          c_to_dup = -1;
        } else if((c & 0x80) == 0) {
          if(c_to_dup == 0x55) {
            putPixels(0, c);
          } else {
            putPixels(255, c);
          }
          c_to_dup = -1;
        } else {
          runlength = c & 0x7f;
        }
      } else {
        runlength = runlength | (c << 7);
          if(c_to_dup == 0x55) {
            putPixels(0, runlength);
          } else {
            putPixels(255, runlength);
          }
          c_to_dup = -1;
          runlength = -1;
      }
    }
}

#define RLEBUFSIZE 4096
#define READBUFSIZE 2048
void readFile(fs::FS &fs, const char * path){
    static uint8_t rle_buf[RLEBUFSIZE];
    uint8_t* p_rle_buf;
    size_t rle_size = 0;

    size_t filelen = 0;
    size_t filesize;
    static uint8_t compbuf[READBUFSIZE];

    Serial.printf("Reading file: %s\n", path);
    File file = fs.open(path);
    if(!file || file.isDirectory()){
        Serial.println("Failed to open file for reading");
        display.drawStringMaxWidth(0, 10, 128, "File open error. Upload video.hs using ESP32 Sketch Upload."); display.display();
        return;
    }
    filelen = file.size();
    filesize = filelen;
    Serial.printf("File size: %d\n", filelen);

    // init display, putPixels and decodeRLE
    display.resetDisplay();
    curr_x = 0;
    curr_y = 0;
    pImage = (uint32_t*)display.buffer;
    runlength = -1;
    c_to_dup = -1;

    // init decoder
    heatshrink_decoder_reset(&hsd);
    size_t   count  = 0;
    uint32_t sunk   = 0;
    size_t toSink = 0;
    uint32_t sinkHead = 0;

    lastRefresh = micros();

    // Go through file...
    while(filelen) {
      if(toSink == 0) {
        toSink = file.read(compbuf, READBUFSIZE);
        filelen -= toSink;
        sinkHead = 0;
      }

      // uncompress buffer
      heatshrink_decoder_sink(&hsd, &compbuf[sinkHead], toSink, &count);
      //Serial.print("^^ sinked ");
      //Serial.println(count);
      toSink -= count;
      sinkHead = count;
      sunk += count;
      if (sunk == filesize) {
        heatshrink_decoder_finish(&hsd);
      }

      HSD_poll_res pres;
      do {
          rle_size = 0;
          pres = heatshrink_decoder_poll(&hsd, rle_buf, RLEBUFSIZE, &rle_size);
          //Serial.print("^^ polled ");
          //Serial.println(rle_size);
#ifndef DISABLE_HS_ERROR
          if(pres < 0) {
            Serial.print("POLL ERR! ");
            Serial.println(pres);
            return;
          }
#endif

          p_rle_buf = rle_buf;
          while(rle_size) {
            rle_size--;
#ifndef DISABLE_HS_ERROR
            if(rle_bufhead >= RLEBUFSIZE) {
              Serial.println("RLE_SIZE ERR!");
              return;
            }
#endif
            decodeRLE(*(p_rle_buf++));
          }
      } while (pres == HSDR_POLL_MORE);
    }
    file.close();
#ifdef ENABLE_FRAME_COUNTER
    Serial.print("Done. ");
    Serial.println(frame);
#else
    Serial.println("Done.");
#endif

    // 10 sec
    delay(10000);
    // Reset to Infinite Loop Demo !
    ESP.restart();
}



void setup(){
    Serial.begin(115200);
#ifdef RESET_OLED
    // Reset for some displays
    pinMode(RESET_OLED, OUTPUT); digitalWrite(RESET_OLED, LOW); delay(50); digitalWrite(RESET_OLED, HIGH);
#endif
    display.init();
#ifdef OLED_BRIGHTNESS
    display.setBrightness(OLED_BRIGHTNESS);
#endif
    display.flipScreenVertically ();
    display.clear();
    display.setTextAlignment (TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
    display.setColor(WHITE);
    display.drawString(0, 0, "Mounting SPIFFS...     ");
    display.display();
    if(!SPIFFS.begin()){
        Serial.println("SPIFFS mount failed");
        display.drawStringMaxWidth(0, 10, 128, "SPIFFS mount failed. Upload video.hs using ESP32 Sketch Upload."); display.display();
        return;
    }

    pinMode(BOOT_SW, INPUT_PULLUP);
    attachInterrupt(BOOT_SW, isr, CHANGE);
    Serial.print("totalBytes(): ");
    Serial.println(SPIFFS.totalBytes());
    Serial.print("usedBytes(): ");
    Serial.println(SPIFFS.usedBytes());
    listDir(SPIFFS, "/", 0);

#ifdef ENABLE_EXTRA_I2C_CLOCK_UP
    // Direct Access I2C SCL frequency setting value
    // It Tested ESP32-D0WDQ6 (revision 1)
    uint32_t* ptr;
    ptr = (uint32_t*)0x3FF53000; // I2C_SCL_LOW_PERIOD_REG
    // *ptr = 30; // Don't work
    // *ptr = 31; // Sometime Stop Frame drawing
    // *ptr = 32; // Works
    *ptr = 35; // Safety value
    ptr = (uint32_t*)0x3FF53038; // I2C_SCL_HIGH_PERIOD_REG
    // *ptr = 0; // Works
    *ptr = 2; // Safety value
#endif

    readFile(SPIFFS, "/video.hs");

    //Serial.print("Format SPIFSS? (enter y for yes): ");
    // while(!Serial.available()) ;
    //if(Serial.read() == 'y') {
    //  bool ret = SPIFFS.format();
    //  if(ret) Serial.println("Success. "); else Serial.println("FAILED! ");
    //} else {
    //  Serial.println("Aborted.");
    //}
}

void loop(){

}

