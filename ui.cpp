#include <string.h>
#include <float.h>
#include <math.h>

#include "pico/multicore.h"
#include "ui.h"
#include <hardware/flash.h>
#include "pico/util/queue.h"

#define WATERFALL_WIDTH (128)
#define WATERFALL_MAX_VALUE (64)

static const uint32_t ev_display_tmout_evset = (1UL << ev_button_menu_press) |
                                               (1UL << ev_button_back_press) |
                                               (1UL << ev_button_push_press);

////////////////////////////////////////////////////////////////////////////////
// Encoder 
////////////////////////////////////////////////////////////////////////////////

void ui::setup_encoder()
{
    gpio_set_function(PIN_AB, GPIO_FUNC_PIO1);
    gpio_set_function(PIN_AB+1, GPIO_FUNC_PIO1);
    uint offset = pio_add_program(pio, &quadrature_encoder_program);
    quadrature_encoder_program_init(pio, sm, offset, PIN_AB, 1000);
    new_position = (quadrature_encoder_get_count(pio, sm) + 2)/4;
    old_position = new_position;
}

int32_t ui::get_encoder_change()
{
    new_position = -((quadrature_encoder_get_count(pio, sm) + 2)/4);
    int32_t delta = new_position - old_position;
    old_position = new_position;
    if((settings[idx_hw_setup] >> flag_reverse_encoder) & 1)
    {
      return -delta;
    } else {
      return delta;
    }
}

int32_t ui::encoder_control(int32_t *value, int32_t min, int32_t max)
{
	int32_t position_change = get_encoder_change();
	*value += position_change;
	if(*value > max) *value = min;
	if(*value < min) *value = max;
	return position_change;
}

////////////////////////////////////////////////////////////////////////////////
// Display
////////////////////////////////////////////////////////////////////////////////
void ui::setup_display() {
  i2c_init(i2c1, 400000);
  gpio_set_function(PIN_DISPLAY_SDA, GPIO_FUNC_I2C);
  gpio_set_function(PIN_DISPLAY_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(PIN_DISPLAY_SDA);
  gpio_pull_up(PIN_DISPLAY_SCL);
  disp.external_vcc=false;
  ssd1306_init(&disp, 128, 64, 0x3C, i2c1);
}

void ui::display_clear(bool colour)
{
  cursor_x = 0;
  cursor_y = 0;
  ssd1306_clear(&disp, colour);
}

void ui::display_clear_str(uint32_t scale, bool colour)
{
  ssd1306_fill_rectangle(&disp, 0, cursor_y, 128, 9*scale, colour);
}

void ui::display_linen(uint8_t line)
{
  cursor_y = 9*(line-1);
  cursor_x = 0;
}

void ui::display_set_xy(int16_t x, int16_t y)
{
  cursor_x = x;
  cursor_y = y;
}

void ui::display_add_xy(int16_t x, int16_t y)
{
  cursor_x += x;
  cursor_y += y;
}

uint16_t ui::display_get_x() { return cursor_x; }

uint16_t ui::display_get_y() { return cursor_y; };

void ui::display_draw_separator(uint16_t y, uint32_t scale, bool colour){
  // always draw top line
  ssd1306_draw_line(&disp, 0, y, 127, y, colour);
  // for 2px, just draw another below
  if (scale == 2) {
    ssd1306_draw_line(&disp, 0, y+1, 127, y+1, colour);
  }
  // for 3px draw top and bottom, middle blank
  if (scale == 3) {
    ssd1306_draw_line(&disp, 0, y+2, 127, y+2, colour);
  }
}

void ui::display_print_char(char x, uint32_t scale, uint32_t style)
{
  if ( !(style&style_nowrap) && (cursor_x > 128 - 6*(signed)scale)) {
    cursor_x = 0;
    cursor_y += 9*scale;
  }

  ssd1306_draw_char(&disp, cursor_x, cursor_y, scale, x, !(style&style_reverse) );
  cursor_x += (6*scale);
}

/* return index of 1st match. -1 if not found */
int ui::strchr_idx(const char str[], uint8_t c) {
  for (unsigned int i=0; i<strlen(str);i++){
    if (str[i] == c) return i;
  }
  return -1;
}

void ui::display_print_str(const char str[], uint32_t scale, uint32_t style)
{
  int16_t box_x1 = INT16_MAX;
  int16_t box_y1 = INT16_MAX;
  int16_t box_x2 = INT16_MIN;
  int16_t box_y2 = INT16_MIN;

  bool colour = !(style&style_reverse);
  int next_ln;
  unsigned int length;

  // find the index of the next \n
  next_ln = strchr_idx( str, '\n');
  // if found, compute length of string, if not, length to end of str
  length = (next_ln<0) ? strlen(str) : (unsigned)next_ln;

  if (style & style_centered) {
    cursor_x = (128- 6*scale*length)/2;
  }
  if (style & style_right) {
    cursor_x = (128 - 6*scale*length);
  }

  for (size_t i=0; i<strlen(str); i++) {
    if (str[i] == '\a') {
      colour = !colour;
      continue;
    }
    if (str[i] == '\n') {
      next_ln = strchr_idx( &str[i+1], '\n');
      length = (next_ln<0) ? strlen(str)-(i+1) : (unsigned)next_ln-(i+1);

      if (style & style_centered) {
        cursor_x = (128- 6*scale*length)/2;
      } else if (style & style_right) {
        cursor_x = (128- 6*scale*length);
      } else {
        cursor_x = 0;
      }
      cursor_y += 9*scale;
      continue;
    }
    if ( !(style&style_nowrap) && (cursor_x > 128 - 6*(signed)scale)) {
      cursor_x = 0;
      cursor_y += 9*scale;
    }
    ssd1306_draw_char(&disp, cursor_x, cursor_y, scale, str[i], colour );
    if (style&style_bordered) {
      if (cursor_x < box_x1) box_x1=cursor_x;
      if (cursor_y < box_y1) box_y1=cursor_y;
      if ((signed)(cursor_x + 5*scale) > box_x2) box_x2 = (cursor_x + 5*scale);
      if ((signed)(cursor_y + 8*scale) > box_y2) box_y2 = (cursor_y + 8*scale);
    }
    cursor_x += 6*scale;
  }
  if (style&style_bordered) {
    // text, black, white, black
    ssd1306_draw_rectangle(&disp, box_x1-1, box_y1-1, box_x2-box_x1+1, box_y2-box_y1+1, 1-colour);
    ssd1306_draw_rectangle(&disp, box_x1-2, box_y1-2, box_x2-box_x1+3, box_y2-box_y1+3, colour);
    ssd1306_draw_rectangle(&disp, box_x1-3, box_y1-3, box_x2-box_x1+5, box_y2-box_y1+5, 1-colour);
  }
}

void ui::display_print_num(const char format[], int16_t num, uint32_t scale, uint32_t style)
{
  char buff[16];
  snprintf(buff, 16, format, num);
  display_print_str(buff, scale, style);
}

void ui::display_print_freq(char separator, uint32_t frequency, uint32_t scale, uint32_t style)
{
  char buff[16];
  const int32_t MHz = frequency / 1000000;
  frequency %= 1000000;
  const int32_t kHz = frequency / 1000;
  frequency %= 1000;
  const int32_t Hz = frequency;
  snprintf(buff, 16, "%2ld%c%03ld%c%03ld", MHz, separator, kHz, separator, Hz);
  display_print_str(buff, scale, style);
}

void ui::display_show()
{
  ssd1306_show(&disp);
}

static float find_nearest_tick(float dist)
{
  const float ticks[] = {10.0f, 5.0f, 2.0f, 1.0f, 0.5f, 0.2f, 0.1f, 0.05f, 0.02f, 0.01f};
  float min_dist = fabsf(dist - ticks[0]);
  float min_tick = ticks[0];

  for(size_t i = 1; i < sizeof(ticks) / sizeof(ticks[0]); i++)
  {
    if(fabsf(dist - ticks[i]) < min_dist)
    {
      min_dist = fabsf(dist - ticks[i]);
      min_tick = ticks[i];
    }
  }
  return min_tick;
}

////////////////////////////////////////////////////////////////////////////////
// Home page status display (original)
////////////////////////////////////////////////////////////////////////////////
void ui::renderpage_original(bool view_changed, rx_status & status, rx & receiver)
{
  receiver.access(false);
  const float power_dBm = status.signal_strength_dBm;
  const float battery_voltage = 3.0f * 3.3f * (status.battery/65535.0f);
  const float temp_voltage = 3.3f * (status.temp/65535.0f);
  const float temp = 27.0f - (temp_voltage - 0.706f)/0.001721f;
  const float block_time = (float)adc_block_size/(float)adc_sample_rate;
  const float busy_time = ((float)status.busy_time*1e-6f);
  receiver.release();
#define buff_SZ 21
  char buff [buff_SZ];
  display_clear();

  //frequency
  uint32_t remainder, MHz, kHz, Hz;
  MHz = (uint32_t)settings[idx_frequency]/1000000u;
  remainder = (uint32_t)settings[idx_frequency]%1000000u; 
  kHz = remainder/1000u;
  remainder = remainder%1000u; 
  Hz = remainder;
  display_set_xy(0,0);
  snprintf(buff, buff_SZ, "%2lu.%03lu", MHz, kHz);
  display_print_str(buff,2);
  snprintf(buff, buff_SZ, ".%03lu", Hz);
  display_print_str(buff,1);

  //mode
  display_print_str(modes[settings[idx_mode]],1, style_right);

  //step
  display_set_xy(0,8);
  display_print_str(steps[settings[idx_step]],1, style_right);

  //signal strength/cpu
  int8_t power_s = dBm_to_S(power_dBm);

  display_set_xy(0,24);
  display_print_str(smeter[power_s],1);
  display_print_num("% 4ddBm", (int)power_dBm, 1, style_right);

  snprintf(buff, buff_SZ, "%2.1fV %2.0f%cC %3.0f%%", battery_voltage, temp, '\x7f', (100.0f*busy_time)/block_time);
  display_set_xy(0,16);
  display_print_str(buff, 1, style_right);

  draw_spectrum(32, receiver);

  display_show();
}

////////////////////////////////////////////////////////////////////////////////
// Home page status display with bigger spectrum view
////////////////////////////////////////////////////////////////////////////////
void ui::renderpage_bigspectrum(bool view_changed, rx_status & status, rx & receiver)
{
  display_clear();
  draw_slim_status(0, status, receiver);
  draw_spectrum(8, receiver);
  display_show();
}

////////////////////////////////////////////////////////////////////////////////
// Home page status display with big waterfall
////////////////////////////////////////////////////////////////////////////////
void ui::renderpage_waterfall(bool view_changed, rx_status & status, rx & receiver)
{
  if (view_changed) display_clear();

  draw_waterfall(8, receiver);
  draw_slim_status(0, status, receiver);
  display_show();
}

////////////////////////////////////////////////////////////////////////////////
// Home page status display with big simple text
////////////////////////////////////////////////////////////////////////////////
void ui::renderpage_bigtext(bool view_changed, rx_status & status, rx & receiver)
{

  receiver.access(false);
  const float power_dBm = status.signal_strength_dBm;
  receiver.release();

  display_clear();
  display_set_xy(0,0);
  display_print_freq('.',settings[idx_frequency],2,style_centered);

  display_set_xy(0,24);
  //mode and step size
  display_print_str(modes[settings[idx_mode]],2);
  display_print_str(steps[settings[idx_step]], 2, style_right);

  //signal strength
  display_set_xy(0,48);
  int8_t power_s = dBm_to_S(power_dBm);

  display_print_str(smeter[power_s],2);

  display_show();
}

// Draw a slim 8 pixel status line
void ui::draw_slim_status(uint16_t y, rx_status & status, rx & receiver)
{
  receiver.access(false);
  const float power_dBm = status.signal_strength_dBm;
  receiver.release();

  display_set_xy(0,y);
  display_print_freq(',', settings[idx_frequency],1);
  display_add_xy(4,0);

  //mode
  display_print_str(modes[settings[idx_mode]],1);

  //signal strength dBm
  display_print_num("% 4ddBm", (int)power_dBm, 1, style_right);
}

int ui::dBm_to_S(float power_dBm) {
  int power_s = floorf((power_dBm-S0)/6.0f);
  if(power_dBm >= S9) power_s = floorf((power_dBm-S9)/10.0f)+9;
  if(power_s < 0) power_s = 0;
  if(power_s > 12) power_s = 12;
  return (power_s);
}

void ui::log_spectrum(float *min, float *max)
{
  *min=log10f(spectrum[0] + FLT_MIN);
  *max=log10f(spectrum[0] + FLT_MIN);

  for (uint16_t x = 0; x < 128; x++)
  {
    spectrum[x] = log10f(spectrum[x] + FLT_MIN);
    if (spectrum[x] < *min)
    {
      *min = spectrum[x];
    }
    if (spectrum[x] > *max)
    {
      *max = spectrum[x];
    }
  }
}

void ui::draw_h_tick_marks(uint16_t startY)
{
  // tick marks at startY
  ssd1306_draw_line(&disp, 0, startY + 2, 127, startY + 2, 1);

  ssd1306_draw_line(&disp, 0, startY, 0, startY, 1);
  ssd1306_draw_line(&disp, 64, startY, 64, startY, 1);
  ssd1306_draw_line(&disp, 127, startY, 127, startY, 1);

  ssd1306_draw_line(&disp, 32, startY + 1, 32, startY + 3, 1);
  ssd1306_draw_line(&disp, 96, startY + 1, 96, startY + 3, 1);
}

////////////////////////////////////////////////////////////////////////////////
// Home page status display
////////////////////////////////////////////////////////////////////////////////
void ui::renderpage_fun(bool view_changed, rx_status & status, rx & receiver)
{
  static int degrees = 0;
  static int xm, ym;

  if (degrees == 0) {
    xm = rand()%10+1;
    ym = rand()%10;
  }
  if (view_changed) {
    xm = ym = 5;
    degrees = 0;
  }
  display_clear();
  ssd1306_bmp_show_image(&disp, crystal, sizeof(crystal));
  ssd1306_scroll_screen(&disp, 40*cos(xm*M_PI*degrees/180), 20*sin(ym*M_PI*degrees/180));
  display_show();
  if ((degrees+=3) >=360) degrees = 0;
}


////////////////////////////////////////////////////////////////////////////////
// Paints the spectrum from startY to bottom of screen
////////////////////////////////////////////////////////////////////////////////
void ui::draw_spectrum(uint16_t startY, rx & receiver)
{
  float min;
  float max;

  static float min_avg = log10f(FLT_MIN);
  static float max_avg = log10f(FLT_MAX);

  //Display spectrum capture
  receiver.get_spectrum(spectrum);
  log_spectrum(&min, &max);

  draw_h_tick_marks(startY);

  min_avg += (min - min_avg) / 15.0f;
  max_avg += (max - max_avg) / 15.0f;
  const float range = max_avg - min_avg;

#define MAX_HEIGHT (64-startY-3)
  const float scale = (MAX_HEIGHT-0.0f) / range;

  //plot
  for(uint16_t x=0; x<128; x++)
  {
    int16_t y = scale*(spectrum[x]-min_avg);
    if(y < 0) y=0;
    if(y > MAX_HEIGHT) y=MAX_HEIGHT;
    ssd1306_draw_line(&disp, x, 63-y, x, 63, 1);
  }

  const float tick =  find_nearest_tick(range / 4.0f);
  const float min_r = roundf(min_avg / tick) * tick;

  const int16_t start = roundf(scale * (min_r - min_avg));
  const int16_t stop = roundf(scale * range);
  const int16_t step = roundf(scale * tick);

  for (int16_t y = start; y < stop; y += step)
  {
    for (uint8_t x = 0; x < 128; x += 4)
    {
      ssd1306_draw_line(&disp, x, 63 - y, x + 1, 63 - y, 2);
    }
  }
}

void ui::draw_waterfall(uint16_t starty, rx & receiver)
{
  float min;
  float max;

  static int8_t tmp_line[WATERFALL_WIDTH];
  static int8_t curr_line[WATERFALL_WIDTH];

  // Move waterfall down to  make room for the new line
  ssd1306_scroll_screen(&disp, 0, 1);

  receiver.get_spectrum(spectrum);
  log_spectrum(&min, &max);

  const float scale = ((float)WATERFALL_MAX_VALUE)/(max-min);
  int16_t err = 0;

  for(uint16_t x=0; x<WATERFALL_WIDTH; x++)
  {
      int16_t y = scale*(spectrum[x]-min);
      if(y < 0) y=0;
      if(y > WATERFALL_MAX_VALUE) y=WATERFALL_MAX_VALUE - 1;
      curr_line[x] = y + tmp_line[x];
      tmp_line[x] = 0;
  }

  for(uint16_t x=0; x<WATERFALL_WIDTH; x++)
  {
      // Simple Floyd-Steinberg dithering
      if(curr_line[x] > ((WATERFALL_MAX_VALUE + 1) / 2))
      {
        ssd1306_draw_pixel(&disp, x, starty + 3, 1);
        err = curr_line[x] - WATERFALL_MAX_VALUE;
      } else {
        ssd1306_draw_pixel(&disp, x, starty + 3, 0);
        err = curr_line[x] - 0;
      }

      if(x < (WATERFALL_WIDTH - 1))
      {
        curr_line[x + 1] += 7 * err / 16;
        tmp_line[x + 1] += err / 16;
      }
      tmp_line[x] += 5 * err / 16;
      if(x > 0)
      {
        tmp_line[x - 1] += 3 * err / 16;
      }
  }

  draw_h_tick_marks(starty);

}

////////////////////////////////////////////////////////////////////////////////
// Generic Menu Options
////////////////////////////////////////////////////////////////////////////////

void ui::print_enum_option(const char options[], uint8_t option){
#define MAX_OPTS 32
  char *splits[MAX_OPTS];
  int num_splits;
  char *new_options;

  new_options = (char*)malloc(strlen(options)+1);
  strcpy (new_options, options);

  splits[0] = strtok(new_options, "#");
  for ( num_splits = 1; num_splits < MAX_OPTS; num_splits++) {
          splits[num_splits] = strtok(NULL, "#");
          if (!splits[num_splits]) break;
  }

  if ( (num_splits==2) && (strlen(splits[0])+strlen(splits[1]))*12 < 128) {
    display_print_str(splits[0],2, (option==0) ? style_reverse : 0);
    display_print_str(" ");
    display_print_str(splits[1],2, style_right|((option==1) ? style_reverse : 0));
  } else {
    // default to 1st option if invalid
    if (option > num_splits) option = 0;
    display_print_str(splits[option], 2, style_centered);
  }
}

uint32_t ui::bit_entry(const char title[], const char options[], uint8_t bit_position, uint32_t *value)
{
    uint32_t bit = (*value >> bit_position) & 1;
    uint32_t return_value = enumerate_entry(title, options, &bit);
    if(bit)
    {
     *value |= (1 << bit_position);
    } else {
     *value &= ~(1 << bit_position);
    }
    return return_value;

}

//choose from an enumerate list of settings
uint32_t ui::menu_entry(const char title[], const char options[], uint32_t *value)
{
  int32_t select=*value;
  bool draw_once = true;
  uint32_t max = 0;
  for (size_t i=0; i<strlen(options); i++) {
    if (options[i] == '#') max++;
  }
  // workaround for accidental last # oissions
  if (options[strlen(options)-1] != '#') max++;
  if (max > 0) max--;

  while(1){
    if(encoder_control(&select, 0, max)!=0 || draw_once)
    {
      //print selected menu item
      draw_once = false;
      display_clear();
      display_print_str(title, 2, style_centered);
      display_draw_separator(18,3);
      display_linen(4);
      print_enum_option(options, select);
      display_show();
    }

    event_t ev = event_get();
    //select menu item
    if((ev.tag == ev_button_menu_press) || (ev.tag == ev_button_push_press)){
      *value = select;
      return 1;
    }

    //cancel
    if(ev.tag == ev_button_back_press){
      return 0;
    }
  }
}

//choose from an enumerate list of settings
uint32_t ui::enumerate_entry(const char title[], const char options[], uint32_t *value)
{
  int32_t select=*value;
  bool draw_once = true;
  uint32_t max = 0;
  for (size_t i=0; i<strlen(options); i++) {
    if (options[i] == '#') max++;
  }
  // workaround for accidental last # oissions
  if (options[strlen(options)-1] != '#') max++;
  if (max > 0) max--;

  while(1){
    if(encoder_control(&select, 0, max)!=0 || draw_once)
    {
      //print selected menu item
      draw_once = false;
      display_clear();
      display_print_str(title, 2, style_centered);
      display_draw_separator(40,1);
      display_linen(6);
      print_enum_option(options, select);
      display_show();
    }

    event_t ev = event_get();
    //select menu item
    if((ev.tag == ev_button_menu_press) || (ev.tag == ev_button_push_press)){
      *value = select;
      return 1;
    }

    //cancel
    if(ev.tag == ev_button_back_press){
      return 0;
    }
  }
}

//select a number in a range
int16_t ui::number_entry(const char title[], const char format[], int16_t min, int16_t max, int16_t multiple, uint32_t *value)
{
  int32_t select=*value/multiple;
  bool draw_once = true;
  while(1){
    if(encoder_control(&select, min, max)!=0 || draw_once)
    {
      //print selected menu item
      draw_once = false;
      display_clear();
      display_print_str(title, 2, style_centered);
      display_draw_separator(40,1);
      display_linen(6);
      display_print_num(format, select*multiple, 2, style_centered);
      display_show();
    }

    event_t ev = event_get();

    //select menu item
    if((ev.tag == ev_button_menu_press) || (ev.tag == ev_button_push_press)){
      *value = select*multiple;
      return 1;
    }

    //cancel
    if(ev.tag == ev_button_back_press){
      return 0;
    }
  }
}

//Apply settings
void ui::apply_settings(bool suspend)
{
  receiver.access(true);
  settings_to_apply.tuned_frequency_Hz = settings[idx_frequency];
  settings_to_apply.agc_speed = settings[idx_agc_speed];
  settings_to_apply.enable_auto_notch = settings[idx_rx_features] >> flag_enable_auto_notch & 1;
  settings_to_apply.mode = settings[idx_mode];
  settings_to_apply.volume = settings[idx_volume];
  settings_to_apply.squelch = settings[idx_squelch];
  settings_to_apply.step_Hz = step_sizes[settings[idx_step]];
  settings_to_apply.cw_sidetone_Hz = settings[idx_cw_sidetone];
  settings_to_apply.gain_cal = settings[idx_gain_cal];
  settings_to_apply.suspend = suspend;
  settings_to_apply.swap_iq = (settings[idx_hw_setup] >> flag_swap_iq) & 1;
  settings_to_apply.bandwidth = settings[idx_bandwidth];
  settings_to_apply.oled_contrast = settings[idx_oled_contrast];
  receiver.release();
}

//remember settings across power cycles
void ui::autosave()
{
  //The flash endurance may not be more than 100000 erase cycles.
  //Cycle through 512 locations, only erasing the flash when they have all been used.
  //This should give an endurance of 51,200,000 cycles.

  //find the next unused channel, an unused channel will be 0xffffffff
  uint16_t empty_channel = 0;
  bool empty_channel_found = false;
  for(uint16_t i=0; i<512; i++)
  {
    if(autosave_memory[i][0] == 0xffffffff)
    {
      empty_channel = i;
      empty_channel_found = true;
      break;
    } 
  }

  //check whether data differs
  if(empty_channel > 0)
  {
    bool difference_found = false;
    for(uint8_t i=0; i<16; i++){
      if(autosave_memory[empty_channel - 1][i] != settings[i])
      {
        difference_found = true;
        break;
      }
    }
    //data hasn't changed, no need to save
    if(!difference_found)
    {
      return;
    }
  }

  //if there are no free channels, erase all the pages
  if(!empty_channel_found)
  {
    const uint32_t address = (uint32_t)&(autosave_memory[0]);
    const uint32_t flash_address = address - XIP_BASE; 
    //!!! PICO is **very** fussy about flash erasing, there must be no code running in flash.  !!!
    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    apply_settings(true);                                //suspend rx to disable all DMA transfers
    WAIT_100MS                                           //wait for suspension to take effect
    multicore_lockout_start_blocking();                  //halt the second core
    const uint32_t ints = save_and_disable_interrupts(); //disable all interrupts

    //safe to erase flash here
    //--------------------------------------------------------------------------------------------
    flash_range_erase(flash_address, sizeof(int)*16*512);
    //--------------------------------------------------------------------------------------------

    restore_interrupts (ints);                           //restore interrupts
    multicore_lockout_end_blocking();                    //restart the second core
    apply_settings(false);                               //resume rx operation
    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    //!!! Normal operation resumed
  }

  //work out which flash sector the channel sits in.
  const uint32_t num_channels_per_sector = FLASH_SECTOR_SIZE/(sizeof(int)*chan_size);
  const uint32_t first_channel_in_sector = num_channels_per_sector * (empty_channel/num_channels_per_sector);
  const uint32_t channel_offset_in_sector = empty_channel%num_channels_per_sector;

  //copy sector to RAM
  static uint32_t sector_copy[num_channels_per_sector][chan_size];
  for(uint16_t channel=0; channel<num_channels_per_sector; channel++)
  {
    for(uint16_t location=0; location<chan_size; location++)
    {
      if(channel+first_channel_in_sector < num_chans)
      {
        sector_copy[channel][location] = autosave_memory[channel+first_channel_in_sector][location];
      }
    }
  }
    
  //modify the selected channel
  for(uint8_t i=0; i<16; i++)
  {
    sector_copy[channel_offset_in_sector][i] = settings[i];
  }

  //write sector to flash
  const uint32_t address = (uint32_t)&(autosave_memory[first_channel_in_sector]);
  const uint32_t flash_address = address - XIP_BASE; 

  //!!! PICO is **very** fussy about flash erasing, there must be no code running in flash.  !!!
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  apply_settings(true);                                //suspend rx to disable all DMA transfers
  WAIT_100MS                                           //wait for suspension to take effect
  multicore_lockout_start_blocking();                  //halt the second core
  const uint32_t ints = save_and_disable_interrupts(); //disable all interrupts

  //safe to erase flash here
  //--------------------------------------------------------------------------------------------
  flash_range_program(flash_address, (const uint8_t*)&sector_copy, FLASH_SECTOR_SIZE);
  //--------------------------------------------------------------------------------------------

  restore_interrupts (ints);                           //restore interrupts
  multicore_lockout_end_blocking();                    //restart the second core
  apply_settings(false);                               //resume rx operation
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  //!!! Normal operation resumed

}

//remember settings across power cycles
void ui::autorestore()
{

  //find the next unused channel, an unused channel will be 0xffffffff
  uint16_t empty_channel = 0;
  bool empty_channel_found = false;
  for(uint16_t i=0; i<512; i++)
  {
    if(autosave_memory[i][0] == 0xffffffff)
    {
      empty_channel = i;
      empty_channel_found = true;
      break;
    } 
  }

  uint16_t last_channel_written = 255;
  if(empty_channel > 0) last_channel_written = empty_channel - 1;
  if(!empty_channel_found) last_channel_written = 255;
  for(uint8_t i=0; i<16; i++){
    settings[i] = autosave_memory[last_channel_written][i];
  }

  apply_settings(false);
  uint8_t display_timeout_setting = (settings[idx_hw_setup] & mask_display_timeout) >> flag_display_timeout;
  display_timer = timeout_lookup[display_timeout_setting];
  ssd1306_flip(&disp, (settings[idx_hw_setup] >> flag_flip_oled) & 1);
  ssd1306_type(&disp, (settings[idx_hw_setup] >> flag_oled_type) & 1);
  ssd1306_contrast(&disp, 17 * settings[idx_oled_contrast]);

}

//Upload memories via USB interface
bool ui::upload_memory()
{
      display_clear();
      display_print_str("Ready for\nmemories",2, style_centered);
      display_show();

      uint8_t progress_ctr=0;

      //work out which flash sector the channel sits in.
      const uint32_t num_channels_per_sector = FLASH_SECTOR_SIZE/(sizeof(int)*chan_size);

      //copy sector to RAM
      bool done = false;
      const uint32_t num_sectors = num_chans/num_channels_per_sector;
      for(uint8_t sector = 0; sector < num_sectors; sector++)
      {

        const uint32_t first_channel_in_sector = num_channels_per_sector * sector;
        static uint32_t sector_copy[num_channels_per_sector][chan_size];
        for(uint16_t channel=0; channel<num_channels_per_sector; channel++)
        {
          for(uint16_t location=0; location<chan_size; location++)
          {
            if(!done)
            {
              printf("sector %u channel %u location %u>\n", sector, channel, location);
              char line [256];
              uint32_t data;
              fgets(line, 256, stdin);
              if(line[0] == 'q' || line[0] == 'Q')
              {
                sector_copy[channel][location] = 0xffffffffu;
                done = true;
              }
              if (sscanf(line, " %lx", &data))
              {
                sector_copy[channel][location] = data;
              }
            }
            else
            {
              sector_copy[channel][location] = 0xffffffffu;
            }
          }
        }
        
        // show some progress
        ssd1306_invert( &disp, 0x1 & (++progress_ctr));

        //write sector to flash
        const uint32_t address = (uint32_t)&(radio_memory[first_channel_in_sector]);
        const uint32_t flash_address = address - XIP_BASE; 

        //!!! PICO is **very** fussy about flash erasing, there must be no code running in flash.  !!!
        //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        apply_settings(true);                                //suspend rx to disable all DMA transfers
        WAIT_100MS                                           //wait for suspension to take effect
        multicore_lockout_start_blocking();                  //halt the second core
        const uint32_t ints = save_and_disable_interrupts(); //disable all interrupts

        //safe to erase flash here
        //--------------------------------------------------------------------------------------------
        flash_range_erase(flash_address, FLASH_SECTOR_SIZE);
        flash_range_program(flash_address, (const uint8_t*)&sector_copy, FLASH_SECTOR_SIZE);
        //--------------------------------------------------------------------------------------------

        restore_interrupts (ints);                           //restore interrupts
        multicore_lockout_end_blocking();                    //restart the second core
        apply_settings(false);                               //resume rx operation
        //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        //!!! Normal operation resumed

      }

      return false;
}

//save current settings to memory
bool ui::memory_store()
{

  //encoder loops through memories
  uint32_t min = 0;
  uint32_t max = num_chans-1;
  // grab last selected memory
  int32_t select=last_select;
  char name[17];

  bool draw_once = true;
  while(1){
    if(encoder_control(&select, min, max)!=0 || draw_once)
    {

      get_memory_name(name, select, false);
      //print selected menu item
      draw_once = false;
      display_clear();
      display_print_str("Store");
      display_print_num(" %03i ", select, 1, style_centered);
      display_print_str("\n", 1);
      // strip trailing spaces 
      char ss_name[17];
      strncpy(ss_name, name, 17);
      for (int i=15; i>=0; i--) {
        if (ss_name[i] != ' ') break;
        ss_name[i] = 0;
      }
      if (12*strlen(ss_name) > 128) {
        display_add_xy(0,4);
        display_print_str(ss_name,1,style_nowrap|style_centered);
      } else {
        display_print_str(ss_name,2,style_nowrap|style_centered);
      }

      display_show();
    }

    event_t ev = event_get();

    //select menu item
    if(ev.tag == ev_button_menu_press){

      //work out which flash sector the channel sits in.
      const uint32_t num_channels_per_sector = FLASH_SECTOR_SIZE/(sizeof(int)*chan_size);
      const uint32_t first_channel_in_sector = num_channels_per_sector * (select/num_channels_per_sector);
      const uint32_t channel_offset_in_sector = select%num_channels_per_sector;

      //copy sector to RAM
      static uint32_t sector_copy[num_channels_per_sector][chan_size];
      for(uint16_t channel=0; channel<num_channels_per_sector; channel++)
      {
        for(uint16_t location=0; location<chan_size; location++)
        {
          if(channel+first_channel_in_sector < num_chans)
          {
            sector_copy[channel][location] = radio_memory[channel+first_channel_in_sector][location];
          }
        }
      }

      // print the top row
      display_clear();

      display_print_str("Store");
      display_print_num(" %03i ", select, 1, style_centered);
      display_print_str(modes[settings[idx_mode]],1,style_right);
      display_print_str("\n", 1);

      //modify the selected channel name
      int retval = string_entry(name);
      if(retval == 0) return false;
      if(retval == 2) // delete record, mark as inactive
      {
              name[12] = 0xffu; name[13] = 0xffu;
              name[14] = 0xffu; name[15] = 0xffu;
      }
      // pack string into uint32 array
      for(uint8_t lw=0; lw<4; lw++)
      {
        sector_copy[channel_offset_in_sector][lw+6] = (name[lw*4+0] << 24 | name[lw*4+1] << 16 | name[lw*4+2] << 8 | name[lw*4+3]);
      }
      for(uint8_t i=0; i<settings_to_store; i++){
        sector_copy[channel_offset_in_sector][i] = settings[i];
      }

      //write sector to flash
      const uint32_t address = (uint32_t)&(radio_memory[first_channel_in_sector]);
      const uint32_t flash_address = address - XIP_BASE; 

      //!!! PICO is **very** fussy about flash erasing, there must be no code running in flash.  !!!
      //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
      apply_settings(true);                                //suspend rx to disable all DMA transfers
		  WAIT_100MS                                           //wait for suspension to take effect
      multicore_lockout_start_blocking();                  //halt the second core
      const uint32_t ints = save_and_disable_interrupts(); //disable all interrupts

      //safe to erase flash here
      //--------------------------------------------------------------------------------------------
      flash_range_erase(flash_address, FLASH_SECTOR_SIZE);
      flash_range_program(flash_address, (const uint8_t*)&sector_copy, FLASH_SECTOR_SIZE);
      //--------------------------------------------------------------------------------------------

      restore_interrupts (ints);                           //restore interrupts
      multicore_lockout_end_blocking();                    //restart the second core
      apply_settings(false);                               //resume rx operation
      //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
      //!!! Normal operation resumed

      ssd1306_invert( &disp, 0);
      return false;
    }

    //cancel
    if(ev.tag == ev_button_back_press){
      return false;
    }
  }
}

//load a channel from memory
bool ui::memory_recall(bool do_scanning)
{

  //encoder loops through memories
  int32_t min = 0;
  int32_t max = num_chans-1;
  // grab last selected memory
  int32_t select=last_select;
  char name[17];
  bool draw_once = true;

  int scan_speed = 0;
  uint32_t last_time = 0;
  uint32_t now_time = 0;

  int32_t pos_change = 0;
  float power_dBm;
  float last_power_dBm = FLT_MAX;
  int8_t power_s = 0;

  //remember where we were incase we need to cancel
  uint32_t stored_settings[settings_to_store];
  for(uint8_t i=0; i<settings_to_store; i++){
    stored_settings[i] = settings[i];
  }

  while(1){
    // grab power
    receiver.access(false);
    power_dBm = status.signal_strength_dBm;
    receiver.release();
    if (power_dBm != last_power_dBm) {
      //signal strength as an int 0..12
      power_s = dBm_to_S(power_dBm);
      draw_once = true;
      last_power_dBm = power_dBm;
    }

    if (do_scanning) {
      pos_change = get_encoder_change();
      if ( pos_change > 0 ) if(++scan_speed>4) scan_speed=4;
      if ( pos_change < 0 ) if(--scan_speed<-4) scan_speed=-4;

      now_time = to_ms_since_boot(get_absolute_time());
      if ((now_time - last_time) > 1000/(unsigned)abs(scan_speed)) {
        last_time = now_time;
        pos_change = scan_speed;
        if (pos_change > 0) if (++select > max) select = min;
        if (pos_change < 0) if (--select < min) select = max;
      }
    } else {
      pos_change = encoder_control(&select, min, max);;
    }

    if( pos_change != 0 || draw_once) {

      if (radio_memory[select][9] == 0xffffffff) {
        if (pos_change < 0) { // search backwards up to 512 times
          for (unsigned int i=0; i<num_chans; i++) {
            if (--select < min) select = max;
            if(radio_memory[select][9] != 0xffffffff)
              break;
          }
        } else if (pos_change > 0) {  // forwards
          for (unsigned int i=0; i<num_chans; i++) {
            if (++select > max) select = min;
            if(radio_memory[select][9] != 0xffffffff)
              break;
          }
        }
      }

      get_memory_name(name, select, true);

      //(temporarily) apply lodaed settings to RX
      for(uint8_t i=0; i<settings_to_store; i++){
        settings[i] = radio_memory[select][i];
      }
      apply_settings(false);

      //print selected menu item
      draw_once = false;
      display_clear();
      if (do_scanning) {
        display_print_str("Scanner");
      } else {
        display_print_str("Recall");
      }
      display_print_num(" %03i ", select, 1, style_centered);

      const char* mode_ptr = modes[radio_memory[select][idx_mode]];
      display_set_xy(128-6*strlen(mode_ptr)-8, display_get_y());
      display_print_str(mode_ptr,1);

      display_print_str("\n", 1);
      if (12*strlen(name) > 128) {
        display_add_xy(0,4);
        display_print_str(name,1,style_nowrap);
      } else {
        display_print_str(name,2,style_nowrap);
      }

      //draw frequency
      display_set_xy(0,27);
      display_print_freq('.', radio_memory[select][idx_frequency], 2);
      display_print_str("\n",2);

      if (do_scanning) {
        // show scanning speed -4..0..+4
        // 120 pixels to play with
        #define box_sz (120/9)

        display_add_xy(0,4);
        for (int i=0; i<9; i++) {
          ssd1306_draw_rectangle(&disp, i*box_sz, display_get_y(), box_sz, box_sz, 1);
        }
        ssd1306_fill_rectangle(&disp, (scan_speed+4)*box_sz, display_get_y(), box_sz, box_sz, 1);
      } else {
        display_print_str("from:  ", 1);
        display_print_freq(',', radio_memory[select][idx_min_frequency], 1);
        display_print_str(" Hz\n",1);

        display_print_str("  To:  ", 1);
        display_print_freq(',', radio_memory[select][idx_max_frequency], 1);
        display_print_str(" Hz\n",1);
      }

      int bar_len = power_s*62/12;
// framed
//      ssd1306_draw_rectangle(&disp, 124, 0, 3, 63, 1);
//      ssd1306_fill_rectangle(&disp, 125, 63-bar_len, 2, bar_len+1, 1);

//solid
      ssd1306_fill_rectangle(&disp, 124, 63-bar_len, 3, bar_len+1, 1);

      display_show();
    }

    event_t ev = event_get();

    if(ev.tag == ev_button_push_press){
      if (do_scanning) {
        scan_speed=0;
      } else {
        last_select=min;
        return 1;
      }
    }

    if(ev.tag == ev_button_menu_press){
      last_select=select;
      return 1;
    }

    //cancel
    if(ev.tag == ev_button_back_press){
      //put things back how they were to start with
      for(uint8_t i=0; i<settings_to_store; i++){
        settings[i] = stored_settings[i];
      }
      apply_settings(false);
      return 0;
    }
  }
}

int ui::get_memory_name(char* name, int select, bool strip_spaces)
{
      if(radio_memory[select][9] != 0xffffffff)
      {
        //load name from memory
        name[0] = radio_memory[select][6] >> 24;
        name[1] = radio_memory[select][6] >> 16;
        name[2] = radio_memory[select][6] >> 8;
        name[3] = radio_memory[select][6];
        name[4] = radio_memory[select][7] >> 24;
        name[5] = radio_memory[select][7] >> 16;
        name[6] = radio_memory[select][7] >> 8;
        name[7] = radio_memory[select][7];
        name[8] = radio_memory[select][8] >> 24;
        name[9] = radio_memory[select][8] >> 16;
        name[10] = radio_memory[select][8] >> 8;
        name[11] = radio_memory[select][8];
        name[12] = radio_memory[select][9] >> 24;
        name[13] = radio_memory[select][9] >> 16;
        name[14] = radio_memory[select][9] >> 8;
        name[15] = radio_memory[select][9];
        name[16] = 0;
      } else {
        strcpy(name, "BLANK           ");
      }

      // strip trailing spaces
      if (strip_spaces) {
        for (int i=15; i>=0; i--) {
          if (name[i] != ' ') break;
          name[i] = 0;
        }
      }
      return (strlen(name));
}

////////////////////////////////////////////////////////////////////////////////
// String Entry (digit by digit)
////////////////////////////////////////////////////////////////////////////////
int ui::string_entry(char string[]){

  int32_t position=0;
  int32_t edit_mode = 0;

  bool draw_once = true;
  while(1){

    int32_t encoder_changed;
    if(edit_mode){
      //change the value of a digit 
      int32_t val = string[position];
      if     ('a' <= string[position] && string[position] <= 'z') val = string[position]-'a'+1;
      else if('A' <= string[position] && string[position] <= 'Z') val = string[position]-'A'+1;
      else if('0' <= string[position] && string[position] <= '9') val = string[position]-'0'+27;
      else if(string[position] == ' ') val = 0;
      encoder_changed = encoder_control(&val, 0, 36);
      if     (1 <= val  && val <= 26) string[position] = 'A' + val - 1;
      else if(27 <= val && val <= 36) string[position] = '0' + val - 27;
      else string[position] = ' ';
    } 
    else 
    {
      //change between chars
      encoder_changed = encoder_control(&position, 0, 19);
    }

    //if encoder changes, or screen hasn't been updated
    if(encoder_changed || draw_once)
    {
      draw_once = false;

      display_set_xy(0,9);
      display_clear_str(2,false);

      // compute starting point to scroll display
#define SCREEN_WIDTH 10
#define BUFFER_WIDTH 16
      int start = 0;
      if (position < SCREEN_WIDTH) start = 0;
      else if (position < BUFFER_WIDTH) start = position-(SCREEN_WIDTH-1);
      else start = (encoder_changed > 0) ? (BUFFER_WIDTH-SCREEN_WIDTH) : 0;

      //write preset name to lcd
      for(int i=start; i<16; i++) {
        if (!edit_mode && (i==position)) {
          display_print_char(string[i], 2, style_nowrap|style_reverse);
        } else {
          display_print_char(string[i], 2, style_nowrap );
        }
      }
      // print scroll bar
#define YP 25
#define LEN SCREEN_WIDTH*128/BUFFER_WIDTH
      ssd1306_draw_line(&disp, 0, YP+1, 127, YP+1, false);
      ssd1306_draw_line(&disp, start*8, YP+1, LEN+start*8, YP+1, true);
      ssd1306_draw_line(&disp, 0, YP, 0, YP+2, true);
      ssd1306_draw_line(&disp, 127, YP, 127, YP+2, true);

      display_draw_separator(40,1);
      display_linen(6);
      display_clear_str(2,false);
      if (position>=16) {
        display_set_xy(0, display_get_y());
        display_print_str(">",2,style_reverse);
        display_print_str("<",2,style_reverse|style_right);
      }
      display_set_xy(0, display_get_y());
      print_enum_option("OK#CLEAR#DELETE#EXIT#", position-16);

      display_show();
    }

    event_t ev = event_get();

    //select menu item
    if((ev.tag == ev_button_menu_press) || (ev.tag == ev_button_push_press))
    {
      draw_once = true;
	    edit_mode = !edit_mode;
	    if(position==16) return 1; //Yes
      if(position==17) {  // clear
        memset(string, ' ', strlen(string));
        edit_mode = false;
        position = 0;
      }
	    if(position==18) return 2; //delete
	    if(position==19) return 0; //No exit
	  }

    //cancel
    if(ev.tag == ev_button_back_press)
    {
      return 0;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// Frequency menu item (digit by digit)
////////////////////////////////////////////////////////////////////////////////
bool ui::frequency_entry(const char title[], uint32_t which_setting){

  int32_t digit=0;
  int32_t digits[8];
  int32_t i, digit_val;
  int32_t edit_mode = 0;
  unsigned frequency;

  //convert to binary representation
  frequency = settings[which_setting];
  digit_val = 10000000;
  for(i=0; i<8; i++){
      digits[i] = frequency / digit_val;
      frequency %= digit_val;
      digit_val /= 10;
  }

  bool draw_once = true;
  while(1){

    bool encoder_changed;
    if(edit_mode){
      //change the value of a digit 
      encoder_changed = encoder_control(&digits[digit], 0, 9);
    } 
    else 
    {
      //change between digits
      encoder_changed = encoder_control(&digit, 0, 9);
    }

    //if encoder changes, or screen hasn't been updated
    if(encoder_changed || draw_once)
    {
      draw_once = false;

      //write frequency to lcd
      display_clear();
      display_print_str(title,1);
      display_set_xy(4,9);
      for(i=0; i<8; i++)
      {
        if (!edit_mode && (i==digit)) {
          display_print_char(digits[i] + '0', 2, style_reverse);
        } else {
          display_print_char(digits[i] + '0', 2 );
        }
        if(i==1||i==4) display_print_char('.', 2 );
      }
      display_draw_separator(40,1);
      display_linen(6);
      print_enum_option(" OK #EXIT#", digit-8);
      display_show();
    }

    event_t ev = event_get();

    //select menu item
    if((ev.tag == ev_button_menu_press) || (ev.tag == ev_button_push_press))
    {
      draw_once = true;
	    edit_mode = !edit_mode;
      
	    if(digit==8) //Yes, Ok
      {
	      digit_val = 10000000;

        //convert back to a binary representation
        settings[which_setting] = 0;
        for(i=0; i<8; i++)
        {
	        settings[which_setting] += (digits[i] * digit_val);
		      digit_val /= 10;
		    }

        //sanity check the 3 frequencies
        //when manually changing to a frequency outside the current band, remove any band limits
        //which changing band limits, force frequency within
        if (settings[idx_max_frequency] < settings[idx_min_frequency]){
          // force them to be the same ?
          settings[idx_max_frequency] = settings[idx_min_frequency];
        }

        switch (which_setting) {
          case idx_frequency:
            if((settings[idx_frequency] > settings[idx_max_frequency]) || (settings[idx_frequency] < settings[idx_max_frequency]))
            {
              settings[idx_min_frequency] = 0;
              settings[idx_max_frequency] = 30000000;
            }
          case idx_max_frequency:
            if(settings[idx_frequency] > settings[idx_max_frequency])
            {
              settings[idx_frequency] = settings[idx_max_frequency];
            }
            break;
          case idx_min_frequency:
            if(settings[idx_frequency] < settings[idx_min_frequency])
            {
              settings[idx_frequency] = settings[idx_min_frequency];
            }
            break;
        }
		    return true;
	    }
	    if(digit==9) return 0; //No
	  }

    //cancel
    if(ev.tag == ev_button_back_press)
    {
      return false;
    }
  }
}

bool ui::display_timeout(bool encoder_change, event_t event)
{
    uint8_t display_timeout_setting = (settings[idx_hw_setup] & mask_display_timeout) >> flag_display_timeout;
    uint16_t display_timeout = timeout_lookup[display_timeout_setting];

    //A timeout value of zero means never time out
    if(!display_timeout) return true;

    //A button press causes timer to be reset to max value
    //and re-enables the display if it was previously off
    if(encoder_change || ((1UL << event.tag) & (ev_display_tmout_evset)))
    {
      if(!display_timer)
      {
        ssd1306_poweron(&disp);
        do
        {
          event = event_get();
        } while (((1UL << event.tag) & (ev_display_tmout_evset)));
        display_timer = display_timeout;
        return false;
      }
      display_timer = display_timeout;
      return true;
    }

    //if display is on, decrement the timer, once every 100ms
    if(display_timer)
    {
      --display_timer;
      //if a timeout occurs turn display off
      if(!display_timer)
      {
         ssd1306_poweroff(&disp);
         return false;
      }
      return true;
    }

    //at this point timer must be expired and display is off
    return false;
}

bool ui::configuration_menu()
{
  bool rx_settings_changed=false;
  uint32_t setting = 0;

  while (1) {
      event_t ev = event_get();
      if(ev.tag == ev_button_back_press){
        break;
      }

      if(!menu_entry("HW Config", "Display\nTimeout#Regulator\nMode#Reverse\nEncoder#Swap IQ#Gain Cal#Flip OLED#OLED Type#Display\nContrast#USB\nUpload#", &setting)) return 1;
      switch(setting)
      {
        case 0: 
          setting = (settings[idx_hw_setup] & mask_display_timeout) >> flag_display_timeout;
          rx_settings_changed |= enumerate_entry("Display\nTimeout", "Never#5 Sec#10 Sec#15 Sec#30 Sec#1 Min#2 Min#4 Min#", &setting);
          display_timer = timeout_lookup[setting];
          settings[idx_hw_setup] &=  ~mask_display_timeout;
          settings[idx_hw_setup] |=  setting << flag_display_timeout;
          break;

        case 1 : 
          enumerate_entry("PSU Mode", "FM#PWM#", &regmode);
          gpio_set_dir(23, GPIO_OUT);
          gpio_put(23, regmode);
          break;

        case 2 : 
          rx_settings_changed |= bit_entry("Reverse\nEncoder", "Off#On#", flag_reverse_encoder, &settings[idx_hw_setup]);
          break;

        case 3 : 
          rx_settings_changed |= bit_entry("Swap IQ", "Off#On#", flag_swap_iq, &settings[idx_hw_setup]);
          break;

        case 4: 
          rx_settings_changed |= number_entry("Gain Cal", "%idB", 1, 100, 1, &settings[idx_gain_cal]);
          break;

        case 5: 
          rx_settings_changed |= bit_entry("Flip OLED", "Off#On#", flag_flip_oled, &settings[idx_hw_setup]);
          ssd1306_flip(&disp, (settings[idx_hw_setup] >> flag_flip_oled) & 1);
          break;

        case 6: 
          rx_settings_changed |= bit_entry("OLED Type", "SSD1306#SH1106#", flag_oled_type, &settings[idx_hw_setup]);
          ssd1306_type(&disp, (settings[idx_hw_setup] >> flag_oled_type) & 1);
          break;

        case 7:
          rx_settings_changed |= number_entry("Display\nContrast", "%i", 0, 15, 1, &settings[idx_oled_contrast]);
          ssd1306_contrast(&disp, 17 * settings[idx_oled_contrast]);
          break;

        case 8: 
          setting = 0;
          enumerate_entry("USB Upload", "Back#Memory#Firmware#", &setting);
          if(setting==1) {
            upload_memory();
          } else if (setting == 2) {
            display_clear();
            display_print_str("Ready for\nfirmware",2, style_centered);
            display_show();
            reset_usb_boot(0,0);
          }
          break;
      }
  }

  return rx_settings_changed;

}

////////////////////////////////////////////////////////////////////////////////
// This is the startup animation
////////////////////////////////////////////////////////////////////////////////
bool ui::do_splash()
{
  static int step=0;
  if (step++ >= 20) {  // we're done
    step = 0;
    return true;
  }

  display_clear();
  ssd1306_bmp_show_image(&disp, crystal, 1086);

  int i=-1;
#if 0
// zoom in
       if (step <= 5) i=0;        // image for 3 tenths
  else if (step <= 7) i=step-5;
  else if (step <= 12) i=3;
  else if (step <= 18) i=step-7;

#else
// zoom out
       if (step <= 6) i=10-step;
  else if (step <= 11) i=3;
  else if (step <= 13) i=14-step;
  else if (step <= 18) i=0;

#endif

  if (i==0) {
    // do nothing, leave the bitmap
  } else if (i>0) {
    display_set_xy(0,(64-i*8)/2); // disp height - text height /2
    display_print_str("PicoRX",i,style_centered|style_nowrap|style_bordered);
  } else if (i==-1) {
    display_clear();
  }
  display_show();
  return false;

}

////////////////////////////////////////////////////////////////////////////////
// This is the main UI loop. Should get called about 10 times/second
////////////////////////////////////////////////////////////////////////////////
void ui::do_ui(event_t event)
{
    static bool rx_settings_changed = true;
    bool autosave_settings = false;
    uint32_t encoder_change = get_encoder_change();
    static bool maybe_changeview = false;
    static int current_view = 0;
    static bool view_changed = true;  // has the main view changed?
    static bool splash_done = false;

    if (!splash_done) {
      splash_done = do_splash();
      if ((button_state != idle) || (encoder_change)) splash_done=true;
    }

    //automatically switch off display after a period of inactivity
    if(!display_timeout(encoder_change, event)) return;

    // compute the complex button interactions on the home page
    // that will later be used for live freq changes and home page selection
    switch(button_state)
    {
      case idle:
        if(event.tag == ev_button_menu_press)
        {
          button_state = down;
          timeout = 100;
        } else if (event.tag == ev_button_back_press)
        {
          maybe_changeview = true;
          button_state = slow_mode;
        }
        break;
      case slow_mode:
        if (event.tag == ev_button_back_release)
        {
          button_state = idle;
          if (maybe_changeview == true) {
            current_view = (current_view+1) % NUM_VIEWS;
            view_changed = true;
            maybe_changeview = false;
          }
        } else if (event.tag == ev_button_menu_press)
        {
          button_state = very_fast_mode;
        }
        break;
      case down:
        if(encoder_change != 0 || (timeout-- == 0))
        {
          button_state = fast_mode;
        }
        else if(event.tag == ev_button_menu_release)
        {
          button_state = menu;
        }
        break;
      case fast_mode:
        if(event.tag == ev_button_menu_release)
        {
          button_state = idle;
        } else if(event.tag == ev_button_back_press)
        {
          button_state = very_fast_mode;
        }
        break;
      case very_fast_mode:
        if (event.tag == ev_button_back_release)
        {
          button_state = fast_mode;
        }
        else if (event.tag == ev_button_menu_release)
        {
          button_state = slow_mode;
        }
        break;
      case menu:
        button_state = idle;
        break;
    }

    //update frequency if encoder changes
    if(encoder_change != 0)
    {
      maybe_changeview = false;
      rx_settings_changed = true;

      frequency_autosave_pending = false;
      frequency_autosave_timer = 10u;

      switch (button_state)
      {
      case fast_mode:
        // fast if menu button held
        settings[idx_frequency] += encoder_change * step_sizes[settings[idx_step]] * 10;
        break;

      case very_fast_mode:
        // very fast if both buttons pressed
        settings[idx_frequency] += encoder_change * step_sizes[settings[idx_step]] * 100;
        break;

      case slow_mode:
        // slow if cancel button held
        settings[idx_frequency] += encoder_change * (step_sizes[settings[idx_step]] / 10);
        break;

      default:
      settings[idx_frequency] += encoder_change * step_sizes[settings[idx_step]];
        break;
      }

      if (settings[idx_frequency] > settings[idx_max_frequency])
          settings[idx_frequency] = settings[idx_min_frequency];

      if ((int)settings[idx_frequency] < (int)settings[idx_min_frequency])
          settings[idx_frequency] = settings[idx_max_frequency];
      
    }

    if(frequency_autosave_pending)
    {
      if(!frequency_autosave_timer)
      {
        frequency_autosave_pending = false;
      }
      else
      {
        frequency_autosave_timer--;
      }
    }


    //if button is pressed enter menu
    else if(button_state == menu)
    {
      view_changed = true;
      rx_settings_changed = top_menu(settings_to_apply);
      autosave_settings = rx_settings_changed;
    }
    else if(event.tag == ev_button_push_press)
    {
      view_changed = true;
      rx_settings_changed = memory_recall(false);
      autosave_settings = rx_settings_changed;
    }

    if(rx_settings_changed)
    {
      autosave();
      autosave_settings = rx_settings_changed;
    }

    if(autosave_settings)
    {
      receiver.access(true);
      settings_to_apply.tuned_frequency_Hz = settings[idx_frequency];
      settings_to_apply.agc_speed = settings[idx_agc_speed];
      settings_to_apply.enable_auto_notch = settings[idx_rx_features] >> flag_enable_auto_notch & 1;
      settings_to_apply.mode = settings[idx_mode];
      settings_to_apply.volume = settings[idx_volume];
      settings_to_apply.squelch = settings[idx_squelch];
      settings_to_apply.step_Hz = step_sizes[settings[idx_step]];
      settings_to_apply.cw_sidetone_Hz = settings[idx_cw_sidetone];
      settings_to_apply.bandwidth = settings[idx_bandwidth];
      settings_to_apply.gain_cal = settings[idx_gain_cal];
      receiver.release();
    }


    if (splash_done) {
      switch (current_view) {
        case 1: renderpage_bigspectrum(view_changed, status, receiver); break;
        case 2: renderpage_waterfall(view_changed, status, receiver); break;
        case 3: renderpage_bigtext(view_changed, status, receiver); break;
        case 4: renderpage_fun(view_changed, status, receiver); break;
        default: renderpage_original(view_changed, status, receiver); break;
      }
      view_changed = false;
    }
    rx_settings_changed = false;
}

// top level menu selection and launch
bool ui::top_menu(rx_settings & settings_to_apply)
{
  bool rx_settings_changed = false;
  uint32_t setting = 0;

  while (1)
  {
      event_t ev = event_get();
      if(ev.tag == ev_button_back_press){
        break;
      }
      if(!menu_entry("Menu", "Frequency#Recall#Store#Volume#Mode#AGC Speed#Bandwidth#Squelch#Auto Notch#Band Start#Band Stop#Frequency\nStep#CW Tone\nFrequency#HW Config#", &setting)) 
        return false;

      switch(setting)
      {
        case 0 : 
          rx_settings_changed |= frequency_entry("frequency", idx_frequency);
          break;

        case 1:
          // we quit menu if they selected something
          if (memory_recall(true)) return true;
          break;

        case 2:
          memory_store();
          break;

        case 3 : 
          rx_settings_changed |= number_entry("Volume", "%i", 0, 9, 1, &settings[idx_volume]);
          break;

        case 4 : 
          rx_settings_changed |= enumerate_entry("Mode", "AM#LSB#USB#FM#CW#", &settings[idx_mode]);
          break;

        case 5 :
          rx_settings_changed |= enumerate_entry("AGC Speed", "Fast#Normal#Slow#Very slow#", &settings[idx_agc_speed]);
          break;

        case 6 :
          rx_settings_changed |= enumerate_entry("Bandwidth", "V Narrow#Narrow#Normal#Wide#Very Wide#", &settings[idx_bandwidth]);
          break;

        case 7 :
          rx_settings_changed |= enumerate_entry("Squelch", "S0#S1#S2#S3#S4#S5#S6#S7#S8#S9#S9+10dB#S9+20dB#S9+30dB#", &settings[idx_squelch]);
          break;

        case 8 : 
          rx_settings_changed |= bit_entry("Auto Notch", "Off#On#", flag_enable_auto_notch, &settings[idx_rx_features]);
          break;

        case 9 : 
          rx_settings_changed |= frequency_entry("Band Start", idx_min_frequency);
          break;

        case 10 : 
          rx_settings_changed |= frequency_entry("Band Stop", idx_max_frequency);
          break;

        case 11 : 
          rx_settings_changed |= enumerate_entry("Frequency\nStep", "10Hz#50Hz#100Hz#1kHz#5kHz#10kHz#12.5kHz#25kHz#50kHz#100kHz#", &settings[idx_step]);
          settings[idx_frequency] -= settings[idx_frequency]%step_sizes[settings[idx_step]];
          break;

        case 12 : 
          rx_settings_changed |= number_entry("CW Tone\nFrequency", "%iHz", 1, 30, 100, &settings[idx_cw_sidetone]);
          break;

        case 13 : 
          rx_settings_changed |= configuration_menu();
          break;
      }
  }
  return rx_settings_changed;
}

ui::ui(rx_settings & settings_to_apply, rx_status & status, rx &receiver) : settings_to_apply(settings_to_apply), status(status), receiver(receiver)
{
  setup_display();
  setup_encoder();

  button_state = idle;
}
