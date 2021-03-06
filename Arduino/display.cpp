#include "display.h"
#include "Nextion.h"
#include "HVAC.h"
#include <TimeLib.h>
#include <ESP8266mDNS.h> // for WiFi.RSSI()
#include <Event.h>

Nextion nex;
extern HVAC hvac;
extern eventHandler event;

void Display::init()
{
  nex.FFF(); // Just to end any debug strings in the Nextion
  screen( true ); // brighten the screen if it just reset
  refreshAll();
  nex.itemPic(9, hvac.m_EE.bLock ? 20:21);
}

// called each second
void Display::oneSec()
{
  updateClock();
  updateRunIndicator(false); // running stuff
  displayTime();    // time update every seconds
  updateModes();    // mode, heat mode, fan mode
  updateTemps();    // 
  updateAdjMode(false); // update touched temp settings
  updateNotification(false);
  updateRSSI();     //
  if( m_backlightTimer ) // the dimmer thing
  {
    if(--m_backlightTimer == 0)
        screen(false);
  }

  static uint8_t lastState;
  if(--m_temp_counter <= 0 || hvac.getState() != lastState)
  {
    displayOutTemp();
    addGraphPoints();
    lastState = hvac.getState();
    m_temp_counter = 5*60;         // update every 5 minutes
  }
}

void Display::checkNextion() // all the Nextion recieved commands
{
  char cBuf[64];
  int len = nex.service(cBuf); // returns just the button value or 0 if no data
  uint8_t btn;
  String s;
  static uint8_t textIdx = 0;

  Lines(); // draw lines at full speed

  if(len == 0)
    return;

  switch(cBuf[0])  // code
  {
    case 0x65: // button
      btn = cBuf[2];
      nex.brightness(NEX_BRIGHT);
 
      switch(cBuf[1]) // page
      {
        case Page_Thermostat:
          m_backlightTimer = NEX_TIMEOUT;
          switch(btn)
          {
            case 6: // cool hi
            case 7: // cool lo
            case 8: // heat hi
            case 9: // heat lo
              m_adjustMode = btn-6;
              break;
            case 15: // cool hi
            case 16: // cool lo
            case 17: // heat hi
            case 18: // heat lo
              m_adjustMode = btn-15;
              break;

            case 22: // fan
              if(hvac.m_EE.bLock) break;
              hvac.setFan( !hvac.getFan() );
              updateModes(); // faster feedback
              break;
            case 23: // Mode
              if(hvac.m_EE.bLock) break;
              hvac.setMode( (hvac.getSetMode() + 1) & 3 );
              updateModes(); // faster feedback
              break;
            case 24: // Heat
              if(hvac.m_EE.bLock) break;
              hvac.setHeatMode( (hvac.getHeatMode() + 1) % 3 );
              updateModes(); // faster feedback
              break;
            case 10: // notification clear
              if(hvac.m_EE.bLock) break;
              hvac.m_notif = Note_None;
              break;
            case 11: // forecast
              nex.setPage("graph");
              fillGraph();
              break;
            case 2: // time
              nex.setPage("clock");
              delay(10); // 20 works
              updateClock();
              break;
            case 12: // DOW
              if(hvac.m_EE.bLock) break;
              textIdx = 0;
              nex.setPage("keyboard"); // go to keyboard
              nex.itemText(1, "Enter Zipcode");
              break;
            case 13: // temp scale
              if(hvac.m_EE.bLock) break;
              textIdx = 1;
              nex.setPage("keyboard"); // go to keyboard
              nex.itemText(1, "Enter Password");
              break;
            case 5:  // target temp
            case 19:
              if(hvac.m_EE.bLock) break;
              hvac.enableRemote();
              break;
            case 1: // out
              break;
            case 3: // in
            case 4: // rh
              hvac.m_bLocalTempDisplay = !hvac.m_bLocalTempDisplay; // toggle remote temp display
              updateTemps();
              break;
            case 21: // humidifier indicator
              break;
            case 25: // lock
//#define PWLOCK  // uncomment for password entry unlock
#ifdef PWLOCK
              if(hvac.m_EE.bLock)
              {
                textIdx = 2;
                nex.itemText(0, ""); // clear last text
                nex.setPage("keyboard"); // go to keyboard
                nex.itemText(1, "Enter Password");
              }
              else
                hvac.m_EE.bLock = true;
#else
              hvac.m_EE.bLock = !hvac.m_EE.bLock;
#endif
              nex.itemPic(9, hvac.m_EE.bLock ? 20:21);
              break;
          }
          break;
        default: // all pages go back
          screen(true);
          break;
      }
      break;
    case 0x70:// string return from keyboard
      switch(textIdx)
      {
        case 0: // zipcode edit
          if(strlen(cBuf + 1) < 5)
            break;
          strncpy(hvac.m_EE.zipCode, cBuf + 1, sizeof(hvac.m_EE.zipCode));
          break;
        case 1: // password edit
          if(strlen(cBuf + 1) < 5)
            return;
          strncpy(hvac.m_EE.password, cBuf + 1, sizeof(hvac.m_EE.password) );
          break;
        case 2: // password unlock
          if(!strcmp(hvac.m_EE.password, cBuf + 1) )
            hvac.m_EE.bLock = false;
          nex.itemText(0, ""); // clear password
          nex.itemPic(9, hvac.m_EE.bLock ? 20:21);
          break;
      }
      screen(true); // back to main page
      break;
  }
}

void Display::updateTemps()
{
  static uint16_t last[7];  // only draw changes
  static bool bRmt = false;

  if(nex.getPage())
  {
    memset(last, 0, sizeof(last));
    bRmt = false;
    return;
  }

  if(bRmt != hvac.showLocalTemp())
  {
    bRmt = hvac.showLocalTemp();
    nex.itemColor("f2", bRmt ? rgb16(31, 0, 15) : rgb16(0, 63, 31));
    nex.itemColor("f3", bRmt ? rgb16(31, 0, 15) : rgb16(0, 63, 31));
  }

  uint16_t temp = hvac.showLocalTemp() ? hvac.m_localTemp : hvac.m_inTemp;
  uint16_t rh = hvac.showLocalTemp() ? hvac.m_localRh : hvac.m_rh;

  if(last[0] != temp)                   nex.itemFp(2, last[0] = temp);
  if(last[1] != rh)                     nex.itemFp(3, last[1] = rh);
  if(last[2] != hvac.m_targetTemp)      nex.itemFp(4, last[2] = hvac.m_targetTemp);
  if(last[3] != hvac.m_EE.coolTemp[1])  nex.itemFp(5, last[3] = hvac.m_EE.coolTemp[1]);
  if(last[4] != hvac.m_EE.coolTemp[0])  nex.itemFp(6, last[4] = hvac.m_EE.coolTemp[0]);
  if(last[5] != hvac.m_EE.heatTemp[1])  nex.itemFp(7, last[5] = hvac.m_EE.heatTemp[1]);
  if(last[6] != hvac.m_EE.heatTemp[0])  nex.itemFp(8, last[6] = hvac.m_EE.heatTemp[0]);
}

const char *_days_short[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// time and dow on main page
void Display::displayTime()
{
  static int8_t lastDay = -1;

  if(nex.getPage())
  {
    lastDay = -1;
    return;  // t7 and t8 are only on thermostat (for now)
  }

  String sTime = String( hourFormat12() );
  sTime += ":";
  if(minute() < 10) sTime += "0";
  sTime += minute();
  sTime += ":";
  if(second() < 10) sTime += "0";
  sTime += second();
  sTime += " ";
  sTime += isPM() ? "PM":"AM";

  nex.itemText(8, sTime);

  if(weekday() != lastDay)   // update weekday
  {
    lastDay = weekday();
    nex.itemText(7, _days_short[weekday()-1]);
  }
}

#define Fc_Left     22
#define Fc_Top      29
#define Fc_Width   196
#define Fc_Height   65

void Display::drawForecast(bool bRef)
{
  int i;

  if(hvac.m_fcData[1].h == -1) // no data yet
    return;

  if(bRef)
  {
    if(hvac.m_fcData[0].h == -1) // first time only
    {
      hvac.m_fcData[0].h = hvac.m_fcData[1].h;
      hvac.m_fcData[0].t = hvac.m_fcData[1].t;
    }

    int8_t hrs = ( (hvac.m_fcData[1].h - hour() + 1) % 3 ) + 1;   // Set interval to 2, 5, 8, 11..20,23
    int8_t mins = (60 - minute() + 53) % 60;   // mins to :52, retry will be :57
  
    if(hrs < 0 || hrs > 3) // something's wrong (could be local time)
      hrs = 0;
  
    m_updateFcst = ((hrs * 60) + mins);
  }

  if(nex.getPage()) // on different page
    return;

  if(bRef) // new forecast
  {
    m_temp_counter = 2; // Todo: just for first point
    nex.refreshItem("t19");
    nex.refreshItem("t20");
    nex.refreshItem("s0");
    delay(5);
  }

  int8_t tmin = hvac.m_outMin[0];
  int8_t tmax = hvac.m_outMax[0];
  int16_t y = Fc_Top+1;
  int16_t incy = (Fc_Height-4) / 3;
  int16_t dec = (tmax - tmin)/3;
  int16_t t = tmax;
  int16_t x;

  // temp scale
  for(i = 0; i <= 3; i++)
  {
    nex.text(3, y-6, 0, rgb16(0, 31, 31), String(t)); // font height/2=6?
    y += incy;
    t -= dec;
  }

  int8_t day = weekday()-1;              // current day
  int8_t h0 = hour();                    // zeroeth hour
  int8_t pts = hvac.m_fcData[18].h - h0; // normally 52 hours
  int8_t h;
  int16_t day_x = 0;

  if(pts <= 0) return;                     // error

  for(i = 0, h = h0; i < pts; i++, h++)    // v-lines
  {
    x = Fc_Left + Fc_Width * i / pts;
    if( (h % 24) == 0) // midnight
    {
      nex.line(x, Fc_Top+1, x, Fc_Top+Fc_Height-2, rgb16(23, 47, 23) ); // (light gray)
      if(x - 49 > Fc_Left) // fix 1st day too far left
      {
        nex.text(day_x = x - 54, Fc_Top+Fc_Height+1, 1, rgb16(0, 63, 31), _days_short[day]);
      }
      if(++day > 6) day = 0;
    }
    if( (h % 24) == 12) // noon
    {
      nex.line(x, Fc_Top, x, Fc_Top+Fc_Height, rgb16(15, 31, 15) ); // gray
    }
  }

  day_x += 84;
  if(day_x < Fc_Left+Fc_Width - (8*3) )  // last partial day
    nex.text(day_x, Fc_Top+Fc_Height+1, 1, rgb16(0, 63, 31), _days_short[day]);

  int16_t y2 = Fc_Top+Fc_Height - 1 - (hvac.m_fcData[1].t - tmin) * (Fc_Height-2) / (tmax-tmin);
  int16_t x2 = Fc_Left;

  for(i = 1; i <= 18; i++) // should be 18 data points
  {
    int y1 = Fc_Top+Fc_Height - 1 - (hvac.m_fcData[i].t - tmin) * (Fc_Height-2) / (tmax-tmin);
    int x1 = Fc_Left + (hvac.m_fcData[i].h - h0) * (Fc_Width-1) / pts;
    if(x2 < Fc_Left) x2 = Fc_Left;  // first point may be history
    if(x1 < Fc_Left) x1 = x2;  // todo: fix this
    nex.line(x2, y2, x1, y1, rgb16(31, 0, 0) ); // red
    x2 = x1;
    y2 = y1;
  }
  displayOutTemp();
}

// get value at current minute between hours
int Display::tween(int8_t t1, int8_t t2, int m, int8_t h)
{
  if(h == 0) h = 1; // div by zero check
  double t = (double)(t2 - t1) * (m * 100 / (60 * h)) / 100;
  return (int)((t + t1) * 10);
}

void Display::displayOutTemp()
{
  if(hvac.m_fcData[1].h == -1) // no read yet
    return;
  
  int8_t hd = hour() - hvac.m_fcData[1].h;      // hours past 1st value
  int16_t outTempDelayed;
  int16_t outTempReal;

  if(hd < 0)                                    // 1st value is top of next hour
  {
     outTempReal = hvac.m_fcData[1].t * 10;         // just use it
     outTempDelayed = hvac.m_fcData[0].t * 10;
  }
  else
  {
     int m = minute();              // offset = hours past + minutes of hour

     if(hd) m += (hd * 60);              // add hours ahead (up to 2)
     outTempReal = tween(hvac.m_fcData[1].t, hvac.m_fcData[2].t, m, hvac.m_fcData[2].h - hvac.m_fcData[1].h);
     outTempDelayed = tween(hvac.m_fcData[0].t, hvac.m_fcData[1].t, m, hvac.m_fcData[1].h - hvac.m_fcData[0].h);
  }
  if(nex.getPage() == Page_Thermostat)
    nex.itemFp(1, outTempReal);

  hvac.updateOutdoorTemp(outTempDelayed);
}

void Display::Note(char *cNote)
{
  screen(true);
  nex.itemText(12, cNote);
  event.alert(cNote);
}

// update the notification text box
void Display::updateNotification(bool bRef)
{
  static uint8_t note_last = Note_Init; // Needs a clear after startup
  if(!bRef && note_last == hvac.m_notif) // nothing changed
    return;
  note_last = hvac.m_notif;

  String s = "";
  switch(hvac.m_notif)
  {
    case Note_None:
      break;
    case Note_CycleLimit:
      s = "Cycle Limit";
      break;
    case Note_Filter:
      s = "Replace Filter";
      break;
    case Note_Forecast:
      s = "Forecast Error"; // max chars 14 with this font
      break;
    case Note_Network:
      s = "Network Error";
      break;
    case Note_RemoteOff:
      s = "Remote Off";
      break;
    case Note_RemoteOn:
      s = "Remote On";
      break;
  }
  nex.itemText(12, s);
  if(s != "" && bRef == false) // refresh shouldn't be resent
    event.alert(s);
}

// true: set screen backlight to bright plus switch to thermostat
// false: cycle to next screensaver
bool Display::screen(bool bOn)
{
  static bool bOldOn = true;

  if(bOldOn && nex.getPage()) // not in sync
    bOldOn = false;

  if(bOn) // input or other reason
  {
    nex.brightness(NEX_BRIGHT);
  }
  if(bOn == false && nex.getPage() == Page_Graph) // last sequence was graph
    bOn = true;

  m_backlightTimer = NEX_TIMEOUT; // update the auto backlight timer
  if(bOn)
  {
    if( bOn == bOldOn )
      return false; // no change occurred
    nex.setPage("Thermostat");
    delay(25); // 20 works most of the time
    refreshAll();
  }
  else switch(nex.getPage())
  {
    case Page_Clock: // already clock
      randomSeed(analogRead(0)+micros());
      nex.setPage("blank"); // lines
      break;
    case Page_Blank: // lines
      nex.setPage("graph"); // chart thing
      fillGraph();
      break;
    default:  // probably thermostat
      nex.setPage("clock"); // clock
      delay(20);
      updateClock();
      nex.brightness(NEX_DIM);
      break;
  }
  bOldOn = bOn;
  return true; // it was changed
}

// things to update on page change to thermostat
void Display::refreshAll()
{
  updateRunIndicator(true);
  drawForecast(false);
  updateNotification(true);
  updateAdjMode(true);
}

// Analog clock
void Display::updateClock()
{
  if(nex.getPage() != Page_Clock)
    return;

  nex.refreshItem("cl"); // erases lines
  delay(8); // 8 works, 5 does not
  const float x = 159; // center
  const float y = 120;

  float size = 80;
  float angle = minute() * 6;
  float ang =  M_PI * (180-angle) / 180;
  float x2 = x + size * sin(ang);
  float y2 = y + size * cos(ang);
  nex.line(x, y, x2, y2, rgb16(0, 0, 31) ); // (blue) minute

  size = 64;
  angle = (hour() + (minute() * 0.00833) ) * 30;
  ang =  M_PI * (180-angle) / 180;
  x2 = x + size * sin(ang);
  y2 = y + size * cos(ang);
  nex.line(x, y, x2, y2, rgb16(0, 63, 31) ); // (cyan) hour

  size = 91;
  angle = second() * 6;
  ang =  M_PI * (180-angle) / 180;
  x2 = x + size * sin(ang);
  y2 = y + size * cos(ang);
  ang =  M_PI * (0-angle) / 180;
  size = 24;
  float x3 = x + size * sin(ang);
  float y3 = y + size * cos(ang);

  nex.line(x3, y3, x2, y2, rgb16(31, 0, 0) ); // (red) second
}

void Display::updateModes() // update any displayed settings
{
  const char *sFan[] = {"Auto", "On"};
  const char *sModes[] = {"Off", "Cool", "Heat", "Auto"};
  const char *sHeatModes[] = {"HP", "NG", "Auto"};
  static bool bFan = true; // set these to something other than default to trigger them all
  static uint8_t nMode = 10;
  static uint8_t heatMode = 10;

  if(nex.getPage())
  {
    nMode = heatMode = 10;
    return;
  }

  if(bFan != hvac.getFan() && nex.getPage() == Page_Thermostat)
  {
    int idx = 10; // not running
    if( bFan = hvac.getFanRunning() )
    {
      idx = 11; // running
      if(hvac.getFan())
        idx = 12; // on and running
    }
    17, 
    nex.itemPic(5, idx);
  }

  if(nMode != hvac.getSetMode())
  {
    nMode = hvac.getSetMode();
    nex.itemPic(7, nMode + 13);
  }

  if(heatMode != hvac.getHeatMode())
  {
    heatMode = hvac.getHeatMode();
    nex.itemPic(8, heatMode + 17);
  }
}

void Display::updateAdjMode(bool bRef)  // current adjust indicator of the 4 temp settings
{
  static uint8_t am = 0;

  if(nex.getPage() || (bRef == false && am == m_adjustMode) )
    return;
  nex.visible("p" + String(am), 0);
  am = m_adjustMode;
  nex.visible("p" + String(am), 1);
}

void Display::updateRSSI()
{
  static uint8_t seccnt = 2;
  static int16_t rssiT;
#define RSSI_CNT 8
  static int16_t rssi[RSSI_CNT];
  static uint8_t rssiIdx = 0;

  if(nex.getPage()) // must be page 0
  {
    rssiT = 0; // cause a refresh later
    seccnt = 1;
    return;
  }
  if(--seccnt)
    return;
  seccnt = 3;     // every 3 seconds

  rssi[rssiIdx] = WiFi.RSSI();
  if(++rssiIdx >= RSSI_CNT) rssiIdx = 0;

  int16_t rssiAvg = 0;
  for(int i = 0; i < RSSI_CNT; i++)
    rssiAvg += rssi[i];

  rssiAvg /= RSSI_CNT;
  if(rssiAvg == rssiT)
    return;

  nex.itemText(22, String(rssiT = rssiAvg) + "dB");

  int sigStrength = 127 + rssiT;
  int wh = 24; // width and height
  int x = 178;
  int y = 172;
  int sect = 127 / 5; //
  int dist = wh  / 5; // distance between blocks

  y += wh;

  for (int i = 1; i < 6; i++)
  {
    nex.fill( x + i*dist, y - i*dist, dist-2, i*dist, (sigStrength > i * sect) ? rgb16(0, 63,31) : rgb16(5, 10, 5) );
  }
}

void Display::updateRunIndicator(bool bForce) // run and fan running
{
  static bool bOn = false; // blinker
  static bool bCurrent = false; // run indicator
  static bool bPic = false; // red/blue
  static bool bHumid = false; // next to rH

  if(bForce)
  {
    bOn = false;
    bCurrent = false;
    bPic = false;
    bHumid = false;
  }

  if(hvac.getState()) // running
  {
    if(bPic != (hvac.getState() > State_Cool) ? true:false)
    {
      bPic = (hvac.getState() > State_Cool) ? true:false;
      nex.itemPic(4, bPic ? 3:1); // red or blue indicator
    }
    if(hvac.m_bRemoteStream)
      bOn = !bOn; // blink indicator if remote temp
    else bOn = true; // just on
  }
  else bOn = false;

  if(bCurrent != bOn && nex.getPage() == Page_Thermostat)
    nex.visible("p4", (bCurrent = bOn) ? 1:0); // blinking run indicator

  if(bHumid != hvac.getHumidifierRunning() && nex.getPage() == Page_Thermostat)
    nex.visible("p6", (bHumid = hvac.getHumidifierRunning()) ? 1:0); // blinking run indicator
}

// Lines demo
void Display::Lines()
{
  if(nex.getPage() != Page_Blank)
    return;

#define LINES 25
  static Line line[LINES], delta;
  uint16_t color;
  static uint8_t r=0, g=0, b=0;
  static int8_t cnt = 0;
  static bool bInit = false;

  if(!bInit)
  {
    memset(&line, 10, sizeof(line));
    memset(&delta, 1, sizeof(delta));
    bInit = true;
  }
  // Erase oldest line
  nex.line(line[LINES - 1].x1, line[LINES - 1].y1, line[LINES - 1].x2, line[LINES - 1].y2, 0);

  // FIFO the lines
  for(int i = LINES - 2; i >= 0; i--)
    line[i+1] = line[i];

  if(--cnt <= 0)
  {
    cnt = 5; // every 5 runs
    delta.x1 = constrain(delta.x1 + random(-1,2), -4, 4); // random direction delta
    delta.x2 = constrain(delta.x2 + random(-1,2), -4, 4);
    delta.y1 = constrain(delta.y1 + random(-1,2), -4, 4);
    delta.y2 = constrain(delta.y2 + random(-1,2), -4, 4);
  }
  line[0].x1 += delta.x1; // add delta to positions
  line[0].y1 += delta.y1;
  line[0].x2 += delta.x2;
  line[0].y2 += delta.y2;

  line[0].x1 = constrain(line[0].x1, 0, 320); // keep it on the screen
  line[0].x2 = constrain(line[0].x2, 0, 320);
  line[0].y1 = constrain(line[0].y1, 0, 240);
  line[0].y2 = constrain(line[0].y2, 0, 240);

  b += random(-2, 3); // random RGB shift
  g += random(-3, 4); // green is 6 bits
  r += random(-2, 3);

  color = rgb(r,g,b);
  
  nex.line(line[0].x1, line[0].y1, line[0].x2, line[0].y2, color); // draw the new line

  if(line[0].x1 == 0 && delta.x1 < 0) delta.x1 = -delta.x1; // bounce off edges
  if(line[0].x2 == 0 && delta.x2 < 0) delta.x2 = -delta.x2;
  if(line[0].y1 == 0 && delta.y1 < 0) delta.y1 = -delta.y1;
  if(line[0].y2 == 0 && delta.y2 < 0) delta.y2 = -delta.y2;
  if(line[0].x1 == 320 && delta.x1 > 0) delta.x1 = -delta.x1;
  if(line[0].x2 == 320 && delta.x2 > 0) delta.x2 = -delta.x2;
  if(line[0].y1 == 240 && delta.y1 > 0) delta.y1 = -delta.y1;
  if(line[0].y2 == 240 && delta.y2 > 0) delta.y2 = -delta.y2;
}

void Display::addGraphPoints()
{
  static bool bInit = false;
  if(bInit == false)
  {
    memset(m_points, 0, sizeof(m_points));
    bInit = true;
  }
  if( hvac.m_inTemp == 0)
    return;
  if(m_pointsAdded == 299)
    memcpy(&m_points, &m_points[1], sizeof(m_points) - 6);

  const int base = 660; // 66.0 base   Todo: scale all this
  m_points[m_pointsAdded][0] = (hvac.m_inTemp - base) * 101 / 110; // 66~90 scale to 0~220
  m_points[m_pointsAdded][1] = hvac.m_rh * 55 / 250;
  m_points[m_pointsAdded][2] = (hvac.m_targetTemp - base) * 101 / 110;

  int8_t tt = hvac.m_EE.cycleThresh;
  if(hvac.getMode() == Mode_Cool) // Todo: could be auto
    tt = -tt;

  m_points[m_pointsAdded][3] = (hvac.m_targetTemp + tt - base) * 101 / 110;
  m_points[m_pointsAdded][4] = hvac.getState();
  m_points[m_pointsAdded][5] = (hvac.m_localTemp - base) * 101 / 110; // 66~90 scale to 0~220

  if(m_pointsAdded < 299) // 300x220
    m_pointsAdded++;
}

// Draw the last 25 hours (todo: add run times)
void Display::fillGraph()
{
  uint16_t textcolor = rgb16(0, 63, 31);
  nex.text(292, 219, 2, textcolor, String(66));
  nex.line( 10, 164+8, 310, 164+8, rgb16(10, 20, 10) );
  nex.text(292, 164, 2, textcolor, String(72));
  nex.line( 10, 112+8, 310, 112+8, rgb16(10, 20, 10) );
  nex.text(292, 112, 2, textcolor, String(78));
  nex.line( 10,  58+8, 310,  58+8, rgb16(10, 20, 10) );
  nex.text(292, 58, 2, textcolor, String(84));
  nex.text(292,  8, 2, textcolor, String(90));

  int16_t x = m_pointsAdded - 1 - (minute() / 5); // center over even hour
  int8_t h = hourFormat12();

  while(x > 10)
  {
    nex.line(x, 10, x, 230, rgb16(10, 20, 10) );
    nex.text(x-4, 0, 1, 0x7FF, String(h)); // draw hour above chart
    x -= 12 * 6; // left 6 hours
    h -= 6;
    if( h <= 0) h += 12;
  }

  drawPoints(2, rgb16( 22, 40, 10) ); // target (draw behind the other stuff)
  drawPoints(3, rgb16( 22, 40, 10) ); // target threshold
  drawPoints(1, rgb16(  0, 53,  0) ); // rh green
  if(hvac.isRemote())
    drawPoints(5, rgb16( 31, 0,  15) ); // remote temp
  drawPointsTemp(); // off/cool/heat colors
//  drawPoints(0, rgb16(31,  0,  0) ); // plain inTemp red
}

void Display::drawPoints(uint8_t arr, uint16_t color)
{
  uint8_t y = m_points[0][arr];
  const int yOff = 240-10;

  for(int i = 1, x = 10; i < m_pointsAdded; i++)
  {
    if(y != m_points[i+1][arr])
    {
      nex.line(x, yOff - y, i+10, yOff - m_points[i][arr], color);
      x = i + 10;
    }
    y = m_points[i][arr];
  }
}

// Not implemented yet (colors might be weird or something)
void Display::drawPointsTemp()
{
  uint8_t y = m_points[0][0];
  const int yOff = 240-10;
  uint16_t color = rgb16(31, 0, 0);

  for(int i = 1, x = 10; i < m_pointsAdded; i++)
  {
    color = stateColor(m_points[i][4]);
    nex.line(x, yOff - y, i+10, yOff - m_points[i][0], color);
    x = i + 10;
    y = m_points[i][0];
  }
}

uint16_t Display::stateColor(uint8_t v) // return a color based on run state
{
  uint16_t color;

  switch(v)
  {
    case State_Off: // off
      color = rgb16(20, 40, 20); // gray
      break;
    case State_Cool: // cool
      color = rgb16(0, 0, 31); // blue
      break;
    case State_HP:
    case State_NG:
      color = rgb16(31, 0, 0); // red
      break;
  }
  return color;
}
