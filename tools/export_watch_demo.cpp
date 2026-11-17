// Headless export of the watch's real LovyanGFX UI. Invoked by the Python
// wrapper; this file is not part of the PlatformIO firmware source directory.
#include <DigitalWatchface.hpp>
#include <lgfx/v1/LGFX_Sprite.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace lgfx { inline namespace v1 {
unsigned long millis() { return 0; }
unsigned long micros() { return 0; }
void delay(unsigned long) {}
void delayMicroseconds(unsigned int) {}
void gpio_hi(uint32_t) {}
void gpio_lo(uint32_t) {}
bool gpio_in(uint32_t) { return false; }
void pinMode(int_fast16_t, pin_mode_t) {}
void lgfxPinMode(int_fast16_t, pin_mode_t) {}
}}

using lgfx::LGFX_Sprite;
static constexpr int SIZE = 240;
static constexpr int CENTER = 120;
static constexpr uint32_t STEPS = 1957;
static constexpr uint8_t BATTERY = 79;
static constexpr uint64_t TIME_MS = (10 * 3600 + 8 * 60 + 30) * 1000ULL;
static constexpr uint32_t PALETTE[] = {
    0, 0, 0x202020, 0x303030, 0x404040, 0, 0, 0xa0ebaa,
    0xff0000, 0xc80000, 0x008cff, 0xffc280, 0xffffff, 0xc0c0c0,
    0xff0000, 0xffffff};
static constexpr uint8_t SHOE[] = {
    0x20,0x00,0x00, 0x50,0x00,0x00, 0x90,0x80,0x00,
    0x8f,0x40,0x00, 0x80,0xa0,0x00, 0x80,0x5e,0x00,
    0x80,0x01,0x00, 0xbf,0xff,0x80, 0x80,0x00,0x80,
    0x7f,0xff,0x00};

static void require(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "%s\n", message);
  std::exit(1);
}

static void setup(LGFX_Sprite& sprite, int width, int height, bool indexed=false) {
  sprite.setColorDepth(indexed ? lgfx::palette_4bit : lgfx::rgb565_2Byte);
  require(sprite.createSprite(width, height), "Unable to allocate native sprite");
  sprite.setPivot(width / 2, height / 2);
  if (indexed) {
    for (unsigned i=0; i<16; ++i) {
      const uint32_t c = PALETTE[i];
      sprite.setPaletteColor(i, c >> 16, c >> 8, c);
    }
  }
  sprite.fillScreen(indexed ? 6 : 0x000000U);
}

static void ppm(const std::string& path, LGFX_Sprite& sprite) {
  FILE* file = std::fopen(path.c_str(), "wb");
  require(file, "Unable to open exported image");
  std::fprintf(file, "P6\n%d %d\n255\n", sprite.width(), sprite.height());
  for (int y=0; y<sprite.height(); ++y) for (int x=0; x<sprite.width(); ++x) {
    const uint16_t p = sprite.readPixel(x,y);
    const unsigned char pixel[] = {
      static_cast<unsigned char>(((p >> 11) & 31) * 255 / 31),
      static_cast<unsigned char>(((p >> 5) & 63) * 255 / 63),
      static_cast<unsigned char>((p & 31) * 255 / 31)};
    std::fwrite(pixel, 1, 3, file);
  }
  std::fclose(file);
}

static void drawDial(LGFX_Sprite& dial) {
  dial.setTextFont(4);
  dial.setTextDatum(lgfx::middle_center);
  dial.fillScreen(0);
  dial.fillCircle(CENTER, CENTER, CENTER, 6);
  for (int i=1; i<=60; ++i) {
    const float angle = (i % 60) * 6.0f * 0.0174532925f;
    const float c = -std::cos(angle);
    const float s = std::sin(angle);
    const bool major = i % 5 == 0;
    dial.fillCircle(CENTER + s * (CENTER * 10 / 10.4f),
                    CENTER + c * (CENTER * 10 / 10.4f), major * 3 + 1, 12);
    if (major && i % 15 == 0) {
      dial.setTextColor(15);
      dial.drawNumber(i / 5, CENTER + s * (CENTER * 10 / 13.0f),
                      CENTER + c * (CENTER * 10 / 13.0f) + 3);
    }
  }
}

static void battery(LGFX_Sprite& dial) {
  constexpr int x=145, y=115;
  dial.fillRect(x,y,20,12,6);
  dial.drawRect(x,y,16,12,13);
  dial.fillRect(x+16,y+3,3,6,13);
  dial.fillRect(x+2,y+2,14*BATTERY/100,8,10);
  dial.setFont(&lgfx::fonts::Font2);
  dial.setTextDatum(lgfx::top_left);
  dial.setTextColor(13,6);
  char text[8];
  std::snprintf(text,sizeof(text),"%3u%%",BATTERY);
  dial.drawString(text,x+20,y-2);
}

static void date(LGFX_Sprite& dial) {
  dial.fillRect(45,56,150,28,6);
  dial.setFont(&lgfx::fonts::Font2);
  dial.setTextDatum(lgfx::middle_center);
  dial.setTextColor(13,6);
  dial.drawString("Sat 07 Nov",120,74);
}

static void steps(LGFX_Sprite& canvas) {
  canvas.setFont(&lgfx::fonts::Font2);
  canvas.setTextDatum(lgfx::middle_left);
  canvas.setTextColor(0xc0c0c0U,0x000000U);
  const char* text = "1,957";
  const int row_width = 17 + 4 + canvas.textWidth(text);
  const int x = CENTER - row_width / 2;
  canvas.drawBitmap(x,165,SHOE,17,10,0xc0c0c0U);
  canvas.drawString(text,x+21,170);
}

static void dot(LGFX_Sprite& canvas, int position, uint32_t color) {
  const float a=position*6.0f*0.0174532925f;
  canvas.fillCircle(CENTER+std::sin(a)*(CENTER*10/10.4f),
                    CENTER-std::cos(a)*(CENTER*10/10.4f),
                    (position % 5 == 0)*3+1,color);
}

static void exportFont(const std::string& out, FILE* metadata,
                       const char* name, const lgfx::IFont* font,
                       int cell_width, int cell_height) {
  constexpr int columns=16, first=32, last=126, padding=8;
  constexpr int rows=(last-first+1+columns-1)/columns;
  LGFX_Sprite atlas;
  setup(atlas,columns*cell_width,rows*cell_height);
  atlas.setFont(font);
  atlas.setTextDatum(lgfx::top_left);
  atlas.setTextColor(0xffffffU,0x000000U);
  lgfx::FontMetrics defaults{};
  font->getDefaultMetric(&defaults);
  std::fprintf(metadata,
      "\"%s\":{\"image\":\"%s-atlas.png\",\"cellWidth\":%d,"
      "\"cellHeight\":%d,\"columns\":16,\"padding\":8,"
      "\"first\":32,\"last\":126,\"height\":%d,\"baseline\":%d,\"glyphs\":{",
      name,name,cell_width,cell_height,defaults.height,defaults.baseline);
  for (int code=first; code<=last; ++code) {
    const int x=(code-first)%columns*cell_width;
    const int y=(code-first)/columns*cell_height;
    const char text[]={static_cast<char>(code),0};
    atlas.drawString(text,x+padding,y+padding);
    auto metric=defaults;
    font->updateFontMetric(&metric,code);
    std::fprintf(metadata,
        "%s\"%d\":{\"x\":%d,\"y\":%d,\"advance\":%d,"
        "\"xOffset\":%d,\"inkWidth\":%d,\"leadingAdjustment\":%d,\"textWidth\":%d}",
        code==first?"":",",code,x,y,metric.x_advance,metric.x_offset,
        metric.width,metric.x_offset<0?-metric.x_offset:0,atlas.textWidth(text));
  }
  std::fprintf(metadata,"}}");
  ppm(out+"/"+name+"-atlas.ppm",atlas);
}

static void exportDigits(const std::string& out) {
  FILE* file=std::fopen((out+"/digital-digits.ppm").c_str(),"wb");
  require(file,"Unable to export digital digits");
  std::fprintf(file,"P6\n440 68\n255\n");
  uint8_t pixels[68][440]{};
  for (unsigned digit=0; digit<10; ++digit) {
    unsigned offset=watch::digital_detail::digit_offsets[digit];
    for (int y=0; y<68; ++y) {
      int x=0;
      while (x<44) {
        const uint8_t run=watch::digital_detail::digit_coverage[offset++];
        const unsigned char alpha=(run>>4)*17;
        const int length=(run&15)+1;
        for (int i=0; i<length; ++i) pixels[y][digit*44+x+i]=alpha;
        x+=length;
      }
    }
  }
  for (auto& row:pixels) for (uint8_t alpha:row) {
    const uint8_t rgb[]={alpha,alpha,alpha};
    std::fwrite(rgb,1,3,file);
  }
  std::fclose(file);
}

int main(int argc,char** argv) {
  require(argc==2,"Usage: export_watch_demo OUTPUT_DIRECTORY");
  const std::string out=argv[1];
  LGFX_Sprite canvas;
  setup(canvas,SIZE,SIZE);
  LGFX_Sprite dial(&canvas),hour(&canvas),minute(&canvas),second(&canvas);
  setup(dial,SIZE,SIZE,true);
  setup(hour,9,119,true);hour.setPivot(3,100);hour.fillScreen(0);
  setup(minute,9,119,true);minute.setPivot(3,100);minute.fillScreen(0);
  setup(second,3,119,true);second.setPivot(1,100);second.fillScreen(9);
  hour.fillRect(0,42,7,53,15);hour.fillCircle(3,42,3,15);
  minute.fillRect(2,5,3,90,15);minute.fillCircle(3,5,1,15);
  second.fillRect(0,99,1,2,9);
  ppm(out+"/hour-hand.ppm",hour);
  ppm(out+"/minute-hand.ppm",minute);
  ppm(out+"/second-hand.ppm",second);
  drawDial(dial);
  ppm(out+"/analog-dial.ppm",dial);

  LGFX_Sprite shoe;
  setup(shoe,17,10);
  shoe.drawBitmap(0,0,SHOE,17,10,0xffffffU);
  ppm(out+"/shoe-icon.ppm",shoe);
  exportDigits(out);
  FILE* metadata=std::fopen((out+"/native-fonts.json").c_str(),"w");
  require(metadata,"Unable to write font metrics");
  std::fprintf(metadata,"{");
  exportFont(out,metadata,"font2",&lgfx::fonts::Font2,32,32);
  std::fprintf(metadata,",");
  exportFont(out,metadata,"roboto-thin24",&lgfx::fonts::Roboto_Thin_24,64,48);
  std::fprintf(metadata,"}\n");
  std::fclose(metadata);

  // Reference analog frame uses the same palette sprites and AA composition as
  // composeClock in main.cpp, with fixed demo data instead of hardware reads.
  date(dial);battery(dial);dial.pushSprite(0,0);steps(canvas);
  const int sec=TIME_MS/1000;
  const int min=sec/60;
  dot(canvas,min%60,0xff0000U);
  dot(canvas,((min/60)*5)%60,0xff0000U);
  dot(canvas,sec%60,0xc80000U);
  hour.pushRotateZoomWithAA(static_cast<float>(TIME_MS)/120000.0f,1,1,0);
  minute.pushRotateZoomWithAA(static_cast<float>(TIME_MS)/10000.0f,1,1,0);
  second.pushRotateZoomWithAA(static_cast<float>(TIME_MS)*6.0f/1000.0f,.7f,1,0);
  canvas.fillCircle(CENTER,CENTER,6,0xc80000U);
  canvas.drawCircle(CENTER,CENTER,2,0x000000U);
  ppm(out+"/analog-reference.ppm",canvas);
  const watch::DigitalFaceData demo{10,8,7,11,6,STEPS,BATTERY,false,1.5f};
  watch::drawDigitalWatchface(canvas,demo,false);
  ppm(out+"/digital-reference.ppm",canvas);
  std::puts("Exported real LovyanGFX dial, hands, fonts, digits and reference faces.");
}
