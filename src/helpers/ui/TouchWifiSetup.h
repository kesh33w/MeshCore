#pragma once
#include "TouchWifiModel.h"
#include "ST7789LCDDisplay.h"
#include <WiFi.h>
#include <Wire.h>
#if ENV_PIN_SDA && ENV_PIN_SCL
#error "TFT touch requires exclusive Wire1; move telemetry to the main I2C bus first"
#endif
#if defined(DISPLAY_ROTATION) && DISPLAY_ROTATION != 3
#error "Verify the TFT touch transform before changing display rotation"
#endif

// Only active during the existing physical/blank-SSID recovery boot. The mesh
// loop still runs on every iteration; scans and connection attempts are async.
class TouchWifiSetup {
  enum State { LIST, SSID, PASSWORD, STOP_PORTAL, CONNECTING, FAILED, SAVED, FALLBACK };
#ifdef TFT_WIFI_SETUP_BENCH
  bool bench_observing = false;
  bool bench_register_read = false;
  uint32_t bench_next_report = 0;
  uint8_t bench_previous[2][16] = {};
  size_t bench_previous_n[2] = {99,99};
  unsigned bench_reports = 0;
  void observeTouch(uint32_t now) {
    if ((int32_t)(now-bench_next_report)<0) return;
    bench_next_report=now+50;
    bench_register_read=!bench_register_read;
    const unsigned mode=bench_register_read?1:0;
    uint8_t raw[16]={};size_t n=0;
    Wire1.beginTransmission(0x2E);
    if(bench_register_read) Wire1.write(0x00);
    uint8_t ack=Wire1.endTransmission(!bench_register_read);
    if(ack==0) {
      Wire1.requestFrom((uint8_t)0x2E,(size_t)16,true);
      while(Wire1.available() && n<sizeof(raw)) raw[n++]=Wire1.read();
    }
    if(bench_reports<200 && (n!=bench_previous_n[mode] || memcmp(raw,bench_previous[mode],16))) {
      Serial.printf("Touch diagnostic: mode=%s ack=%u n=%u raw=",mode?"reg0":"direct",ack,unsigned(n));
      for(size_t i=0;i<n;i++) Serial.printf("%02x",raw[i]);
      Serial.println();bench_reports++;
      memcpy(bench_previous[mode],raw,16);bench_previous_n[mode]=n;
    }
  }
#endif
  State state = LIST;
  TouchWifiCallbacks* cb = nullptr;
  ST7789LCDDisplay* screen = nullptr;
  bool available = false, active = false, dirty = true, pressed = false, upper = false, show_password = false;
  uint8_t idle = 0, page = 0, keyboard = 0;
  uint32_t next_poll = 0, deadline = 0, next_scan_retry = 0;
  int count = -1;
  char ssid[32] = {};
  TouchWifiModel::Entry password, name;
  const char* message = "Choose a network";
  bool encrypted = true, save_failed = false, last_flip = false;

  void label(int x, int y, const char* text, uint8_t size=2) {
    auto& d=screen->nativeCanvas(); d.setCursor(x,y); d.setTextSize(size); d.setTextColor(ST77XX_WHITE); d.print(text);
  }
  void button(int x, int y, int w, int h, const char* text) {
    auto& d=screen->nativeCanvas(); d.fillRect(x+1,y+1,w-2,h-2,0x2104); label(x+5,y+9,text);
  }
  void edit(State s) { state=s; keyboard=0; upper=false; show_password=false; dirty=true; }
  void scan(bool fresh) {
    if (fresh && WiFi.scanComplete() != WIFI_SCAN_RUNNING) WiFi.scanDelete();
    count=WiFi.scanComplete();
    if (count==WIFI_SCAN_FAILED) count=WiFi.scanNetworks(true, false, false, 600);
    next_scan_retry=millis()+5000;
    page=0; dirty=true;
  }
  void render() {
    auto& d=screen->nativeCanvas(); d.fillScreen(ST77XX_BLACK); d.setTextWrap(false);
    label(6,6,"Wi-Fi setup");
    if (state==LIST) {
      label(6,32,count==WIFI_SCAN_RUNNING ? "Scanning..." : (count==WIFI_SCAN_FAILED ? "Scan failed: tap Scan" : "Choose a network"),1);
      for (int row=0;row<4;row++) {
        int i=page*4+row;
        if(i>=count || i>=20) break;
        String network=WiFi.SSID(i); char shown[25]; network.toCharArray(shown,sizeof(shown));
        button(0,48+row*34,320,34,network.length()?shown:"(hidden: enter name)");
      }
      button(0,186,80,27,"Scan"); button(80,186,80,27,"Next");
      button(160,186,160,27,"Type name");
      button(0,214,160,26,"Use phone"); button(160,214,160,26,"Cancel");
    } else if(state==SSID || state==PASSWORD) {
      if(state==PASSWORD) button(230,0,90,28,show_password?"Hide":"Show");
      label(6,29,state==SSID?"Network name":(show_password?"Password (visible)":"Password (hidden)"),1);
      char shown[25]={};
      const char* value=state==SSID?name.text:password.text;
      size_t n=strlen(value), length=n>24?24:n;
      if(state==PASSWORD && !show_password) memset(shown,'*',length); else memcpy(shown,value+n-length,length);
      label(6,44,shown);
      const char* keys=keyset();
      for(int i=0;i<40;i++) {
        char key[2]={keys[i],0};
        button((i%10)*32,72+(i/10)*30,32,30,key[0]==' '?"sp":key);
      }
      button(0,194,64,46,"Back"); button(64,194,64,46,keyboard?"abc":"123");
      button(128,194,64,46,upper?"abc":"ABC"); button(192,194,64,46,"Del");
      button(256,194,64,46,state==SSID?"Next":"Join");
    } else if(state==CONNECTING || state==STOP_PORTAL) {
      label(6,60,"Connecting..."); label(6,90,"Saved settings unchanged",1);
      button(0,190,320,50,"Cancel");
    } else if(state==FAILED) {
      label(6,65,message,1); label(6,90,save_failed?"Restart to check saved settings":"Saved settings unchanged",1);
      button(0,140,160,50,"Retry"); button(160,140,160,50,"Networks");
      button(0,190,320,50,"Use phone");
    } else if(state==SAVED) { label(6,65,"Connected and saved"); label(6,100,"Restarting...",1); }
  }
  const char* keyset() const {
    // Exactly 40 keys per page; all printable ASCII characters are available.
    if(keyboard==1) return "!@#$%^&*()" "-_=+[]{};:" "'\",.<>/?\\|" "`~        ";
    return upper ? "QWERTYUIOP" "ASDFGHJKL " "ZXCVBNM   " "0123456789" :
                   "qwertyuiop" "asdfghjkl " "zxcvbnm   " "0123456789";
  }
  void beginConnection() {
    save_failed = false;
    if(!TouchWifiModel::validCredentials(ssid,password.text)) {
      message="Use 8-63 characters (or open Wi-Fi)"; state=FAILED; password.clear(); dirty=true; return;
    }
    if(encrypted && !password.text[0]) {
      message="This network needs a password"; state=FAILED; dirty=true; return;
    }
    state=STOP_PORTAL; deadline=millis()+10000; dirty=true;
  }
  void tap(uint16_t x,uint16_t y) {
    if(state==LIST) {
      if(y>=48 && y<184) {
        int i=page*4+(y-48)/34;
        if(i<count && i<20) {
          String chosen=WiFi.SSID(i);
          if(chosen.length()>=sizeof(ssid)) { message="Network name exceeds saved limit"; state=FAILED; dirty=true; return; }
          chosen.toCharArray(ssid,sizeof(ssid)); encrypted=WiFi.encryptionType(i)!=WIFI_AUTH_OPEN;
          password.clear(); name.clear(); edit(chosen.length()?PASSWORD:SSID);
        }
      } else if(y>=186 && y<213) {
        if(x<80) scan(true);
        else if(x<160) { page=(page+1)*4<count && (page+1)*4<20?page+1:0; dirty=true; }
        else { name.clear(); password.clear(); encrypted=false; edit(SSID); }
      } else if(y>=214) {
        password.clear(); name.clear();
        if(x<160) { state=FALLBACK; dirty=false; } else cb->touchRestart(false);
      }
    } else if(state==SSID || state==PASSWORD) {
      if(state==PASSWORD && y<28 && x>=230) { show_password=!show_password; dirty=true; return; }
      auto& entry=state==SSID?name:password;
      if(y>=72 && y<192) entry.append(keyset()[(y-72)/30*10+x/32],state==SSID?31:63);
      else if(y>=194) {
        if(x<64) { password.clear(); name.clear(); state=LIST; scan(false); }
        else if(x<128) keyboard=!keyboard;
        else if(x<192) upper=!upper;
        else if(x<256) entry.backspace();
        else if(state==SSID) {
          if(name.text[0]) { strncpy(ssid,name.text,sizeof(ssid)); name.clear(); edit(PASSWORD); }
        } else beginConnection();
      }
      dirty=true;
    } else if((state==CONNECTING || state==STOP_PORTAL) && y>=190) {
      cb->touchCancelConnection(); password.clear(); cb->touchRestart(false);
    } else if(state==FAILED) {
      if(y>=190) cb->touchRestart(true);
      else if(y>=140) {
        if(x<160 && ssid[0]) edit(PASSWORD);
        else { state=LIST; scan(true); }
        dirty=true;
      }
    }
  }
 public:
#ifdef TFT_WIFI_SETUP_BENCH
  void bench(const char* action, const char* saved_ssid, const char* saved_password, char* reply) {
    if (!strcmp(action,"observe")) {
      password.clear();name.clear();bench_observing=true;bench_reports=0;
      bench_previous_n[0]=bench_previous_n[1]=99;
      screen->turnOn();auto& d=screen->nativeCanvas();d.fillScreen(ST77XX_BLACK);d.setTextWrap(false);
      label(12,18,"Touch diagnostic");label(12,65,"Tap circle, then lift");
      label(12,100,"No passwords. No settings saved.",1);
      d.drawCircle(160,160,15,ST77XX_WHITE);
      strcpy(reply,"OK - touch observation only");return;
    }
    if (!strcmp(action,"observe stop")) {bench_observing=false;dirty=true;strcpy(reply,"OK");return;}
    if (!strcmp(action,"setup")) { cb->touchRestart(true); return; }
    if (!strcmp(action,"scan") && active) { state=LIST; scan(true); strcpy(reply,"OK"); return; }
    if (!strcmp(action,"state")) {
      bool found=false; int n=WiFi.scanComplete();
      for(int i=0;i<n && i<20;i++) if(WiFi.SSID(i)==saved_ssid) found=true;
      snprintf(reply,160,"touch state=%u active=%u controller=%u scan=%d saved_network_visible=%u wifi_mode=%u wifi_status=%u",
               unsigned(state),active,available,n,found,unsigned(WiFi.getMode()),unsigned(WiFi.status())); return;
    }
    if (!strcmp(action,"exit")) { password.clear(); cb->touchRestart(false); return; }
    if (!active) { strcpy(reply,"Error: touch setup inactive"); return; }
    if (!strcmp(action,"cancel")) { tap(20,210); strcpy(reply,"OK"); return; }
    if (!strcmp(action,"wrong") || !strcmp(action,"correct")) {
      strncpy(ssid,saved_ssid,sizeof(ssid)); ssid[sizeof(ssid)-1]=0;
      password.clear();
      strncpy(password.text,saved_password,sizeof(password.text)-1);
      if (!strcmp(action,"wrong")) password.text[0]=password.text[0]=='x'?'y':'x';
      encrypted=true; beginConnection(); strcpy(reply,"OK"); return;
    }
    strcpy(reply,"Error: unsupported touch test");
  }
#endif
  void begin(ST7789LCDDisplay* display,TouchWifiCallbacks* callbacks) {
    screen=display; cb=callbacks;
    Wire1.begin(47,48,100000); Wire1.setTimeOut(8);
    Wire1.beginTransmission(0x2E); available=Wire1.endTransmission()==0;
    Serial.printf("Touch WiFi: controller %s\n",available?"detected":"unavailable; portal retained");
    active=available && cb && cb->touchSetupAllowed();
    if(active) { screen->turnOn(); scan(false); }
  }
  bool loop(bool flipped) {
#ifdef TFT_WIFI_SETUP_BENCH
    if(bench_observing) { observeTouch(millis()); return true; }
#endif
    if(!active || state==FALLBACK) return false;
    if (last_flip != flipped) { last_flip=flipped; dirty=true; }
    uint32_t now=millis();
    if(state==LIST) {
      int n=WiFi.scanComplete();
      if(n==WIFI_SCAN_FAILED && (int32_t)(now-next_scan_retry)>=0) {
        n=WiFi.scanNetworks(true, false, false, 600);
        next_scan_retry=now+5000;
      }
      if(n!=count) {count=n;dirty=true;}
    }
    if(state==STOP_PORTAL) {
      if(cb->touchPrepareConnection()) {
        if (cb->touchStartConnection(ssid,password.text)) {
          state=CONNECTING; deadline=now+30000;
        } else { message="Connection could not start"; state=FAILED; password.clear(); }
        dirty=true;
      } else if((int32_t)(now-deadline)>=0) {
        message="Setup is busy; please retry"; state=FAILED; password.clear(); dirty=true;
      }
    } else if(state==CONNECTING) {
      if(cb->touchConnectionReady()) {
        if(cb->touchSaveCredentials(ssid,password.text)) {
          state=SAVED; deadline=now+1000;
        } else {
          cb->touchCancelConnection(); message="Save failed; use phone setup"; save_failed=true; state=FAILED;
        }
        password.clear(); dirty=true;
      } else if((int32_t)(now-deadline)>=0) {
        cb->touchCancelConnection(); password.clear(); message="Cannot connect. Check Wi-Fi password."; state=FAILED; dirty=true;
      }
    } else if(state==SAVED && (int32_t)(now-deadline)>=0) cb->touchRestart(false);
    if((int32_t)(now-next_poll)>=0 && state!=SAVED) {
      next_poll=now+50;
      uint8_t data[16]; size_t n=0;
      Wire1.beginTransmission(0x2E); Wire1.write(0x00);
      if(Wire1.endTransmission(false)==0) {
        Wire1.requestFrom((uint8_t)0x2E,(size_t)16,true);
        while(Wire1.available() && n<sizeof(data)) data[n++]=Wire1.read();
      }
      uint16_t x,y;
      if(TouchWifiModel::point(data,n,flipped,x,y)) {
        idle=0; if(!pressed) { pressed=true; tap(x,y); }
      } else if(n==16 && (data[2]==0 || (data[2]==1 && (data[3]>>6)==1))) { if(++idle>=2) {pressed=false;idle=2;} }
      // A failed/partial read never creates an artificial release.
    }
    if(dirty && state!=FALLBACK) { render(); dirty=false; }
    return state!=FALLBACK;
  }
};
