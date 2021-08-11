/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android-base/stringprintf.h>
#include <batteryservice/BatteryService.h>
#include <cutils/klog.h>

#include "healthd_draw.h"

#define LOGE(x...) KLOG_ERROR("charger", x);
#define LOGV(x...) KLOG_DEBUG("charger", x);

HealthdDraw::HealthdDraw(animation* anim)
  : kSplitScreen(HEALTHD_DRAW_SPLIT_SCREEN),
    kSplitOffset(HEALTHD_DRAW_SPLIT_OFFSET) {
  gr_init();
  gr_font_size(gr_sys_font(), &char_width_, &char_height_);

  screen_width_ = gr_fb_width() / (kSplitScreen ? 2 : 1);
  screen_height_ = gr_fb_height();
 LOGE("screen_width_:%d,screen_height_:%d\n",screen_width_,screen_height_);
  int res;
  if (!anim->text_clock.font_file.empty() &&
      (res = gr_init_font(anim->text_clock.font_file.c_str(),
                          &anim->text_clock.font)) < 0) {
    LOGE("Could not load time font (%d)\n", res);
  }
  if (!anim->text_percent.font_file.empty() &&
      (res = gr_init_font(anim->text_percent.font_file.c_str(),
                          &anim->text_percent.font)) < 0) {
    LOGE("Could not load percent font (%d)\n", res);
  }
}

HealthdDraw::~HealthdDraw() {}

void HealthdDraw::redraw_screen(const animation* batt_anim,  bool charger_connected) {
  clear_screen();

  /* try to display *something* */
  if (batt_anim->cur_level < 0 || batt_anim->num_frames == 0)
    draw_unknown(batt_anim->surf_unknown);
  else{
    draw_battery(batt_anim,charger_connected);
    draw_power_percent(batt_anim);
    }
  gr_flip();
}

void HealthdDraw::blank_screen(bool blank) { gr_fb_blank(blank); }

void HealthdDraw::clear_screen(void) {
  gr_color(0, 0, 0, 255);
  gr_clear();
}

int HealthdDraw::draw_surface_centered(GRSurface* surface) {
  int w = gr_get_width(surface);
  int h = gr_get_height(surface);
  int x = (screen_width_ - w) / 2 + kSplitOffset;
  int y = (screen_height_ - h) / 2;

  LOGV("drawing surface %dx%d+%d+%d\n", w, h, x, y);
  gr_blit(surface, 0, 0, w, h, x, y);
  if (kSplitScreen) {
    x += screen_width_ - 2 * kSplitOffset;
    LOGV("drawing surface %dx%d+%d+%d\n", w, h, x, y);
    gr_blit(surface, 0, 0, w, h, x, y);
  }

  return y + h;
}

int HealthdDraw::draw_text(const GRFont* font, int x, int y, const char* str) {
  int str_len_px = gr_measure(font, str);

  if (x < 0) x = (screen_width_ - str_len_px) / 2;
  if (y < 0) y = (screen_height_ - char_height_) / 2;
  gr_text(font, x + kSplitOffset, y, str, false /* bold */);
  if (kSplitScreen)
    gr_text(font, x - kSplitOffset + screen_width_, y, str, false /* bold */);

  return y + char_height_;
}

void HealthdDraw::determine_xy(const animation::text_field& field,
                               const int length, int* x, int* y) {
  *x = field.pos_x;

  int str_len_px = length * field.font->char_width;
  if (field.pos_x == CENTER_VAL) {
    *x = (screen_width_ - str_len_px) / 2;
  } else if (field.pos_x >= 0) {
    *x = field.pos_x;
  } else {  // position from max edge
    *x = screen_width_ + field.pos_x - str_len_px - kSplitOffset;
  }

  *y = field.pos_y;

  if (field.pos_y == CENTER_VAL) {
    *y = (screen_height_ - field.font->char_height) / 2;
  } else if (field.pos_y >= 0) {
    *y = field.pos_y;
  } else {  // position from max edge
    *y = screen_height_ + field.pos_y - field.font->char_height;
  }
}

void HealthdDraw::draw_clock(const animation* anim) {
  static constexpr char CLOCK_FORMAT[] = "%H:%M";
  static constexpr int CLOCK_LENGTH = 6;

  const animation::text_field& field = anim->text_clock;

  if (field.font == nullptr || field.font->char_width == 0 ||
      field.font->char_height == 0)
    return;

  time_t rawtime;
  time(&rawtime);
  tm* time_info = localtime(&rawtime);

  char clock_str[CLOCK_LENGTH];
  size_t length = strftime(clock_str, CLOCK_LENGTH, CLOCK_FORMAT, time_info);
  if (length != CLOCK_LENGTH - 1) {
    LOGE("Could not format time\n");
    return;
  }

  int x, y;
  determine_xy(field, length, &x, &y);

  LOGV("drawing clock %s %d %d\n", clock_str, x, y);
  gr_color(field.color_r, field.color_g, field.color_b, field.color_a);
  draw_text(field.font, x, y, clock_str);
}

void HealthdDraw::draw_surface_percent_number(GRSurface* surface,int basecount,int offect){
    int w;
    int h;
    int x;
    int y;

    w = gr_get_width(surface);
    h = gr_get_height(surface);
    x = (gr_fb_width() -160) / 2 - w -30;
    y = basecount - (h+3) * (offect+1) ;
    LOGE("drawing number %d + %d + %d + %d   display:%d,display:%d\n", w, h, x, y,gr_fb_width(),gr_fb_height());
    gr_blit(surface, 0, 0, w, h, x, y);
}
int HealthdDraw::draw_surface_percent_symbol(GRSurface* surface,int offect){
    int w;
    int h;
    int x;
    int y;
    w = gr_get_width(surface);
    h = gr_get_height(surface);
    x = (gr_fb_width() -160) / 2 - w -30;
    y = (gr_fb_height() - h) / 2 + h * offect; 
    LOGE("drawing symbol %d + %d + %d + %d\n", w, h, x, y);
    gr_blit(surface, 0, 0, w, h, x, y);
    return y ;
}
void HealthdDraw::draw_percent(GRSurface *surface,int value,int basecount,int offect) {
   char str[50] = {0};
    if(surface != NULL)
        res_free_surface(surface);
    sprintf(str,"charger/charging_text_%d",value);
    LOGE("str=%s\n",str);
    int ret = res_create_display_surface(str, &surface);
    if (ret < 0) {
        LOGE("Cannot load image\n");
        surface = NULL;
    }
    draw_surface_percent_number(surface,basecount,offect);
}
void HealthdDraw::select(const animation* batt_anim, int x, int y, int z){
    int basecount;
    int offect = 0;

    if(z != 0)
        offect = 2;
    else if(y != 0)
        offect = 1;

    basecount = draw_surface_percent_symbol(batt_anim->surf_percent, offect);
    draw_percent(batt_anim->surf_one, x, basecount, 0);
    if(z == 0 && y != 0)
    {
        draw_percent(batt_anim->surf_ten, y, basecount, offect);
    }
    if(z)
    {
        draw_percent(batt_anim->surf_ten, y, basecount, offect - 1);
        draw_percent(batt_anim->surf_hundred, z, basecount, offect);
    }

}
void HealthdDraw::draw_power_percent(const animation* batt_anim){
    int value ;
    value = batt_anim->cur_level ;

    int x, y, z;
    x = value % 10;
    y = (value / 10) % 10;
    z = (value / 100) % 10;
    select(batt_anim, x, y, z);
}
void HealthdDraw::draw_battery(const animation* anim, bool charger_connected) {
	animation::frame& frame = anim->frames[anim->cur_frame];
	int value ;
	value = anim->cur_level ;
	LOGE("batt_prop->batteryLevel %d \n",value);
	unsigned  int m_setp =0;
	if ((value>=0)&&(value <100))
		m_setp = value/10;
	if(value == 100){
		draw_surface_centered(anim->surf_full);
		return;
	}
	if(charger_connected){
		if (anim->num_frames != 0) {
			draw_surface_centered(frame.surface);
			LOGE("draw high battery level %d \n",value);
		}
		LOGE("drawing frame #%d time=%d\n",anim->cur_frame, frame.disp_time);
	}
	else {
			draw_surface_centered(anim->frames[m_setp].plugout_surface);
			LOGE("draw plugout battery level: %d,m_setp :%d \n",value,m_setp);
	}
}

void HealthdDraw::draw_unknown(GRSurface* surf_unknown) {
  int y;
  if (surf_unknown) {
    draw_surface_centered(surf_unknown);
  } else {
    gr_color(0xa4, 0xc6, 0x39, 255);
    y = draw_text(gr_sys_font(), -1, -1, "Charging!");
    draw_text(gr_sys_font(), -1, y + 25, "?\?/100");
  }
}
