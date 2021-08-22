HardwareSerial Ser(2);

#define LGFX_USE_V1

#include <LovyanGFX.hpp>
#include <stdlib.h>
#include <stdint.h>

TaskHandle_t th[2];

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Light_PWM _light_instance;

public:
  LGFX(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = VSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = 1;
      cfg.pin_sclk = 18;
      cfg.pin_mosi = 23;
      cfg.pin_miso = 19;
      cfg.pin_dc = 5;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 4;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 1;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;

      _panel_instance.config(cfg);
    }

    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = 21;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
    setPanel(&_panel_instance);
  }
};

LGFX lcd;
static LGFX_Sprite buf(&lcd);

//---------------Graph Settings----------------
#define GRAPHX 0
#define GRAPHY 58
#define GRAPHHEIGHT 180
#define GRAPHWIDTH 320
#define DATAS 1000
#define AVEN 2
#define DIVH 6
#define DIVV 6
//---------------------------------------------

//Frequency Calc
char buff[32];
char fbuf[16];
int counter = 0;
double freq;
unsigned long period = 0;

//Display
unsigned int disprange = 120;
unsigned int dispshift = 0;
unsigned int lastaddr = 1;
unsigned int hrefaddr = 0;
unsigned int lrefaddr = 0;
unsigned int maxdata = 60100;
unsigned int mindata = 59900;
unsigned int graphtop;
unsigned int graphbottom;
unsigned int resolution;
float ydiv;
float xdiv;
uint16_t freqlog[DATAS] = {};
unsigned int ext[] = {10, 20, 40, 50, 100, 200, 400, 500, 1000};
unsigned int tim[] = {15, 30, 60, 120, 300, 600};
uint8_t refreshflag = 0;

void bufdraw(void)
{
  buf.pushSprite(0, 0);
  buf.clear();
}

void dataadd(uint16_t value)
{
  if (lastaddr == (DATAS - 1))
  {
    lastaddr = 0;
  }
  else
  {
    lastaddr++;
  }
  freqlog[lastaddr] = value;
  if (lastaddr == hrefaddr || lastaddr == lrefaddr)
  {
  }
  if (value > maxdata)
  {
    maxdata = value;
  }
  if (value < mindata)
  {
    mindata = value;
  }
}

void extcheck(int16_t checkstart, uint16_t dats)
{
  checkstart = (DATAS + lastaddr - checkstart) % DATAS;
  uint16_t max = 60010;
  uint16_t min = 59990;
  int16_t checkend = (DATAS + checkstart - dats) % DATAS;
  int16_t n = checkstart;
  while (n != checkend)
  {
    if (freqlog[n] > max)
    {
      max = freqlog[n];
    }
    if (freqlog[n] < min)
    {
      min = freqlog[n];
    }
    n--;
    if (n < 0)
    {
      n = DATAS - 1;
    }
  }
  mindata = min;
  maxdata = max;
}

void dataprot(int16_t start, uint16_t dats)
{
  start = (DATAS + lastaddr - start) % DATAS;
  int16_t stcp = start;
  int16_t laststcp = start;
  float prestart = (float)start;
  uint8_t drawy[GRAPHWIDTH - 2];
  uint16_t drawx = (GRAPHWIDTH - 2);
  uint32_t sum = 0;
  uint16_t ave = 0;
  uint8_t offset = 0;
  uint8_t stchanged = 1;
  while (drawx > 0)
  {
    for (int d = 0; d < AVEN; d++) //移動平均を求める
    {
      sum += freqlog[((stcp + DATAS - d) % DATAS)];
    }
    ave = sum / AVEN;
    //オフセット（DIV)分を求める　graphbottomの線から何DIV上に書く
    for (int s = 1; s < DIVH; s++)
    {
      if ((graphbottom + resolution * s) <= ave) //resolutionは最小の目盛間隔10－＞0.010Hz
        offset++;
      else
        break;
    }
    //graphbottomからの絶対距離をdrawyに保持　ydivは1div当たりYピクセル数
    if (stchanged == 1)
    {
      drawy[drawx - 1] = (offset * ydiv) + ydiv * (ave % resolution) / resolution;
      stchanged = 0;
    }
    else
      drawy[drawx - 1] = 255;
    //使用した変数を初期化
    offset = 0;
    ave = 0;
    sum = 0;
    //1つ前のデータへ移動
    drawx--;
    //移動平均の開始位置をシフトする
    prestart -= (dats / ((float)GRAPHWIDTH));
    laststcp = stcp;
    stcp = (int16_t)prestart;
    if (laststcp == stcp)
      stchanged = 0;
    else
      stchanged = 1;
  }
  //draw
  uint16_t save = 0;
  for (int n = 0; n < GRAPHWIDTH - 3; n++) //液晶に描画 0~316?
  {
    if (drawy[n] != 255)
    {
      if (drawy[n + 1] != 255) //次のデータは255ではない（実データ）
      {
        buf.drawLine((GRAPHX + 1 + n), (GRAPHY + GRAPHHEIGHT - drawy[n]), (GRAPHX + 2 + n), (GRAPHY + GRAPHHEIGHT - drawy[n + 1]), TFT_RED);
      }
      else
      {                             //次のデータは２５５（パディング）
        save = n;                   //現在位置を保存する（開始位置を指定するため必要）　これは最後にデータがあった場所を表す
        while (drawy[n + 1] == 255) //隣を比較して２５５なら繰り返して実データが出るまで待機
        {
          n++;
          if (n >= GRAPHWIDTH - 3)
            return;
        }
        buf.drawLine((GRAPHX + 1 + save), (GRAPHY + GRAPHHEIGHT - drawy[save]), (GRAPHX + 2 + n), (GRAPHY + GRAPHHEIGHT - drawy[n + 1]), TFT_RED);
      }
    }
  }
}

void drawframe(void)
{
  //Draw Outside Frame and Value
  buf.drawRect(GRAPHX, GRAPHY, GRAPHWIDTH, GRAPHHEIGHT, TFT_RED);
  buf.setFont(&fonts::Font0);
  buf.setCursor(GRAPHX + 2, GRAPHY + 1);
  buf.printf("%5u", graphtop);
  buf.setCursor(GRAPHX + 2, GRAPHY + GRAPHHEIGHT - 7);
  buf.printf("%5u", graphbottom);
}

void drawframehorz(void)
{
  unsigned int range = 0;
  int s = 0;
  int divs = 1;
  int t = 1;
  //Range Fix
  for (s = 0; s < 8; s++)
  {
    graphtop = maxdata + ((maxdata % ext[s] == 0) ? 0 : (ext[s] - (maxdata % ext[s])));
    graphbottom = mindata - (mindata % ext[s]);
    range = graphtop - graphbottom;
    if (ext[s] >= (range / DIVH))
    {
      break;
    }
  }
  resolution = ext[s];
  //Solve Divides
  while ((graphbottom + ext[s] * divs) < graphtop)
  {
    divs++;
  }
  //Solve Y pixels/div
  ydiv = GRAPHHEIGHT / (float)divs;
  //Draw Horizontal Line
  while ((graphtop - ext[s] * t) > graphbottom)
  {
    buf.drawFastHLine((GRAPHX + 1), (GRAPHY + (ydiv * t)), (GRAPHWIDTH - 2), (((graphtop - ext[s] * t) == 60000) ? TFT_YELLOW : TFT_WHITE));
    buf.setCursor(GRAPHX + 2, (GRAPHY + (ydiv * t) + 1));
    buf.setFont(&fonts::Font0);
    buf.printf("%5u", (graphtop - ext[s] * t));
    t++;
  }
}

void drawframevert(unsigned int sec)
{
  unsigned int range = sec;
  uint16_t numx = 0;
  uint16_t numy = 0;
  int s = 0;
  for (s = 0; s < 6; s++)
  {
    if (tim[s] >= (range / DIVV))
    {
      break;
    }
  }
  xdiv = (float)GRAPHWIDTH / range; //1s当たりのPX数
  int t = 0;
  while ((GRAPHX + GRAPHWIDTH - (t * (xdiv * tim[s])) + (dispshift * xdiv)) > GRAPHX)
  {
    buf.drawFastVLine((GRAPHX + GRAPHWIDTH - (t * (xdiv * tim[s])) + (dispshift * xdiv)), (GRAPHY + 1), (GRAPHHEIGHT - 2), TFT_WHITE);
    numx = (GRAPHX + GRAPHWIDTH - (t * (xdiv * tim[s])) + (dispshift * xdiv) + 1);
    numy = (GRAPHY + GRAPHHEIGHT - 8);
    if (numx < (GRAPHWIDTH + GRAPHX - 5))
    {
      buf.setCursor(numx, numy);
      buf.setFont(&fonts::Font0);
      buf.printf("%u", (tim[s] * t));
    }
    t++;
  }
}

void redraw(void)
{
  extcheck(dispshift, disprange);
  //Frequency Draw
  sprintf(fbuf, "%7.4lf", freq);
  buf.setFont(&fonts::Font7);
  buf.setCursor(0, 0);
  buf.println(fbuf);
  buf.setFont(&fonts::Font4);
  buf.setCursor(204, 24);
  buf.println("Hz");
  //Disp Range Draw
  buf.setFont(&fonts::Font4);
  buf.setCursor(240, 0);
  buf.println(disprange);
  //Frame Draw
  drawframe();
  drawframehorz();
  drawframevert(disprange);
  //Data Draw
  dataprot(dispshift, disprange);
  bufdraw();
}

void task1(void *pvParameters)
{
  while (1)
  {
    while (Ser.available())
    {
      char inChar = char(Ser.read());
      buff[counter] = inChar;
      counter++;
      if (inChar == '\n')
      {
        counter = 0;
        period = atol(buff);
        freq = ((double)16000000 / (double)period);
        dataadd((uint16_t)(freq * 1000));
        refreshflag = 1;
      }
    }
    vTaskDelay(1);
  }
}

void task2(void *pvParameters)
{
  while (1)
  {
    if (refreshflag == 1)
    {
      refreshflag = 0;
      redraw();
    }
    if (digitalRead(26) == LOW)
    {
      dispshift += 1;
      if (dispshift == 1000)
      {
        dispshift = 999;
      }
      redraw();
    }
    if (digitalRead(27) == LOW)
    {
      dispshift -= 1;
      if (dispshift == -1)
      {
        dispshift = 0;
      }
      redraw();
    }
    if (digitalRead(14) == LOW)
    {
      if (disprange != 0)
      {
        disprange--;
      }
      redraw();
    }
    if (digitalRead(13) == LOW)
    {
      if (disprange != 1000)
      {
        disprange++;
      }
      redraw();
    }
    delay(1);
  }
}

void setup(void)
{

  //Initialize
  pinMode(13, INPUT);
  pinMode(14, INPUT);
  pinMode(26, INPUT);
  pinMode(27, INPUT);
  Serial.begin(115200);
  Ser.begin(115200);
  //LCD initialize
  lcd.init();
  lcd.setTextSize((std::max(lcd.width(), lcd.height()) + 255) >> 8);
  lcd.fillScreen(TFT_BLACK);
  buf.setPsram(true);
  buf.setColorDepth(8);
  buf.createSprite(320, 240);
  buf.setFont(&fonts::Font7);
  buf.setTextColor(TFT_WHITE);
  buf.setTextWrap(false);
  //initialize log data
  for (int s = 0; s < DATAS; s++)
  {
    freqlog[s] = 59995;
  }
  //Crate task
  xTaskCreateUniversal(
      task2,
      "task2",
      8192,
      NULL,
      1,
      NULL,
      APP_CPU_NUM);
  xTaskCreateUniversal(
      task1,
      "task1",
      8192,
      NULL,
      1,
      NULL,
      PRO_CPU_NUM);
}

void loop(void)
{
  delay(1);
}