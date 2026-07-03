// ============================================================
// ESP32-C6 Web UI v1 for STM32 FPGA logic analyzer
//
// ESP32-C6:
//   - Wi-Fi Access Point
//   - HTTP WebServer
//   - UART client to STM32
//
// Browser:
//   http://192.168.4.1
//
// STM32 machine protocol:
//   @PING
//   @STATUS
//   @SHOW
//   @CFG <rate_hz> <count> <pre> <ch> <R|F>
//   @RUN
//   @IMM
//   @READ
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

static const int STM32_UART_RX_PIN = 5;   // ESP32 RX  <- STM32 TX
static const int STM32_UART_TX_PIN = 4;   // ESP32 TX  -> STM32 RX

static const uint32_t USB_BAUD   = 115200;
static const uint32_t STM32_BAUD = 115200;

static const char *AP_SSID = "FPGA-LA";
static const char *AP_PASS = "12345678";

static const size_t MAX_SAMPLES = 1024;

HardwareSerial Stm32Serial(1);
WebServer server(80);
Preferences prefs;

static uint8_t samples[MAX_SAMPLES];
static size_t samplesCount = 0;

static uint32_t cfgRateHz = 1000000;
static uint16_t cfgCount = 512;
static uint16_t cfgPre = 128;
static uint8_t cfgChannel = 0;
static char cfgEdge = 'R';

static String lastStatus = "No status yet";
static String lastError = "";

// ------------------------------------------------------------
// STM32 UART helpers
// ------------------------------------------------------------
static void flushStm32Input()
{
    while (Stm32Serial.available()) {
        Stm32Serial.read();
    }
}

static void sendToStm32(const String &cmd)
{
    Serial.print("ESP32 -> STM32: ");
    Serial.println(cmd);

    Stm32Serial.print(cmd);
    Stm32Serial.print("\r\n");
}

static bool readStm32Line(String &line, uint32_t timeoutMs)
{
    line = "";

    uint32_t start = millis();

    while ((millis() - start) < timeoutMs) {
        while (Stm32Serial.available()) {
            char c = (char)Stm32Serial.read();

            if (c == '\r') {
                continue;
            }

            if (c == '\n') {
                if (line.length() > 0) {
                    line.trim();
                    return true;
                }
            } else {
                line += c;

                if (line.length() > 512) {
                    line = "";
                    return false;
                }
            }
        }

        delay(1);
    }

    return false;
}

static bool waitForLinePrefix(
    const String &prefix,
    String &outLine,
    uint32_t timeoutMs
)
{
    uint32_t start = millis();

    while ((millis() - start) < timeoutMs) {
        String line;

        if (!readStm32Line(line, 200)) {
            continue;
        }

        Serial.println(line);

        if (line.startsWith(prefix)) {
            outLine = line;
            return true;
        }

        if (line.startsWith("@ERR")) {
            outLine = line;
            return false;
        }
    }

    outLine = "";
    return false;
}

static bool sendCommandWaitPrefix(
    const String &cmd,
    const String &prefix,
    uint32_t timeoutMs,
    String *response = nullptr
)
{
    flushStm32Input();
    sendToStm32(cmd);

    String line;
    bool ok = waitForLinePrefix(prefix, line, timeoutMs);

    if (response != nullptr) {
        *response = line;
    }

    if (!ok) {
        lastError = "No expected response: " + prefix + ", got: " + line;
        return false;
    }

    return true;
}

// ------------------------------------------------------------
// HEX parsing
// ------------------------------------------------------------
static int hexNibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool parseHexByte(char hi, char lo, uint8_t &value)
{
    int h = hexNibble(hi);
    int l = hexNibble(lo);

    if (h < 0 || l < 0) {
        return false;
    }

    value = (uint8_t)((h << 4) | l);
    return true;
}

static bool stm32ReadCapture()
{
    samplesCount = 0;

    String header;

    if (!sendCommandWaitPrefix("@READ", "@DATA", 3000, &header)) {
        lastError = "No @DATA from STM32";
        return false;
    }

    int expectedCount = 0;

    if (sscanf(header.c_str(), "@DATA %d", &expectedCount) != 1) {
        lastError = "Bad @DATA header";
        return false;
    }

    if (expectedCount < 0) {
        lastError = "Negative @DATA count";
        return false;
    }

    if ((size_t)expectedCount > MAX_SAMPLES) {
        expectedCount = MAX_SAMPLES;
    }

    while (true) {
        String line;

        if (!readStm32Line(line, 5000)) {
            lastError = "Timeout while reading HEX";
            return false;
        }

        if (line == "@END") {
            break;
        }

        if (line.startsWith("@ERR")) {
            lastError = line;
            return false;
        }

        line.trim();

        if ((line.length() % 2) != 0) {
            lastError = "Odd HEX line length";
            return false;
        }

        for (size_t i = 0; i < line.length(); i += 2) {
            if (samplesCount >= (size_t)expectedCount) {
                continue;
            }

            uint8_t b = 0;

            if (!parseHexByte(line[i], line[i + 1], b)) {
                lastError = "Bad HEX byte";
                return false;
            }

            samples[samplesCount++] = b;
        }
    }

    if (samplesCount != (size_t)expectedCount) {
        lastError = "Parsed count differs from expected";
        return false;
    }

    return true;
}

// ------------------------------------------------------------
// Web helpers
// ------------------------------------------------------------
static String htmlHeader()
{
    String h;
    h += "<!doctype html><html><head><meta charset='utf-8'>";
    h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    h += "<title>FPGA Logic Analyzer</title>";
    h += "<style>";
    h += "body{font-family:Arial, sans-serif;margin:20px;background:#111;color:#eee;}";
    h += "button,input,select{font-size:16px;margin:4px;padding:8px;}";
    h += "button{background:#2d6cdf;color:white;border:0;border-radius:6px;}";
    h += ".card{background:#1d1d1d;border:1px solid #333;border-radius:8px;padding:12px;margin:12px 0;}";
    h += "pre{background:#000;color:#0f0;padding:10px;overflow:auto;}";
    h += "#healthBox{min-height:150px;white-space:pre-wrap;}";
    h += "#apiStatus{min-height:24px;}";
    h += "canvas{background:#000;border:1px solid #333;width:100%;height:520px;}";
    h += "label{display:inline-block;margin:6px 12px 6px 0;}";
    h += "</style></head><body>";
    h += "<h1>FPGA Logic Analyzer</h1>";
    return h;
}

static String htmlFooter()
{
    return "</body></html>";
}

static String samplesHexString(size_t maxCount)
{
    String out;

    size_t n = samplesCount;
    if (n > maxCount) {
        n = maxCount;
    }

    for (size_t i = 0; i < n; i++) {
        if (samples[i] < 0x10) {
            out += "0";
        }
        out += String(samples[i], HEX);
    }

    out.toUpperCase();
    return out;
}

static void loadConfigFromFlash()
{
    prefs.begin("fpga-la", true); // read-only

    cfgRateHz  = prefs.getUInt("rate", 1000000);
    cfgCount   = prefs.getUShort("count", 512);
    cfgPre     = prefs.getUShort("pre", 128);
    cfgChannel = prefs.getUChar("ch", 0);

    String edge = prefs.getString("edge", "R");
    cfgEdge = (edge == "F") ? 'F' : 'R';

    prefs.end();

    if (cfgRateHz < 1) {
        cfgRateHz = 1000000;
    }

    if (cfgCount < 1) {
        cfgCount = 512;
    }

    if (cfgCount > MAX_SAMPLES) {
        cfgCount = MAX_SAMPLES;
    }

    if (cfgPre > cfgCount) {
        cfgPre = cfgCount;
    }

    if (cfgChannel > 7) {
        cfgChannel = 0;
    }
}

static void saveConfigToFlash()
{
    prefs.begin("fpga-la", false); // read/write

    prefs.putUInt("rate", cfgRateHz);
    prefs.putUShort("count", cfgCount);
    prefs.putUShort("pre", cfgPre);
    prefs.putUChar("ch", cfgChannel);
    prefs.putString("edge", String(cfgEdge));

    prefs.end();

    Serial.println("Config saved to flash");
}

// ------------------------------------------------------------
// Web routes
// ------------------------------------------------------------
static void handleRoot()
{
    String page = htmlHeader();

    page += "<div class='card'>";
    page += "<h2>Config</h2>";
    page += "<div>";
    page += "<label>Rate Hz <input id='cfgRate' name='rate' value='" + String(cfgRateHz) + "'></label>";
    page += "<label>Count <input id='cfgCount' name='count' value='" + String(cfgCount) + "'></label>";
    page += "<label>Pre <input id='cfgPre' name='pre' value='" + String(cfgPre) + "'></label>";
    page += "<label>CH <input id='cfgCh' name='ch' value='" + String(cfgChannel) + "'></label>";
    page += "<label>Edge <select id='cfgEdge' name='edge'>";
    page += "<option value='R'";
    if (cfgEdge == 'R') page += " selected";
    page += ">Rising</option>";
    page += "<option value='F'";
    if (cfgEdge == 'F') page += " selected";
    page += ">Falling</option>";
    page += "</select></label>";
    page += "<button type='button' onclick='apiApplyCfg()'>Apply Config</button>";
    page += "<button type='button' onclick='apiResetCfg()'>Reset Config</button>";
    page += "</div>";
    page += "</div>";

    page += "<div class='card'>";
    page += "<h2>Capture</h2>";
    page += "<button type='button' onclick='apiAutoCapture()'>Capture</button>";
    page += "</div>";

    page += "<div class='card'>";
    page += "<h2>Health</h2>";
    page += "<button type='button' onclick='apiRefreshStatus()'>Refresh Status</button>";
    page += "<pre id='healthBox'>No status yet\n\n\n\n\n\n</pre>";
    page += "</div>";
    /*
    page += "<div class='card'>";
    page += "<h2>Last status</h2>";
    page += "<pre>" + lastStatus + "</pre>";
    if (lastError.length() > 0) {
        page += "<h2>Error</h2><pre>" + lastError + "</pre>";
    }
    page += "</div>";
    */
    page += "<div class='card'>";
    page += "<h2>Waveform</h2>";

    page += "<div style='margin-bottom:10px;'>";
    for (int ch = 0; ch < 8; ch++) {
        page += "<label>";
        page += "<input type='checkbox' class='chbox' value='";
        page += String(ch);
        page += "'";
        if (ch == 0) {
            page += " checked";
        }
        page += "> CH";
        page += String(ch);
        page += "</label>";
    }
    page += "</div>";

    page += "<button type='button' onclick='drawSelectedChannels()'>Show selected</button>";
    page += "<button type='button' onclick='selectAllChannels();drawSelectedChannels();'>Show all</button>";
    page += "<button type='button' onclick='selectOnlyCh0();drawSelectedChannels();'>Show CH0 only</button>";
    page += "<br><br>";
    page += "<button type='button' onclick='setZoom(1)'>Zoom x1</button>";
    page += "<button type='button' onclick='setZoom(2)'>Zoom x2</button>";
    page += "<button type='button' onclick='setZoom(4)'>Zoom x4</button>";
    page += "<button type='button' onclick='setZoom(8)'>Zoom x8</button>";
    page += "<button type='button' onclick='panPrev()'>Prev</button>";
    page += "<button type='button' onclick='panNext()'>Next</button>";
    page += "<button type='button' onclick='centerTrigger()'>Center TRG</button>";

    page += "<br><br>";
    page += "<p id='apiStatus'>AJAX status: idle</p>";
    page += "<canvas id='wave' width='1200' height='520'></canvas>";
    page += "<script>";
    page += "let samplesHex='" + samplesHexString(MAX_SAMPLES) + "';";
    page += "let count=" + String(samplesCount) + ";";
    page += "let marker=" + String(cfgPre) + ";";
    page += "let ch=" + String(cfgChannel) + ";";
    page += "let sampleRateHz=" + String(cfgRateHz) + ";";
    page += R"JS(
    const canvas=document.getElementById('wave');
    const ctx=canvas.getContext('2d');
    let zoomX = 1;
    let viewStart = 0;

    function clampViewStart(){
      const vc=getViewCount();

      if(viewStart<0){
        viewStart=0;
      }

      if(viewStart+vc>count){
        viewStart=count-vc;
      }

      if(viewStart<0){
        viewStart=0;
      }
    }

    function getViewCount(){
      if(count<=0)return 0;

      let vc=Math.floor(count/zoomX);
      if(vc<2)vc=2;
      if(vc>count)vc=count;

      return vc;
    }

    function getViewEnd(){
      const vc=getViewCount();
      let end=viewStart+vc;

      if(end>count){
        end=count;
      }

      return end;
    }

    function setZoom(z){
      const oldCenter=viewStart+Math.floor(getViewCount()/2);

      zoomX=z;

      const newCount=getViewCount();
      viewStart=oldCenter-Math.floor(newCount/2);

      clampViewStart();
      drawSelectedChannels();
    }

    function panPrev(){
      const step=Math.floor(getViewCount()/2);

      viewStart-=step;

      clampViewStart();
      drawSelectedChannels();
    }

    function panNext(){
      const step=Math.floor(getViewCount()/2);

      viewStart+=step;

      clampViewStart();
      drawSelectedChannels();
    }

    function centerTrigger(){
      if(marker<0 || marker>=count){
        return;
      }

      const vc=getViewCount();

      viewStart=marker-Math.floor(vc/2);

      clampViewStart();
      drawSelectedChannels();
    }

    function byteAt(i){
      const p=i*2;
      if(p+1>=samplesHex.length)return 0;
      return parseInt(samplesHex.substr(p,2),16);
    }

    function getSelectedChannels(){
      const boxes=document.querySelectorAll('.chbox');
      const channels=[];
      boxes.forEach(b=>{
        if(b.checked){
          channels.push(parseInt(b.value));
        }
      });
      return channels;
    }

    function selectAllChannels(){
      document.querySelectorAll('.chbox').forEach(b=>b.checked=true);
    }

    function selectOnlyCh0(){
      document.querySelectorAll('.chbox').forEach(b=>b.checked=(b.value==='0'));
    }

    function clearWave(){
      ctx.clearRect(0,0,canvas.width,canvas.height);
      ctx.fillStyle='#000';
      ctx.fillRect(0,0,canvas.width,canvas.height);
    }

    function sampleToX(i){
      const x0=70;
      const x1=canvas.width-30;
      const w=x1-x0;

      const viewEnd=getViewEnd();
      const visibleCount=viewEnd-viewStart;

      if(visibleCount<=1){
        return x0;
      }

      return x0+((i-viewStart)*w/(visibleCount-1));
    }

    function formatTimeUs(us){
      if(us>=1000){
        return (us/1000).toFixed(2)+' ms';
      }
      return us.toFixed(0)+' us';
    }

    function drawTimeTicks(y){
      if(count<=1 || sampleRateHz<=0){
        return;
      }

      const viewEnd=getViewEnd();

      ctx.strokeStyle='#555';
      ctx.lineWidth=1;

      const x0=70;
      const x1=canvas.width-30;

      ctx.beginPath();
      ctx.moveTo(x0,y);
      ctx.lineTo(x1,y);
      ctx.stroke();

      ctx.fillStyle='#aaa';
      ctx.font='12px monospace';

      const ticks=5;
      const visibleCount=viewEnd-viewStart;

      for(let k=0;k<=ticks;k++){
        const idx=viewStart+Math.round((visibleCount-1)*k/ticks);
        const x=sampleToX(idx);
        const tUs=idx*1000000.0/sampleRateHz;

        ctx.strokeStyle='#555';
        ctx.beginPath();
        ctx.moveTo(x,y-4);
        ctx.lineTo(x,y+4);
        ctx.stroke();

        ctx.fillStyle='#aaa';
        ctx.fillText(formatTimeUs(tUs),x-16,y+18);
      }
    }

    function drawTimeInfo(y){
      ctx.fillStyle='#aaa';
      ctx.font='13px monospace';

      if(count<=0 || sampleRateHz<=0){
        ctx.fillText('No time info',10,y);
        return;
      }

      const dtUs=1000000.0/sampleRateHz;
      const totalUs=(count>0?(count-1):0)*dtUs;
      const trigUs=marker*dtUs;

      ctx.fillText(
        'rate='+sampleRateHz+' Hz, dt='+dtUs.toFixed(3)+' us, total='+formatTimeUs(totalUs),
        10,
        y
      );

      if(marker>=0 && marker<count){
        ctx.fillStyle='#ff0';
        ctx.fillText('trigger @ '+formatTimeUs(trigUs),10,y+18);
      }
    }

    function drawDigitalChannel(ch, yBase, height){
      const yHigh=yBase;
      const yLow=yBase+height;

      ctx.strokeStyle='#0f0';
      ctx.lineWidth=1.5;
      ctx.beginPath();
      const viewEnd=getViewEnd();

      for(let i=viewStart;i<viewEnd;i++){
        const b=byteAt(i);
        const v=(b>>ch)&1;
        const x=sampleToX(i);
        const y=v?yHigh:yLow;

        if(i===viewStart){
          ctx.moveTo(x,y);
        }else{
          const prevB=byteAt(i-1);
          const prevV=(prevB>>ch)&1;
          const prevY=prevV?yHigh:yLow;

          ctx.lineTo(x,prevY);
          ctx.lineTo(x,y);
        }
      }

      ctx.stroke();

      ctx.fillStyle='#aaa';
      ctx.font='14px monospace';
      ctx.fillText('CH'+ch,10,yBase+height);
    }

    function drawMarker(yTop,yBottom){
      const viewEnd=getViewEnd();

      if(marker>=viewStart && marker<viewEnd){
        const mx=sampleToX(marker);

        ctx.strokeStyle='#ff0';
        ctx.lineWidth=1;
        ctx.beginPath();
        ctx.moveTo(mx,yTop);
        ctx.lineTo(mx,yBottom);
        ctx.stroke();

        ctx.fillStyle='#ff0';
        ctx.font='14px monospace';
        ctx.fillText('TRG',mx+4,yTop+12);
      }
    }

    function drawSelectedChannels(){
      clearWave();

      ctx.font='14px monospace';
      ctx.fillStyle='#aaa';
      const viewEnd=getViewEnd();
      ctx.fillText(
        'samples: '+count+
        ' view: '+viewStart+'..'+(viewEnd-1)+
        ' zoom x'+zoomX+
        ' marker: '+marker,
        10,
        20
      );

      const channels=getSelectedChannels();

      if(count<=0){
        ctx.fillStyle='#f66';
        ctx.fillText('No samples. Press Auto Capture first.',10,50);
        return;
      }

      if(channels.length===0){
        ctx.fillStyle='#f66';
        ctx.fillText('No channels selected.',10,50);
        return;
      }

      const top=45;
      const row=26;
      const amp=12;

      for(let idx=0; idx<channels.length; idx++){
        const ch=channels[idx];
        const y=top+idx*row;
        drawDigitalChannel(ch,y,amp);
      }

      const waveformBottom=top+channels.length*row;

      drawMarker(35,waveformBottom);
      drawTimeTicks(waveformBottom+16);
      drawTimeInfo(waveformBottom+48);
    }

    async function apiAutoCapture(){
      const statusEl=document.getElementById('apiStatus');
      if(statusEl){
        statusEl.textContent='Capturing...';
      }

      try{
        const r=await fetch('/api/auto');
        const j=await r.json();

        if(!j.ok){
          if(statusEl){
            statusEl.textContent='ERROR: '+j.message;
          }
          return;
        }

        samplesHex=j.hex;
        count=j.count;
        marker=j.marker;
        sampleRateHz=j.rate;
        ch=j.channel;

        if(statusEl){
          statusEl.textContent='OK: samples='+count+', rate='+sampleRateHz;
        }

        drawSelectedChannels();
        apiRefreshStatus();
      }catch(e){
        if(statusEl){
          statusEl.textContent='FETCH ERROR: '+e;
        }
      }
    }

    async function apiApplyCfg(){
      const statusEl=document.getElementById('apiStatus');

      const rate=document.getElementById('cfgRate').value;
      const countVal=document.getElementById('cfgCount').value;
      const pre=document.getElementById('cfgPre').value;
      const cfgCh=document.getElementById('cfgCh').value;
      const edge=document.getElementById('cfgEdge').value;

      if(statusEl){
        statusEl.textContent='Applying config...';
      }

      const url=
        '/api/cfg?rate='+encodeURIComponent(rate)+
        '&count='+encodeURIComponent(countVal)+
        '&pre='+encodeURIComponent(pre)+
        '&ch='+encodeURIComponent(cfgCh)+
        '&edge='+encodeURIComponent(edge);

      try{
        const r=await fetch(url);
        const j=await r.json();

        if(!j.ok){
          if(statusEl){
            statusEl.textContent='CFG ERROR: '+j.message;
          }
          return;
        }

        marker=j.pre;
        sampleRateHz=j.rate;
        ch=j.channel;

        if(statusEl){
          statusEl.textContent=
            'CFG OK: rate='+j.rate+
            ', count='+j.count+
            ', pre='+j.pre+
            ', ch='+j.channel+
            ', edge='+j.edge;
        }

        drawSelectedChannels();
      }catch(e){
        if(statusEl){
          statusEl.textContent='CFG FETCH ERROR: '+e;
        }
      }
    }

    async function apiResetCfg(){
      const statusEl=document.getElementById('apiStatus');

      if(statusEl){
        statusEl.textContent='Resetting config...';
      }

      try{
        const r=await fetch('/api/reset_cfg');
        const j=await r.json();

        if(!j.ok){
          if(statusEl){
            statusEl.textContent='RESET ERROR: '+j.message;
          }
          return;
        }

        document.getElementById('cfgRate').value=j.rate;
        document.getElementById('cfgCount').value=j.count;
        document.getElementById('cfgPre').value=j.pre;
        document.getElementById('cfgCh').value=j.channel;
        document.getElementById('cfgEdge').value=j.edge;

        marker=j.pre;
        sampleRateHz=j.rate;
        ch=j.channel;

        if(statusEl){
          statusEl.textContent='RESET OK';
        }

        drawSelectedChannels();
        apiRefreshStatus();

      }catch(e){
        if(statusEl){
          statusEl.textContent='RESET FETCH ERROR: '+e;
        }
      }
    }

    async function apiRefreshStatus(){
      const box=document.getElementById('healthBox');
      const statusEl=document.getElementById('apiStatus');

      if(statusEl){
        statusEl.textContent='Refreshing status...';
      }

      try{
        const r=await fetch('/api/status');
        const j=await r.json();

        let text='';
        text+='ok: '+j.ok+'\n';
        text+='status: '+j.status+'\n';
        text+='samples: '+j.samples+'\n';
        text+='rate: '+j.rate+' Hz\n';
        text+='count: '+j.count+'\n';
        text+='pre: '+j.pre+'\n';
        text+='channel: CH'+j.channel+'\n';
        text+='edge: '+j.edge+'\n';

        if(j.error && j.error.length>0){
          text+='error: '+j.error+'\n';
        }

        if(box){
          box.textContent=text;
        }

        if(statusEl){
          statusEl.textContent='Status refreshed';
        }

      }catch(e){
        if(statusEl){
          statusEl.textContent='STATUS FETCH ERROR';
        }

        if(box){
          box.textContent='STATUS FETCH ERROR: '+e+'\n\n\n\n\n';
        }
      }
    }

    drawSelectedChannels();
    apiRefreshStatus();
    )JS";
    
    page += "</script>";
    page += "</div>";

    page += "<div class='card'>";
    page += "<h2>Data</h2>";
    page += "<p>samplesCount=" + String(samplesCount) + "</p>";
    page += "<p>";
    page += "<a href='/csv'><button type='button'>Download CSV</button></a> ";
    page += "<a href='/vcd'><button type='button'>Download VCD</button></a>";
    page += "</p>";
    page += "</div>";

    page += htmlFooter();

    server.send(200, "text/html", page);
}

static void handlePing()
{
    String resp;
    if (sendCommandWaitPrefix("@PING", "@PONG", 1000, &resp)) {
        lastStatus = resp;
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

static void handleStatus()
{
    String resp;
    if (sendCommandWaitPrefix("@STATUS", "@STATUS", 1000, &resp)) {
        lastStatus = resp;
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

static void handleApiStatus()
{
    String resp;

    if (!sendCommandWaitPrefix("@STATUS", "@STATUS", 1000, &resp)) {
        server.send(
            500,
            "application/json",
            makeStatusJson(false, "", lastError)
        );
        return;
    }

    lastStatus = resp;
    lastError = "";

    server.send(
        200,
        "application/json",
        makeStatusJson(true, resp, "")
    );
}

static void handleShow()
{
    String resp;
    if (sendCommandWaitPrefix("@SHOW", "@CFG", 1000, &resp)) {
        lastStatus = resp;
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

static void handleCfg()
{
    if (server.hasArg("rate")) cfgRateHz = server.arg("rate").toInt();
    if (server.hasArg("count")) cfgCount = server.arg("count").toInt();
    if (server.hasArg("pre")) cfgPre = server.arg("pre").toInt();
    if (server.hasArg("ch")) cfgChannel = server.arg("ch").toInt();
    if (server.hasArg("edge")) {
        String e = server.arg("edge");
        cfgEdge = (e == "F") ? 'F' : 'R';
    }

    if (cfgCount > MAX_SAMPLES) cfgCount = MAX_SAMPLES;
    if (cfgCount < 1) cfgCount = 1;
    if (cfgPre > cfgCount) cfgPre = cfgCount;
    if (cfgChannel > 7) cfgChannel = 7;

    String cmd = "@CFG ";
    cmd += String(cfgRateHz);
    cmd += " ";
    cmd += String(cfgCount);
    cmd += " ";
    cmd += String(cfgPre);
    cmd += " ";
    cmd += String(cfgChannel);
    cmd += " ";
    cmd += cfgEdge;

    String resp;
    if (sendCommandWaitPrefix(cmd, "@OK", 1000, &resp)) {
        saveConfigToFlash();

        lastStatus = "Configured and saved: " + cmd;
        lastError = "";
    }

    server.sendHeader("Location", "/");
    server.send(303);
}

static void handleRun()
{
    String resp;
    if (sendCommandWaitPrefix("@RUN", "@DONE", 5000, &resp)) {
        lastStatus = resp;
        lastError = "";
    }

    server.sendHeader("Location", "/");
    server.send(303);
}

static void handleImm()
{
    String resp;
    if (sendCommandWaitPrefix("@IMM", "@DONE", 5000, &resp)) {
        lastStatus = resp;
        lastError = "";
    }

    server.sendHeader("Location", "/");
    server.send(303);
}

static void handleRead()
{
    if (stm32ReadCapture()) {
        lastStatus = "Read samples: " + String(samplesCount);
        lastError = "";
    }

    server.sendHeader("Location", "/");
    server.send(303);
}

static void handleAuto()
{
    String resp;

    String cmd = "@CFG ";
    cmd += String(cfgRateHz);
    cmd += " ";
    cmd += String(cfgCount);
    cmd += " ";
    cmd += String(cfgPre);
    cmd += " ";
    cmd += String(cfgChannel);
    cmd += " ";
    cmd += cfgEdge;

    if (!sendCommandWaitPrefix(cmd, "@OK", 1000, &resp)) {
        server.sendHeader("Location", "/");
        server.send(303);
        return;
    }

    if (!sendCommandWaitPrefix("@RUN", "@DONE", 5000, &resp)) {
        server.sendHeader("Location", "/");
        server.send(303);
        return;
    }

    if (!stm32ReadCapture()) {
        server.sendHeader("Location", "/");
        server.send(303);
        return;
    }

    lastStatus = "Auto capture OK, samples=" + String(samplesCount);
    lastError = "";

    server.sendHeader("Location", "/");
    server.send(303);
}

static void handleHex()
{
    String out;
    out += "COUNT ";
    out += String(samplesCount);
    out += "\n";
    out += samplesHexString(MAX_SAMPLES);
    out += "\n";

    server.send(200, "text/plain", out);
}

static void handleDataJson()
{
    String json;
    json.reserve(samplesCount * 3 + 128);

    json += "{";
    json += "\"count\":";
    json += String(samplesCount);
    json += ",\"channel\":";
    json += String(cfgChannel);
    json += ",\"marker\":";
    json += String(cfgPre);
    json += ",\"samples\":[";

    for (size_t i = 0; i < samplesCount; i++) {
        if (i > 0) json += ",";
        json += String(samples[i]);
    }

    json += "]}";

    server.send(200, "application/json", json);
}

static void handleApiResetCfg()
{
    cfgRateHz = 1000000;
    cfgCount = 512;
    cfgPre = 128;
    cfgChannel = 0;
    cfgEdge = 'R';

    saveConfigToFlash();

    String cmd = "@CFG ";
    cmd += String(cfgRateHz);
    cmd += " ";
    cmd += String(cfgCount);
    cmd += " ";
    cmd += String(cfgPre);
    cmd += " ";
    cmd += String(cfgChannel);
    cmd += " ";
    cmd += cfgEdge;

    String resp;

    if (!sendCommandWaitPrefix(cmd, "@OK", 1000, &resp)) {
        server.send(
            500,
            "application/json",
            makeCfgJson(false, "reset saved but STM32 CFG failed")
        );
        return;
    }

    server.send(
        200,
        "application/json",
        makeCfgJson(true, "reset to defaults")
    );
}

static String makeSamplesJson(bool ok, const String &message)
{
    String json;
    json.reserve(samplesCount * 2 + 256);

    json += "{";
    json += "\"ok\":";
    json += ok ? "true" : "false";

    json += ",\"message\":\"";
    json += message;
    json += "\"";

    json += ",\"count\":";
    json += String(samplesCount);

    json += ",\"marker\":";
    json += String(cfgPre);

    json += ",\"rate\":";
    json += String(cfgRateHz);

    json += ",\"channel\":";
    json += String(cfgChannel);

    json += ",\"hex\":\"";
    json += samplesHexString(MAX_SAMPLES);
    json += "\"";

    json += "}";

    return json;
}

static String makeCfgJson(bool ok, const String &message)
{
    String json;

    json += "{";
    json += "\"ok\":";
    json += ok ? "true" : "false";

    json += ",\"message\":\"";
    json += message;
    json += "\"";

    json += ",\"rate\":";
    json += String(cfgRateHz);

    json += ",\"count\":";
    json += String(cfgCount);

    json += ",\"pre\":";
    json += String(cfgPre);

    json += ",\"channel\":";
    json += String(cfgChannel);

    json += ",\"edge\":\"";
    json += cfgEdge;
    json += "\"";

    json += "}";

    return json;
}

static String makeStatusJson(
    bool ok,
    const String &statusLine,
    const String &errorText
)
{
    String json;

    json += "{";
    json += "\"ok\":";
    json += ok ? "true" : "false";

    json += ",\"status\":\"";
    json += statusLine;
    json += "\"";

    json += ",\"samples\":";
    json += String(samplesCount);

    json += ",\"rate\":";
    json += String(cfgRateHz);

    json += ",\"count\":";
    json += String(cfgCount);

    json += ",\"pre\":";
    json += String(cfgPre);

    json += ",\"channel\":";
    json += String(cfgChannel);

    json += ",\"edge\":\"";
    json += cfgEdge;
    json += "\"";

    json += ",\"error\":\"";
    json += errorText;
    json += "\"";

    json += "}";

    return json;
}

static void handleApiAuto()
{
    String resp;

    String cmd = "@CFG ";
    cmd += String(cfgRateHz);
    cmd += " ";
    cmd += String(cfgCount);
    cmd += " ";
    cmd += String(cfgPre);
    cmd += " ";
    cmd += String(cfgChannel);
    cmd += " ";
    cmd += cfgEdge;

    if (!sendCommandWaitPrefix(cmd, "@OK", 1000, &resp)) {
        server.send(500, "application/json", makeSamplesJson(false, "CFG failed: " + lastError));
        return;
    }

    if (!sendCommandWaitPrefix("@RUN", "@DONE", 5000, &resp)) {
        server.send(500, "application/json", makeSamplesJson(false, "RUN failed: " + lastError));
        return;
    }

    if (!stm32ReadCapture()) {
        server.send(500, "application/json", makeSamplesJson(false, "READ failed: " + lastError));
        return;
    }

    lastStatus = "API auto capture OK, samples=" + String(samplesCount);
    lastError = "";

    server.send(200, "application/json", makeSamplesJson(true, "OK"));
}

static void handleCsv()
{
    String csv;

    csv.reserve(samplesCount * 40 + 128);

    csv += "index,time_us,sample_hex,ch0,ch1,ch2,ch3,ch4,ch5,ch6,ch7\r\n";

    double dtUs = 0.0;

    if (cfgRateHz > 0) {
        dtUs = 1000000.0 / (double)cfgRateHz;
    }

    char line[160];

    for (size_t i = 0; i < samplesCount; i++) {
        uint8_t s = samples[i];
        double tUs = (double)i * dtUs;

        snprintf(
            line,
            sizeof(line),
            "%u,%.3f,%02X,%u,%u,%u,%u,%u,%u,%u,%u\r\n",
            (unsigned)i,
            tUs,
            s,
            (s >> 0) & 1,
            (s >> 1) & 1,
            (s >> 2) & 1,
            (s >> 3) & 1,
            (s >> 4) & 1,
            (s >> 5) & 1,
            (s >> 6) & 1,
            (s >> 7) & 1
        );

        csv += line;
    }

    server.sendHeader(
        "Content-Disposition",
        "attachment; filename=capture.csv"
    );

    server.send(200, "text/csv", csv);
}

static void handleApiCfg()
{
    if (server.hasArg("rate")) {
        cfgRateHz = server.arg("rate").toInt();
    }

    if (server.hasArg("count")) {
        cfgCount = server.arg("count").toInt();
    }

    if (server.hasArg("pre")) {
        cfgPre = server.arg("pre").toInt();
    }

    if (server.hasArg("ch")) {
        cfgChannel = server.arg("ch").toInt();
    }

    if (server.hasArg("edge")) {
        String e = server.arg("edge");
        cfgEdge = (e == "F") ? 'F' : 'R';
    }

    if (cfgRateHz < 1) {
        cfgRateHz = 1;
    }

    if (cfgCount > MAX_SAMPLES) {
        cfgCount = MAX_SAMPLES;
    }

    if (cfgCount < 1) {
        cfgCount = 1;
    }

    if (cfgPre > cfgCount) {
        cfgPre = cfgCount;
    }

    if (cfgChannel > 7) {
        cfgChannel = 7;
    }

    String cmd = "@CFG ";
    cmd += String(cfgRateHz);
    cmd += " ";
    cmd += String(cfgCount);
    cmd += " ";
    cmd += String(cfgPre);
    cmd += " ";
    cmd += String(cfgChannel);
    cmd += " ";
    cmd += cfgEdge;

    String resp;

    if (!sendCommandWaitPrefix(cmd, "@OK", 1000, &resp)) {
        server.send(
            500,
            "application/json",
            makeCfgJson(false, "CFG failed: " + lastError)
        );
        return;
    }

    saveConfigToFlash();

    lastStatus = "API configured: " + cmd;
    lastError = "";

    server.send(
        200,
        "application/json",
        makeCfgJson(true, "configured and saved")
    );
}

static void handleVcd()
{
    String vcd;

    /*
     * VCD может быть крупнее CSV.
     * Для 512..1024 samples нормально.
     */
    vcd.reserve(samplesCount * 80 + 1024);

    double dtUs = 1.0;

    if (cfgRateHz > 0) {
        dtUs = 1000000.0 / (double)cfgRateHz;
    }

    /*
     * Для простоты используем timescale 1 us.
     * Если sample rate = 1 MHz, один sample = 1 us.
     * Если sample rate выше/ниже, timestamp округляем до целых us.
     */
    vcd += "$date\n";
    vcd += "  generated by ESP32 FPGA Logic Analyzer\n";
    vcd += "$end\n";

    vcd += "$version\n";
    vcd += "  FPGA Logic Analyzer web export\n";
    vcd += "$end\n";

    vcd += "$timescale 1 us $end\n";

    vcd += "$scope module logic_analyzer $end\n";

    /*
     * VCD identifiers.
     * Берём простые printable chars.
     */
    const char *ids[8] = {
        "!", "\"", "#", "$", "%", "&", "'", "("
    };

    for (int ch = 0; ch < 8; ch++) {
        vcd += "$var wire 1 ";
        vcd += ids[ch];
        vcd += " ch";
        vcd += String(ch);
        vcd += " $end\n";
    }

    vcd += "$upscope $end\n";
    vcd += "$enddefinitions $end\n";

    /*
     * Initial values at #0.
     */
    vcd += "#0\n";

    if (samplesCount > 0) {
        uint8_t s0 = samples[0];

        for (int ch = 0; ch < 8; ch++) {
            vcd += ((s0 >> ch) & 1) ? "1" : "0";
            vcd += ids[ch];
            vcd += "\n";
        }
    } else {
        for (int ch = 0; ch < 8; ch++) {
            vcd += "x";
            vcd += ids[ch];
            vcd += "\n";
        }
    }

    /*
     * Dump only changes to keep file smaller.
     */
    uint8_t prev = samplesCount > 0 ? samples[0] : 0;

    for (size_t i = 1; i < samplesCount; i++) {
        uint8_t cur = samples[i];

        if (cur == prev) {
            continue;
        }

        uint32_t tUs = (uint32_t)((double)i * dtUs + 0.5);

        vcd += "#";
        vcd += String(tUs);
        vcd += "\n";

        uint8_t changed = cur ^ prev;

        for (int ch = 0; ch < 8; ch++) {
            if (changed & (1u << ch)) {
                vcd += ((cur >> ch) & 1) ? "1" : "0";
                vcd += ids[ch];
                vcd += "\n";
            }
        }

        prev = cur;
    }

    server.sendHeader(
        "Content-Disposition",
        "attachment; filename=capture.vcd"
    );

    server.send(200, "text/plain", vcd);
}
// ------------------------------------------------------------
// USB debug console
// ------------------------------------------------------------
static String readUsbCommand()
{
    static String cmd;

    while (Serial.available()) {
        char c = (char)Serial.read();

        if (c == '\r' || c == '\n') {
            if (cmd.length() > 0) {
                String out = cmd;
                cmd = "";
                return out;
            }
        } else if (c == 8 || c == 127) {
            if (cmd.length() > 0) {
                cmd.remove(cmd.length() - 1);
            }
        } else {
            cmd += c;
        }
    }

    return "";
}

static void printHelp()
{
    Serial.println();
    Serial.println("ESP32-C6 Web UI v1");
    Serial.println("AP SSID: FPGA-LA");
    Serial.println("AP PASS: 12345678");
    Serial.println("Open: http://192.168.4.1");
    Serial.println();
    Serial.println("USB commands:");
    Serial.println("  h/help/?");
    Serial.println("  p      -> @PING");
    Serial.println("  s      -> @STATUS");
    Serial.println("  show   -> @SHOW");
    Serial.println("  cfg    -> default @CFG");
    Serial.println("  run    -> @RUN");
    Serial.println("  imm    -> @IMM");
    Serial.println("  read   -> @READ + parse");
    Serial.println("  auto   -> cfg + run + read");
    Serial.println();
}

static void handleUsbCommand(const String &cmdIn)
{
    String cmd = cmdIn;
    cmd.trim();

    if (cmd == "h" || cmd == "help" || cmd == "?") {
        printHelp();
    }
    else if (cmd == "p") {
        String r;
        sendCommandWaitPrefix("@PING", "@PONG", 1000, &r);
    }
    else if (cmd == "s") {
        String r;
        sendCommandWaitPrefix("@STATUS", "@STATUS", 1000, &r);
    }
    else if (cmd == "show") {
        String r;
        sendCommandWaitPrefix("@SHOW", "@CFG", 1000, &r);
    }
    else if (cmd == "cfg") {
        String r;
        sendCommandWaitPrefix("@CFG 1000000 512 128 0 R", "@OK", 1000, &r);
    }
    else if (cmd == "run") {
        String r;
        sendCommandWaitPrefix("@RUN", "@DONE", 5000, &r);
    }
    else if (cmd == "imm") {
        String r;
        sendCommandWaitPrefix("@IMM", "@DONE", 5000, &r);
    }
    else if (cmd == "read") {
        if (stm32ReadCapture()) {
            Serial.print("Parsed samples: ");
            Serial.println(samplesCount);
        } else {
            Serial.print("Read failed: ");
            Serial.println(lastError);
        }
    }
    else if (cmd == "auto") {
        handleAuto();
    }
    else if (cmd.startsWith("@")) {
        sendToStm32(cmd);
    }
    else {
        Serial.println("Unknown command. Type help.");
    }
}

// ------------------------------------------------------------
// Arduino setup / loop
// ------------------------------------------------------------
void setup()
{
    Serial.begin(USB_BAUD);
    delay(1000);

    Serial.println();
    Serial.println("ESP32-C6 Web UI v1 started");

    loadConfigFromFlash();

    Stm32Serial.begin(
        STM32_BAUD,
        SERIAL_8N1,
        STM32_UART_RX_PIN,
        STM32_UART_TX_PIN
    );

    WiFi.mode(WIFI_AP);
    bool apOk = WiFi.softAP(AP_SSID, AP_PASS);

    if (apOk) {
        Serial.print("AP started: ");
        Serial.println(AP_SSID);
        Serial.print("AP IP: ");
        Serial.println(WiFi.softAPIP());
    } else {
        Serial.println("AP start FAILED");
    }

    server.on("/", handleRoot);
    server.on("/ping", handlePing);
    server.on("/status", handleStatus);
    server.on("/api/status", handleApiStatus);
    server.on("/show", handleShow);
    server.on("/cfg", handleCfg);
    server.on("/api/cfg", handleApiCfg);
    server.on("/run", handleRun);
    server.on("/imm", handleImm);
    server.on("/read", handleRead);
    server.on("/auto", handleAuto);
    server.on("/api/auto", handleApiAuto);
    server.on("/hex", handleHex);
    server.on("/data", handleDataJson);
    server.on("/csv", handleCsv);
    server.on("/vcd", handleVcd);
    server.on("/api/reset_cfg", handleApiResetCfg);

    server.begin();
    Serial.println("HTTP server started");

    printHelp();

    String resp;
    sendCommandWaitPrefix("@PING", "@PONG", 1000, &resp);
}

void loop()
{
    server.handleClient();

    String cmd = readUsbCommand();

    if (cmd.length() > 0) {
        handleUsbCommand(cmd);
    }
}