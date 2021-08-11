/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein is
 * confidential and proprietary to MediaTek Inc. and/or its licensors. Without
 * the prior written permission of MediaTek inc. and/or its licensors, any
 * reproduction, modification, use or disclosure of MediaTek Software, and
 * information contained herein, in whole or in part, shall be strictly
 * prohibited.
 * 
 * MediaTek Inc. (C) 2010. All rights reserved.
 * 
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER
 * ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL
 * WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR
 * NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH
 * RESPECT TO THE SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY,
 * INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES
 * TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO.
 * RECEIVER EXPRESSLY ACKNOWLEDGES THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO
 * OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES CONTAINED IN MEDIATEK
 * SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE
 * RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S
 * ENTIRE AND CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE
 * RELEASED HEREUNDER WILL BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE
 * MEDIATEK SOFTWARE AT ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE
 * CHARGE PAID BY RECEIVER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek
 * Software") have been modified by MediaTek Inc. All revisions are subject to
 * any receiver's applicable license agreements with MediaTek Inc.
 */

#include <stdio.h>
#include <utils/Log.h>
#include <cutils/klog.h>

#include <android/hardware/light/2.0/ILight.h>

using ::android::hardware::light::V2_0::Brightness;
using ::android::hardware::light::V2_0::Flash;
using ::android::hardware::light::V2_0::ILight;
using ::android::hardware::light::V2_0::LightState;
using ::android::hardware::light::V2_0::Status;
using ::android::hardware::light::V2_0::Type;
using ::android::sp;



enum {
    LIGHTS_INIT        = 0,
    LIGHTS_CHGFULL     = 1,
    LIGHTS_CHGON       = 2,
    LIGHTS_CHGEXIT     = 3,
    LIGHTS_FUNC_NUM    = 4,
};

enum {
    LIGHTS_STATE_UNKNOWN = 0,
    LIGHTS_STATE_CHGFULL = 1,
    LIGHTS_STATE_CHGON   = 2,
    LIGHTS_STATE_EXIT    = 3,
};
enum {
    CONTROL_UNKNOWN        = 0,
    CONTROL_ON             = 1,
    CONTROL_OFF            = 2,
};

static int led_green_isOn = CONTROL_UNKNOWN;
static int led_red_isOn = CONTROL_UNKNOWN;
static int nChargingFull = CONTROL_UNKNOWN;
static int nChargingRemoved = CONTROL_UNKNOWN;

sp<ILight> g_light;

pthread_mutex_t lights_mutex;
#define LOGE(x...) KLOG_ERROR("charger", x);

void light_init(void)
{
	g_light = ILight::getService();
	if (g_light == nullptr) {
		LOGE("Could not retrieve light service\n");
		return;
	}
}

void set_light_brightness(Type light_type, LightState level)
{
	if (g_light == nullptr) {
		LOGE("No light service, cannot set brightness\n");
		return;
	}

	Status ret = g_light->setLight(light_type, level);
	if (ret != Status::SUCCESS){
		LOGE("Failed to set light to 0x%x for type %d, ret=%d\n",level.color, light_type, ret);
	}
	else{
		LOGE("set light to 0x%x for type %d successfully\n",level.color, light_type);
	}
}

void start_red_led(int skew)
{
	if (led_red_isOn == CONTROL_ON)
		return;
	led_red_isOn = CONTROL_ON;

	if (skew > 255) skew = 255;
	else if (skew < 0) skew = 0;

	LightState brightness = {
		.color = 0xff000000 | (skew << 16), .flashMode = Flash::NONE,
		.brightnessMode = Brightness::USER,
	};
	set_light_brightness(Type::BATTERY, brightness);
}

void stop_red_led()
{
	if (led_red_isOn == CONTROL_OFF)
		return;
	led_red_isOn = CONTROL_OFF;

	LightState brightness = {
		.color = 0u, .flashMode = Flash::NONE,
		.brightnessMode = Brightness::USER,
	};
	set_light_brightness(Type::BATTERY, brightness);
}

void start_green_led(int skew)
{
	if (led_green_isOn == CONTROL_ON)
		return;
	led_green_isOn = CONTROL_ON;

	if (skew > 255) skew = 255;
	else if (skew < 0) skew = 0;

	LightState brightness = {
		.color = 0xff000000 | (skew << 8), .flashMode = Flash::NONE,
		.brightnessMode = Brightness::USER,
	};
	set_light_brightness(Type::BATTERY, brightness);
}

void stop_green_led()
{
	if (led_green_isOn == CONTROL_OFF)
		return;
	led_green_isOn = CONTROL_OFF;

	LightState brightness = {
		.color = 0u, .flashMode = Flash::NONE,
		.brightnessMode = Brightness::USER,
	};
	set_light_brightness(Type::BATTERY, brightness);
}

int lights_chgfull()
{
	/* do not block this function like sleep */
	LOGE("lights_chgfull");

	pthread_mutex_lock(&lights_mutex);
	nChargingFull = CONTROL_ON;
	nChargingRemoved = CONTROL_OFF;

	stop_red_led();
	start_green_led(255);
	pthread_mutex_unlock(&lights_mutex);
	
	return 0;
}

int lights_chgon()
{
	LOGE("lights_chgon");

	pthread_mutex_lock(&lights_mutex);
	nChargingFull = CONTROL_OFF;
	nChargingRemoved = CONTROL_OFF;

	stop_green_led();
	start_red_led(255);
	pthread_mutex_unlock(&lights_mutex);
	return 0;
}

int lights_chgexit()
{
	/* do not block this function like sleep */
	LOGE("lights_chgexit");

	pthread_mutex_lock(&lights_mutex);
	nChargingFull = CONTROL_OFF;
	nChargingRemoved = CONTROL_ON;

	stop_green_led();
	stop_red_led();
	pthread_mutex_unlock(&lights_mutex);

	return 0;
}
void start_backlight()
{

    LightState brightness = {
        .color = 0xff000000 | 0xffffffff,
        .flashMode = Flash::NONE,
        .brightnessMode = Brightness::USER,
    };
    set_light_brightness(Type::BACKLIGHT, brightness);
}

void stop_backlight()
{
    LightState brightness = {
        .color = 0u, .flashMode = Flash::NONE, .brightnessMode = Brightness::USER,
    };
    set_light_brightness(Type::BACKLIGHT, brightness);

}
