// Bad Apple for ESP32 with OLED SSD1306 | 2018 by Hackerspace-FFM.de | MIT-License.
#include "FS.h"
#include "SPIFFS.h"
#include "SSD1306.h"
#include "heatshrink_decoder.h"

// Board Boot SW GPIO 0
#define BOOT_SW 0

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

SSD1306 display (0x3c, I2C_SDA, I2C_SCL);

#if HEATSHRINK_DYNAMIC_ALLOC
#error HEATSHRINK_DYNAMIC_ALLOC must be false for static allocation test suite.
#endif

static heatshrink_decoder hsd;

// global storage for putPixels
int16_t curr_x = 0;
int16_t curr_y = 0;

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

uint32_t lastRefresh = 0;

void putPixels(uint8_t c, int32_t len) {
  uint8_t b = 0;
  while(len--) {
    b = 128;
    for(int i=0; i<8; i++) {
      if(c & b) {
        display.setColor(WHITE);
      } else {
        display.setColor(BLACK);
      }
      b >>= 1;
      display.setPixel(curr_x, curr_y);
      curr_x++;
      if(curr_x >= 128) {
        curr_x = 0;
        curr_y++;
        if(curr_y >= 64) {
          curr_y = 0;
          display.display();
          //display.clear();
          // 30 fps target rate
          if(digitalRead(BOOT_SW)) while((millis() - lastRefresh) < 33) ;
          lastRefresh = millis();
        }
      }
    }
  }
}

void decodeRLE(uint8_t c) {
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
    size_t rle_bufhead = 0;
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
    display.clear();
    display.display();
    curr_x = 0;
    curr_y = 0;
    runlength = -1;
    c_to_dup = -1;
    lastRefresh = millis();

    // init decoder
    heatshrink_decoder_reset(&hsd);
    size_t   count  = 0;
    uint32_t sunk   = 0;
    size_t toRead;
    size_t toSink = 0;
    uint32_t sinkHead = 0;


    // Go through file...
    while(filelen) {
      if(toSink == 0) {
        toRead = filelen;
        if(toRead > READBUFSIZE) toRead = READBUFSIZE;
        file.read(compbuf, toRead);
        filelen -= toRead;
        toSink = toRead;
        sinkHead = 0;
      }

      // uncompress buffer
      HSD_sink_res sres;
      sres = heatshrink_decoder_sink(&hsd, &compbuf[sinkHead], toSink, &count);
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
          if(pres < 0) {
            Serial.print("POLL ERR! ");
            Serial.println(pres);
            return;
          }

          rle_bufhead = 0;
          while(rle_size) {
            rle_size--;
            if(rle_bufhead >= RLEBUFSIZE) {
              Serial.println("RLE_SIZE ERR!");
              return;
            }
            decodeRLE(rle_buf[rle_bufhead++]);
          }
      } while (pres == HSDR_POLL_MORE);
    }
    file.close();
    Serial.println("Done.");

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
    Serial.print("totalBytes(): ");
    Serial.println(SPIFFS.totalBytes());
    Serial.print("usedBytes(): ");
    Serial.println(SPIFFS.usedBytes());
    listDir(SPIFFS, "/", 0);
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

