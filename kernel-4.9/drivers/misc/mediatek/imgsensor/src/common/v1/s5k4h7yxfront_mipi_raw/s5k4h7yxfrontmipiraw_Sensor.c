/*****************************************************************************
 *
 * Filename:
 * ---------
 *	 s5k4h7yxfrontmipi_Sensor.c
 *
 * Project:
 * --------
 *	 ALPS
 *
 * Description:
 * ------------
 *	 Source code of Sensor driver
 *
 *
 *------------------------------------------------------------------------------
 * Upper this line, this part is controlled by CC/CQ. DO NOT MODIFY!!
 *============================================================================
 ****************************************************************************/

#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <linux/hqsysfs.h>

#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"
#include "kd_camera_typedef.h"
#include "s5k4h7yxfrontmipiraw_Sensor.h"

#define PFX "s5k4h7yxfront_camera_sensor"
#define LOG_INF(format, args...)	pr_debug(PFX "[%s] " format, __func__, ##args)
#define LOG_ERR(format, args...)	pr_err(PFX "[%s] " format, __func__, ##args)
static DEFINE_SPINLOCK(imgsensor_drv_lock);
#ifndef VENDOR_EDIT
#define VENDOR_EDIT
#endif

#ifdef VENDOR_EDIT
/* 2017/10/2 add for register device info*/
#define DEVICE_VERSION_S5K4H7YXFRONT    "s5k4h7yxfront"
extern void register_imgsensor_deviceinfo(char *name, char *version, u8 module_id);
//static uint8_t deviceInfo_register_value = 0x00;
extern unsigned char s5k4h7yxfront_get_module_id(void);
#endif

//extern unsigned main_sub_flag;
static imgsensor_info_struct imgsensor_info = {
	.sensor_id = S5K4H7YXFRONT_SENSOR_ID,  //S5K4H7YXFRONT_SENSOR_ID = 0x487C

	.checksum_value = 0x91f55fe8, //0x82256eb5,
	.pre = {
		.pclk = 280000000,
		.linelength = 3688,
		.framelength = 2530,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 1632,		//record different mode's width of grabwindow
		.grabwindow_height = 1224,		//record different mode's height of grabwindow
		/*	 following for MIPIDataLowPwr2HighSpeedSettleDelayCount by different scenario	*/
		.mipi_data_lp2hs_settle_dc = 85,
		/*	 following for GetDefaultFramerateByScenario()	*/
		.max_framerate = 300,
		.mipi_pixel_rate = 295000000,
	},
	.cap = {
		.pclk = 280000000,
		.linelength = 3688,
		.framelength = 2530,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 3264,
		.grabwindow_height = 2448,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 300,
		.mipi_pixel_rate = 295000000,
	},
	.cap1 = {
		.pclk = 280000000,
		.linelength = 3688,
		.framelength = 5060,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 3264,
		.grabwindow_height = 2448,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 150,
		.mipi_pixel_rate = 295000000,
	},
	.normal_video = {
		.pclk = 280000000,
		.linelength = 3688,
		.framelength = 2530,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 3264,
		.grabwindow_height = 2448,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 300,
		.mipi_pixel_rate = 295000000,
	},
	.hs_video = {
		.pclk = 280000000,
		.linelength = 3688,
		.framelength = 632,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 640,
		.grabwindow_height = 480,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 1200,
		.mipi_pixel_rate = 296000000,
	},
	.slim_video = {
		.pclk = 280000000,
		.linelength = 3688,
		.framelength = 2530,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 1280,
		.grabwindow_height = 720,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 300,
		.mipi_pixel_rate = 295000000,
	},
	.margin = 4,
	.min_shutter = 4,
	.max_frame_length = 0xffff,
	.ae_shut_delay_frame = 0,
	.ae_sensor_gain_delay_frame =0,
	.ae_ispGain_delay_frame = 2,
	.ihdr_support = 0,	     //1, support; 0,not support
	.ihdr_le_firstline = 0,  //1,le first ; 0, se first
	.sensor_mode_num = 4,	  //support sensor mode num

	.cap_delay_frame = 3,
	.pre_delay_frame = 3,
	.video_delay_frame = 3,
	.hs_video_delay_frame = 3,
	.slim_video_delay_frame = 3,

	.isp_driving_current = ISP_DRIVING_4MA,
	.sensor_interface_type = SENSOR_INTERFACE_TYPE_MIPI,
    .mipi_sensor_type = MIPI_OPHY_NCSI2, //0,MIPI_OPHY_NCSI2;  1,MIPI_OPHY_CSI2
	.mipi_settle_delay_mode = 0,  //0,MIPI_SETTLEDELAY_AUTO; 1,MIPI_SETTLEDELAY_MANNUAL
	.sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_Gr,
	.mclk = 24,
	.mipi_lane_num = SENSOR_MIPI_4_LANE,
	.i2c_addr_table = {0x20,0xff},
    .i2c_speed = 300,
};


static imgsensor_struct imgsensor = {
	.mirror = IMAGE_NORMAL,				//mirrorflip information
	.sensor_mode = IMGSENSOR_MODE_INIT, //IMGSENSOR_MODE enum value,record current sensor mode,such as: INIT, Preview, Capture, Video,High Speed Video, Slim Video
	.shutter = 0x3D0,					//current shutter
	.gain = 0x100,						//current gain
	.dummy_pixel = 0,					//current dummypixel
	.dummy_line = 0,					//current dummyline
    .current_fps = 30,  //full size current fps : 24fps for PIP, 30fps for Normal or ZSD
    .autoflicker_en = KAL_FALSE,  //auto flicker enable: KAL_FALSE for disable auto flicker, KAL_TRUE for enable auto flicker
	.test_pattern = KAL_FALSE,		//test pattern mode or not. KAL_TRUE for in test pattern mode, KAL_FALSE for normal output
	.current_scenario_id = MSDK_SCENARIO_ID_CAMERA_PREVIEW,//current scenario id
	.ihdr_en = 0, //sensor need support LE, SE with HDR feature
	.i2c_write_id = 0x20,
};


/* Sensor output window information */
static struct SENSOR_WINSIZE_INFO_STRUCT imgsensor_winsize_info[5] =
{{ 3264, 2448, 0,   0,   3264, 2448, 1632, 1224, 0000, 0000, 1632, 1224,	0,	0, 1632, 1224}, // Preview
 { 3264, 2448, 0,   0,   3264, 2448, 3264, 2448, 0000, 0000, 3264, 2448,	0,	0, 3264, 2448}, // capture
 { 3264, 2448, 0,   0,   3264, 2448, 3264, 2448, 0000, 0000, 3264, 2448,	0,	0, 3264, 2448}, // video
 { 3264, 2448, 0,   0,   3264, 2448, 640,  480,  0000, 0000, 640,  480,	    0,	0, 640,  480},  //hight speed video
 { 3264, 2448, 351, 514, 2562, 1440, 1280, 720,  0000, 0000, 1280, 720,	    0,	0, 1280, 720}}; // slim video


extern int iReadReg(u16 a_u2Addr , u8 * a_puBuff , u16 i2cId);
extern int iWriteReg(u16 a_u2Addr , u32 a_u4Data , u32 a_u4Bytes , u16 i2cId);
extern void kdSetI2CSpeed(u16 i2cSpeed);

static kal_uint16 read_cmos_sensor(kal_uint32 addr)
{
    kal_uint16 get_byte=0;
    char pusendcmd[2] = {(char)(addr >> 8) , (char)(addr & 0xFF) };
    iReadRegI2C(pusendcmd , 2, (u8*)&get_byte, 2, imgsensor.i2c_write_id);
    return ((get_byte<<8)&0xff00)|((get_byte>>8)&0x00ff);
}

/*
static void write_cmos_sensor(kal_uint16 addr, kal_uint16 para)
{
    char pusendcmd[4] = {(char)(addr >> 8) , (char)(addr & 0xFF) ,(char)(para >> 8),(char)(para & 0xFF)};
    iWriteRegI2C(pusendcmd , 4, imgsensor.i2c_write_id);
}
*/
static kal_uint16 read_cmos_sensor_8(kal_uint16 addr)
{
    kal_uint16 get_byte=0;
    char pusendcmd[2] = {(char)(addr >> 8) , (char)(addr & 0xFF) };
    iReadRegI2C(pusendcmd , 2, (u8*)&get_byte,1,imgsensor.i2c_write_id);
    return get_byte;
}

static void write_cmos_sensor_8(kal_uint16 addr, kal_uint8 para)
{
    char pusendcmd[4] = {(char)(addr >> 8) , (char)(addr & 0xFF) ,(char)(para & 0xFF)};
    iWriteRegI2C(pusendcmd , 3, imgsensor.i2c_write_id);
}

static void set_dummy(void)
{
	LOG_INF("dummyline = %d, dummypixels = %d \n", imgsensor.dummy_line, imgsensor.dummy_pixel);

	write_cmos_sensor_8(0x0340, imgsensor.frame_length >> 8);
	write_cmos_sensor_8(0x0341, imgsensor.frame_length & 0xFF);
	write_cmos_sensor_8(0x0342, imgsensor.line_length >> 8);
	write_cmos_sensor_8(0x0343, imgsensor.line_length & 0xFF);
}


static void set_max_framerate(UINT16 framerate,kal_bool min_framelength_en)
{
	kal_uint32 frame_length = imgsensor.frame_length;
	//unsigned long flags;

	LOG_INF("framerate = %d, min_framelength_en=%d\n", framerate,min_framelength_en);
	frame_length = imgsensor.pclk / framerate * 10 / imgsensor.line_length;
	LOG_INF("frame_length =%d\n", frame_length);
	spin_lock(&imgsensor_drv_lock);
	imgsensor.frame_length = (frame_length > imgsensor.min_frame_length) ? frame_length : imgsensor.min_frame_length;
	imgsensor.dummy_line = imgsensor.frame_length - imgsensor.min_frame_length;
	//dummy_line = frame_length - imgsensor.min_frame_length;
	//if (dummy_line < 0)
		//imgsensor.dummy_line = 0;
	//else
		//imgsensor.dummy_line = dummy_line;
	//imgsensor.frame_length = frame_length + imgsensor.dummy_line;
	if (imgsensor.frame_length > imgsensor_info.max_frame_length)
	{
		imgsensor.frame_length = imgsensor_info.max_frame_length;
		imgsensor.dummy_line = imgsensor.frame_length - imgsensor.min_frame_length;
	}
	if (min_framelength_en)
		imgsensor.min_frame_length = imgsensor.frame_length;
	spin_unlock(&imgsensor_drv_lock);
	set_dummy();
}	/*	set_max_framerate  */


static void write_shutter(kal_uint32 shutter)
	{
		kal_uint16 realtime_fps = 0;


		if(imgsensor.sensor_mode == IMGSENSOR_MODE_HIGH_SPEED_VIDEO)
		{
			if(shutter > imgsensor.min_frame_length - imgsensor_info.margin)
				shutter = imgsensor.min_frame_length - imgsensor_info.margin;
			write_cmos_sensor_8(0x0340, imgsensor.frame_length >> 8);
			write_cmos_sensor_8(0x0341, imgsensor.frame_length & 0xFF);
			write_cmos_sensor_8(0x0202, (shutter >> 8) & 0xFF);
 			write_cmos_sensor_8(0x0203, shutter  & 0xFF);
			LOG_INF("shutter =%d, framelength =%d\n", shutter,imgsensor.frame_length);
			return;
		}
		spin_lock(&imgsensor_drv_lock);
		if (shutter > imgsensor.min_frame_length - imgsensor_info.margin)
			{
			imgsensor.frame_length = shutter + imgsensor_info.margin;
			}
		else
			{
			imgsensor.frame_length = imgsensor.min_frame_length;
			}
		if (imgsensor.frame_length > imgsensor_info.max_frame_length)
			{
			imgsensor.frame_length = imgsensor_info.max_frame_length;
			}
		spin_unlock(&imgsensor_drv_lock);
		if (shutter < imgsensor_info.min_shutter)
			shutter = imgsensor_info.min_shutter;

		if (imgsensor.autoflicker_en == KAL_TRUE) {
			realtime_fps = imgsensor.pclk / imgsensor.line_length * 10 / imgsensor.frame_length;
			if(realtime_fps >= 297 && realtime_fps <= 305)
			{
				set_max_framerate(296,0);
				//set_dummy();
			}
			else if(realtime_fps >= 147 && realtime_fps <= 150)
			{
				set_max_framerate(146,0);
				//set_dummy();
			}
			else{
			write_cmos_sensor_8(0x0340, imgsensor.frame_length >> 8);
			write_cmos_sensor_8(0x0341, imgsensor.frame_length & 0xFF);
			}
		} else {
			// Extend frame length
			write_cmos_sensor_8(0x0340, imgsensor.frame_length >> 8);
			write_cmos_sensor_8(0x0341, imgsensor.frame_length & 0xFF);
		}

		// Update Shutter
		write_cmos_sensor_8(0x0340, imgsensor.frame_length >> 8);
		write_cmos_sensor_8(0x0341, imgsensor.frame_length & 0xFF);
		write_cmos_sensor_8(0x0202, (shutter >> 8) & 0xFF);
 		write_cmos_sensor_8(0x0203, shutter  & 0xFF);
 		LOG_INF("realtime_fps =%d\n", realtime_fps);
		LOG_INF("shutter =%d, framelength =%d\n", shutter,imgsensor.frame_length);

		//LOG_INF("frame_length = %d ", frame_length);

	}




/*************************************************************************
* FUNCTION
*	set_shutter
*
* DESCRIPTION
*	This function set e-shutter of sensor to change exposure time.
*
* PARAMETERS
*	iShutter : exposured lines
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static void set_shutter(kal_uint32 shutter)
{
	unsigned long flags;
	spin_lock_irqsave(&imgsensor_drv_lock, flags);
	imgsensor.shutter = shutter;
	spin_unlock_irqrestore(&imgsensor_drv_lock, flags);
	write_shutter(shutter);
}	/*	set_shutter */

static kal_uint16 gain2reg(const kal_uint16 gain)
{
	 kal_uint16 reg_gain = 0x0;

    reg_gain = gain/2;
    return (kal_uint16)reg_gain;
}

/*************************************************************************
* FUNCTION
*	set_gain
*
* DESCRIPTION
*	This function is to set global gain to sensor.
*
* PARAMETERS
*	iGain : sensor global gain(base: 0x40)
*
* RETURNS
*	the actually gain set to sensor.
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint16 set_gain(kal_uint16 gain)
{
    kal_uint16 reg_gain;

    if (gain < BASEGAIN || gain > 32 * BASEGAIN) {
        LOG_INF("Error gain setting");

        if (gain < BASEGAIN)
            gain = BASEGAIN;
        else if (gain > 32 * BASEGAIN)
            gain = 32 * BASEGAIN;
    }

    reg_gain = gain2reg(gain);
    spin_lock(&imgsensor_drv_lock);
    imgsensor.gain = reg_gain;
    spin_unlock(&imgsensor_drv_lock);
    LOG_INF("gain = %d , reg_gain = 0x%x\n ", gain, reg_gain);

    //write_cmos_sensor(0x0204,reg_gain);
    write_cmos_sensor_8(0x0204,(reg_gain>>8));
    write_cmos_sensor_8(0x0205,(reg_gain&0xff));

    return gain;
}   /*  s5k4h7yxfrontMIPI_SetGain  */
#if 0
static void ihdr_write_shutter_gain(kal_uint16 le, kal_uint16 se, kal_uint16 gain)
{
	LOG_INF("le:0x%x, se:0x%x, gain:0x%x\n",le,se,gain);

}
#endif

#if 0
static void set_mirror_flip(kal_uint8 image_mirror)
{
	LOG_INF("image_mirror = %d\n", image_mirror);

	/********************************************************
	   *
	   *   0x3820[2] ISP Vertical flip
	   *   0x3820[1] Sensor Vertical flip
	   *
	   *   0x3821[2] ISP Horizontal mirror
	   *   0x3821[1] Sensor Horizontal mirror
	   *
	   *   ISP and Sensor flip or mirror register bit should be the same!!
	   *
	   ********************************************************/

	switch (image_mirror)
    {
        case IMAGE_NORMAL: //B
            write_cmos_sensor(0x0101, 0x00);	//Set normal
            break;
        case IMAGE_V_MIRROR: //Gr X
            write_cmos_sensor(0x0101, 0x01);	//Set flip
            break;
        case IMAGE_H_MIRROR: //Gb
            write_cmos_sensor(0x0101, 0x02);	//Set mirror
            break;
        case IMAGE_HV_MIRROR: //R
            write_cmos_sensor(0x0101, 0x03);	//Set mirror and flip
            break;
    }

}
#endif
/*************************************************************************
* FUNCTION
*	night_mode
*
* DESCRIPTION
*	This function night mode of sensor.
*
* PARAMETERS
*	bEnable: KAL_TRUE -> enable night mode, otherwise, disable night mode
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static void night_mode(kal_bool enable)
{
/*No Need to implement this function*/
}	/*	night_mode	*/
static kal_uint32 streaming_control(kal_bool enable)
{
	int timeout = 100;
	int i = 0;
	int framecnt = 0;

	LOG_INF("streaming_enable(0=Sw Standby,1=streaming): %d\n", enable);
	if (enable) {
		//write_cmos_sensor(0x6028,0x4000);
		write_cmos_sensor_8(0x0100, 0X01);
		mDELAY(10);
	} else {
		//write_cmos_sensor(0x6028,0x4000);
		write_cmos_sensor_8(0x0100, 0x00);
		for (i = 0; i < timeout; i++) {
			mDELAY(10);
			framecnt = read_cmos_sensor_8(0x0005);
			if ( framecnt == 0xFF) {
				LOG_INF(" Stream Off OK at i=%d.\n", i);
				return ERROR_NONE;
			}
		}
		LOG_INF("Stream Off Fail! framecnt=%d.\n", framecnt);
	}
	return ERROR_NONE;
}

#define OTP_4H7F  1

#if OTP_4H7F
#define FLAG_VALUE_PAGE 0x40
#define INCLUDE_NO_OTP_4H7F 0
#define S5K4H7YXFRONT_OFILM_MODULE_ID 0x07
#define S5K4H7YXFRONT_OTP_MODULE_DATA_SIZE 20
#define S5K4H7YXFRONT_FLAG_START_ADDRESS 0x0a04
#define S5K4H7YXFRONT_CHECKSUM_START_ADDRESS 0x0a05
#define getbit(x,y)   ((x) >> (y)&1)
#define GAIN_DEFAULT       0x0100


typedef struct s5k4h7yxfront_otp_data {
	unsigned char page_flag;
	unsigned char module_id[2];
	unsigned short moduleid;
	unsigned char gloden[8];
	unsigned char unint[8];
	unsigned char data[128];
	unsigned char lscdata[640];
} S5K4H7YXFRONT_OTP_DATA;

static int rg_ratio;
static int bg_ratio;
static int golden_rg_ratio;
static int golden_bg_ratio;
static int valid = -1;

static unsigned short R_GAIN;
static unsigned short B_GAIN;
static unsigned short Gr_GAIN;
static unsigned short Gb_GAIN;
static unsigned short G_GAIN;

S5K4H7YXFRONT_OTP_DATA s5k4h7yxfront_otp_data;


static void write_cmos_sensor_16(kal_uint16 addr, kal_uint16 para)
{
    char pusendcmd[4] = {(char)(addr >> 8) , (char)(addr & 0xFF) ,(char)(para >> 8),(char)(para & 0xFF)};

    //kdSetI2CSpeed(imgsensor_info.i2c_speed); // Add this func to set i2c speed by each sensor
    iWriteRegI2C(pusendcmd , 4, imgsensor.i2c_write_id);
}

static int s5k4h7yxfront_read_otp_page_data(int page, int start_add, unsigned char *Buff, int size)
{
	unsigned short stram_flag = 0;
	unsigned short val = 0;
	int i = 0;
	if (NULL == Buff) return 0;

	stram_flag = read_cmos_sensor_8(0x0100); //3
	if (stram_flag == 0) {
		write_cmos_sensor_8(0x0100,0x01);   //3
		mdelay(50);
	}
	write_cmos_sensor_8(0x0a02,page);    //3
	write_cmos_sensor_8(0x3b41,0x01);
	write_cmos_sensor_8(0x3b42,0x03);
	write_cmos_sensor_8(0x3b40,0x01);
	write_cmos_sensor_16(0x0a00,0x0100); //4 otp enable and read start

	for (i = 0; i <= 100; i++)
	{
		mdelay(1);
		val = read_cmos_sensor_8(0x0A01);
		if (val == 0x01)
			break;
	}

	for ( i = 0; i < size; i++ ) {
		Buff[i] = read_cmos_sensor_8(start_add+i); //3
		LOG_INF("+++4h7 1 cur page = %d, Buff[%d] = 0x%x\n",page,i,Buff[i]);
	}
	write_cmos_sensor_16(0x0a00,0x0400);
	write_cmos_sensor_16(0x0a00,0x0000); //4 //otp enable and read end

	return 0;
}

static bool s5k4h7yxfront_read_valid_data(int page, int start_add)
{
	unsigned char page_flag[2] = {0};
	s5k4h7yxfront_read_otp_page_data(page,start_add,&page_flag[0],1);
	s5k4h7yxfront_read_otp_page_data(page,start_add,&page_flag[1],1);
	LOG_INF("+++4h7 2 page = %d,page_flag0 = 0x%x,f1 = 0x%x\n",page,page_flag[0],page_flag[1]);
	if (page_flag[0] != 0 || page_flag[1] != 0) {
		//LOGE("+++4h7 3 get vaild page success = %d\n",page);
		s5k4h7yxfront_otp_data.page_flag = ((page_flag[0] > page_flag[1])? page_flag[0] : page_flag[1]);
		//LOGE("+++4h7====== s5k4h7yxfront_otp_data.page_flag = 0x%x\n",s5k4h7yxfront_otp_data.page_flag);
		if (!((getbit(s5k4h7yxfront_otp_data.page_flag,6)) && !(getbit(s5k4h7yxfront_otp_data.page_flag,7)))) {
			LOG_INF("valid bit 7 = %d,bit 6 = %d\n",getbit(s5k4h7yxfront_otp_data.page_flag,7),getbit(s5k4h7yxfront_otp_data.page_flag,6));
			LOG_ERR("+++4h7 error data not valid\n");
			return false;
		}else{
			return true;
		}
	}else{
		s5k4h7yxfront_otp_data.page_flag = 0;
		return false;
	}
}

static int s5k4h7yxfront_get_vaild_data_page(int start_add)
{
	unsigned short page = 21;
	LOG_INF("read flag .....");
	for (page = 21;page <= 23; page++) {
		if(s5k4h7yxfront_read_valid_data(page, start_add)){
			break;
		}else if (23 == page){
			return 0;
		}
	}

	return page;
}

static int s5k4h7yxfront_read_data_kernel(void)
{
	unsigned int page = 0;
	kal_uint16 check = 0;
	unsigned int i = 0;

	page = s5k4h7yxfront_get_vaild_data_page(S5K4H7YXFRONT_FLAG_START_ADDRESS);
	if(!page) return -1;

	s5k4h7yxfront_read_otp_page_data(page,S5K4H7YXFRONT_CHECKSUM_START_ADDRESS,s5k4h7yxfront_otp_data.data,S5K4H7YXFRONT_OTP_MODULE_DATA_SIZE);

	for (i = 0; i < S5K4H7YXFRONT_OTP_MODULE_DATA_SIZE - 1; i++)
		check += s5k4h7yxfront_otp_data.data[i];

	if ((check % 255 + 1) == s5k4h7yxfront_otp_data.data[S5K4H7YXFRONT_OTP_MODULE_DATA_SIZE - 1]) {
		s5k4h7yxfront_otp_data.moduleid = s5k4h7yxfront_otp_data.data[0];
		rg_ratio = (s5k4h7yxfront_otp_data.data[12] << 2 | ((s5k4h7yxfront_otp_data.data[14] & 0xc0) >> 6));
		bg_ratio = (s5k4h7yxfront_otp_data.data[13] << 2 | ((s5k4h7yxfront_otp_data.data[14] & 0x30) >> 4));
		golden_rg_ratio = (s5k4h7yxfront_otp_data.data[15] << 2 | ((s5k4h7yxfront_otp_data.data[17] & 0xc0) >> 6));
		golden_bg_ratio = (s5k4h7yxfront_otp_data.data[16] << 2 | ((s5k4h7yxfront_otp_data.data[17] & 0x30) >> 4));
		LOG_ERR("moduleid= 0x%x rg_ratio = %d bg_ratio = %d golden_rg_ratio = %d golden_bg_ratio = %d\n",
			s5k4h7yxfront_otp_data.moduleid,rg_ratio,bg_ratio,golden_rg_ratio,golden_bg_ratio);
	}else{
		return -1;
	}
	return 0;
}


static bool update_otp(void)
{
	int rg = 0, bg = 0, golden_rg = 0, golden_bg = 0;
	kal_uint32 r_ratio = 0;
	kal_uint32 b_ratio = 0;

	rg = rg_ratio;
	bg = bg_ratio;
	golden_rg = golden_rg_ratio;
	golden_bg = golden_bg_ratio;

	r_ratio = ((1024*golden_rg)/rg);
	b_ratio = ((1024*golden_bg)/bg);
	LOG_ERR("[S5K4H7YX] otp wb r_ratio = %d, b_ratio = %d\n", r_ratio, b_ratio);

	if ((R_GAIN == 0) && (B_GAIN == 0) && (G_GAIN == 0)){
		if (r_ratio >= 1024)
		{
			if (b_ratio >= 1024)
			{
				R_GAIN = (unsigned short)((GAIN_DEFAULT*r_ratio)/1024);
				G_GAIN = GAIN_DEFAULT;
				B_GAIN = (unsigned short)((GAIN_DEFAULT*b_ratio)/1024);
			} else
			{
				R_GAIN = (unsigned short)((GAIN_DEFAULT*r_ratio)/b_ratio);
				G_GAIN = (unsigned short)((GAIN_DEFAULT*1024)/b_ratio);
				B_GAIN = GAIN_DEFAULT;
			}
		} else
		{
			if (b_ratio >= 1024)
			{
				R_GAIN = GAIN_DEFAULT;
				G_GAIN = (unsigned short)((GAIN_DEFAULT*1024)/r_ratio);
				B_GAIN = (unsigned short)((GAIN_DEFAULT*b_ratio)/r_ratio);

			} else
			{
				Gr_GAIN = (unsigned short)((GAIN_DEFAULT*1024)/r_ratio);
				Gb_GAIN = (unsigned short)((GAIN_DEFAULT*1024)/b_ratio);
				if (Gr_GAIN >= Gb_GAIN)
				{
					R_GAIN = GAIN_DEFAULT;
					G_GAIN = (unsigned short)((GAIN_DEFAULT*1024)/r_ratio);
					B_GAIN = (unsigned short)((GAIN_DEFAULT*b_ratio)/r_ratio);
				} else
				{
					R_GAIN =  (unsigned short)((GAIN_DEFAULT*r_ratio)/b_ratio);
					G_GAIN = (unsigned short)((GAIN_DEFAULT*1024)/b_ratio);
					B_GAIN = GAIN_DEFAULT;
				}
			}
		}
		LOG_INF("[S5K4H7YX] otp1 wb R_GAIN = %d, B_GAIN = %d, G_GAIN = %d\n", R_GAIN, B_GAIN, G_GAIN);
	}

	if ((R_GAIN != 0) && (B_GAIN != 0) && (G_GAIN != 0))
	{
		write_cmos_sensor_8(0x3c0f, 0x00);

		write_cmos_sensor_8(0x0210, R_GAIN>>8);
		write_cmos_sensor_8(0x0211, R_GAIN&0xff);
		write_cmos_sensor_8(0x0212, B_GAIN>>8);
		write_cmos_sensor_8(0x0213, B_GAIN&0xff);
		write_cmos_sensor_8(0x020E, G_GAIN>>8);
		write_cmos_sensor_8(0x020F, G_GAIN&0xff);
		write_cmos_sensor_8(0x0214, G_GAIN>>8);
		write_cmos_sensor_8(0x0215, G_GAIN&0xff);

		LOG_ERR("[S5K4H7YX] otp2 wb R_GAIN = %d, B_GAIN = %d, G_GAIN = %d\n", R_GAIN, B_GAIN, G_GAIN);
	}

	LOG_INF("[S5K4H7YX] otp3 wb R_GAIN = %d, B_GAIN = %d, G_GAIN = %d\n", R_GAIN, B_GAIN, G_GAIN);

	//enable lsc
	write_cmos_sensor_8(0x3400, 0x00);
	write_cmos_sensor_8(0x0B00, 0x01);

	return 1;
}
#endif

static void sensor_init(void)
{
	write_cmos_sensor_8(0x0100, 0x00);
	write_cmos_sensor_8(0x0B05, 0x01);
	write_cmos_sensor_8(0x3074, 0x06);
	write_cmos_sensor_8(0x3075, 0x2F);
	write_cmos_sensor_8(0x308A, 0x20);
	write_cmos_sensor_8(0x308B, 0x08);
	write_cmos_sensor_8(0x308C, 0x0B);
	write_cmos_sensor_8(0x3081, 0x07);
	write_cmos_sensor_8(0x307B, 0x85);
	write_cmos_sensor_8(0x307A, 0x0A);
	write_cmos_sensor_8(0x3079, 0x0A);
	write_cmos_sensor_8(0x306E, 0x71);
	write_cmos_sensor_8(0x306F, 0x28);
	write_cmos_sensor_8(0x301F, 0x20);
	write_cmos_sensor_8(0x306B, 0x9A);
	write_cmos_sensor_8(0x3091, 0x1F);
	write_cmos_sensor_8(0x30C4, 0x06);
	write_cmos_sensor_8(0x3200, 0x09);
	write_cmos_sensor_8(0x306A, 0x79);
	write_cmos_sensor_8(0x30B0, 0xFF);
	write_cmos_sensor_8(0x306D, 0x08);
	write_cmos_sensor_8(0x3080, 0x00);
	write_cmos_sensor_8(0x3929, 0x3F);
	write_cmos_sensor_8(0x3084, 0x16);
	write_cmos_sensor_8(0x3070, 0x0F);
	write_cmos_sensor_8(0x3B45, 0x01);
	write_cmos_sensor_8(0x30C2, 0x05);
	write_cmos_sensor_8(0x3069, 0x87);
	write_cmos_sensor_8(0x3931, 0x02);//VOD 470mV
	write_cmos_sensor_8(0x3932, 0x80);//LP up
	write_cmos_sensor_8(0x3930, 0x80);//tr/tf 2step up
	write_cmos_sensor_8(0x392F, 0x01);//tr/tf 2step up
}   /*  s5k4h7yxfrontMIPI_Sensor_Init  */


static void preview_setting(void)
{
	//write_cmos_sensor_8(0x0100, 0x00);
	write_cmos_sensor_8(0x0136, 0x18);
	write_cmos_sensor_8(0x0137, 0x00);
	write_cmos_sensor_8(0x0305, 0x06);
	write_cmos_sensor_8(0x0306, 0x00);
	write_cmos_sensor_8(0x0307, 0x8C);
	write_cmos_sensor_8(0x030D, 0x06);
	write_cmos_sensor_8(0x030E, 0x00);
	write_cmos_sensor_8(0x030F, 0xB7);  //mipi rate: AF:350M,B7:366M
	write_cmos_sensor_8(0x3C1F, 0x00);
	write_cmos_sensor_8(0x3C17, 0x00);
	write_cmos_sensor_8(0x3C1C, 0x05);
	write_cmos_sensor_8(0x3C1D, 0x15);
	write_cmos_sensor_8(0x0301, 0x04);
	write_cmos_sensor_8(0x0820, 0x02);
	write_cmos_sensor_8(0x0821, 0xBC);
	write_cmos_sensor_8(0x0822, 0x00);
	write_cmos_sensor_8(0x0823, 0x00);
	write_cmos_sensor_8(0x0112, 0x0A);
	write_cmos_sensor_8(0x0113, 0x0A);
	write_cmos_sensor_8(0x0114, 0x03);
	write_cmos_sensor_8(0x3906, 0x00);
	write_cmos_sensor_8(0x0344, 0x00);
	write_cmos_sensor_8(0x0345, 0x08);
	write_cmos_sensor_8(0x0346, 0x00);
	write_cmos_sensor_8(0x0347, 0x08);
	write_cmos_sensor_8(0x0348, 0x0C);
	write_cmos_sensor_8(0x0349, 0xC7);
	write_cmos_sensor_8(0x034A, 0x09);
	write_cmos_sensor_8(0x034B, 0x97);
	write_cmos_sensor_8(0x034C, 0x06);
	write_cmos_sensor_8(0x034D, 0x60);
	write_cmos_sensor_8(0x034E, 0x04);
	write_cmos_sensor_8(0x034F, 0xC8);
	write_cmos_sensor_8(0x0900, 0x01);
	write_cmos_sensor_8(0x0901, 0x22);
	write_cmos_sensor_8(0x0381, 0x01);
	write_cmos_sensor_8(0x0383, 0x01);
	write_cmos_sensor_8(0x0385, 0x01);
	write_cmos_sensor_8(0x0387, 0x03);
	write_cmos_sensor_8(0x0101, 0x00);
	write_cmos_sensor_8(0x0340, 0x09);
	write_cmos_sensor_8(0x0341, 0xE2);
	write_cmos_sensor_8(0x0342, 0x0E);
	write_cmos_sensor_8(0x0343, 0x68);
	write_cmos_sensor_8(0x0200, 0x0D);
	write_cmos_sensor_8(0x0201, 0xD8);
	write_cmos_sensor_8(0x0202, 0x02);
	write_cmos_sensor_8(0x0203, 0x08);
	//write_cmos_sensor_8(0x3400, 0x01);
	//write_cmos_sensor_8(0x0100, 0x01);
	}   /*  s5k4h7yxfrontMIPI_Capture_Setting  */

static void capture_setting(kal_uint16 currefps)

{
    if (currefps == 150)
	{
		//write_cmos_sensor_8(0x0100, 0x00);
		write_cmos_sensor_8(0x0136, 0x18);
		write_cmos_sensor_8(0x0137, 0x00);
		write_cmos_sensor_8(0x0305, 0x06);
		write_cmos_sensor_8(0x0306, 0x00);
		write_cmos_sensor_8(0x0307, 0x8C);
		write_cmos_sensor_8(0x030D, 0x06);
		write_cmos_sensor_8(0x030E, 0x00);
		write_cmos_sensor_8(0x030F, 0xB7); //mipi rate: AF:350M,B7:366M
		write_cmos_sensor_8(0x3C1F, 0x00);
		write_cmos_sensor_8(0x3C17, 0x00);
		write_cmos_sensor_8(0x3C1C, 0x05);
		write_cmos_sensor_8(0x3C1D, 0x15);
		write_cmos_sensor_8(0x0301, 0x04);
		write_cmos_sensor_8(0x0820, 0x02);
		write_cmos_sensor_8(0x0821, 0xBC);
		write_cmos_sensor_8(0x0822, 0x00);
		write_cmos_sensor_8(0x0823, 0x00);
		write_cmos_sensor_8(0x0112, 0x0A);
		write_cmos_sensor_8(0x0113, 0x0A);
		write_cmos_sensor_8(0x0114, 0x03);
		write_cmos_sensor_8(0x3906, 0x04);
		write_cmos_sensor_8(0x0344, 0x00);
		write_cmos_sensor_8(0x0345, 0x08);
		write_cmos_sensor_8(0x0346, 0x00);
		write_cmos_sensor_8(0x0347, 0x08);
		write_cmos_sensor_8(0x0348, 0x0C);
		write_cmos_sensor_8(0x0349, 0xC7);
		write_cmos_sensor_8(0x034A, 0x09);
		write_cmos_sensor_8(0x034B, 0x97);
		write_cmos_sensor_8(0x034C, 0x0C);
		write_cmos_sensor_8(0x034D, 0xC0);
		write_cmos_sensor_8(0x034E, 0x09);
		write_cmos_sensor_8(0x034F, 0x90);
		write_cmos_sensor_8(0x0900, 0x00);
		write_cmos_sensor_8(0x0901, 0x00);
		write_cmos_sensor_8(0x0381, 0x01);
		write_cmos_sensor_8(0x0383, 0x01);
		write_cmos_sensor_8(0x0385, 0x01);
		write_cmos_sensor_8(0x0387, 0x01);
		write_cmos_sensor_8(0x0101, 0x00);
		write_cmos_sensor_8(0x0340, 0x13);
		write_cmos_sensor_8(0x0341, 0xC4);
		write_cmos_sensor_8(0x0342, 0x0E);
		write_cmos_sensor_8(0x0343, 0x68);
		write_cmos_sensor_8(0x0200, 0x0D);
		write_cmos_sensor_8(0x0201, 0xD8);
		write_cmos_sensor_8(0x0202, 0x02);
		write_cmos_sensor_8(0x0203, 0x08);
		//write_cmos_sensor_8(0x3400, 0x01);
		//write_cmos_sensor_8(0x0100, 0x01);
	}
	else
	{
		//write_cmos_sensor_8(0x0100, 0x00);
		write_cmos_sensor_8(0x0136, 0x18);
		write_cmos_sensor_8(0x0137, 0x00);
		write_cmos_sensor_8(0x0305, 0x06);
		write_cmos_sensor_8(0x0306, 0x00);
		write_cmos_sensor_8(0x0307, 0x8C);
		write_cmos_sensor_8(0x030D, 0x06);
		write_cmos_sensor_8(0x030E, 0x00);
		write_cmos_sensor_8(0x030F, 0xB7); //mipi rate: AF:350M,B7:366M
		write_cmos_sensor_8(0x3C1F, 0x00);
		write_cmos_sensor_8(0x3C17, 0x00);
		write_cmos_sensor_8(0x3C1C, 0x05);
		write_cmos_sensor_8(0x3C1D, 0x15);
		write_cmos_sensor_8(0x0301, 0x04);
		write_cmos_sensor_8(0x0820, 0x02);
		write_cmos_sensor_8(0x0821, 0xBC);
		write_cmos_sensor_8(0x0822, 0x00);
		write_cmos_sensor_8(0x0823, 0x00);
		write_cmos_sensor_8(0x0112, 0x0A);
		write_cmos_sensor_8(0x0113, 0x0A);
		write_cmos_sensor_8(0x0114, 0x03);
		write_cmos_sensor_8(0x3906, 0x04);
		write_cmos_sensor_8(0x0344, 0x00);
		write_cmos_sensor_8(0x0345, 0x08);
		write_cmos_sensor_8(0x0346, 0x00);
		write_cmos_sensor_8(0x0347, 0x08);
		write_cmos_sensor_8(0x0348, 0x0C);
		write_cmos_sensor_8(0x0349, 0xC7);
		write_cmos_sensor_8(0x034A, 0x09);
		write_cmos_sensor_8(0x034B, 0x97);
		write_cmos_sensor_8(0x034C, 0x0C);
		write_cmos_sensor_8(0x034D, 0xC0);
		write_cmos_sensor_8(0x034E, 0x09);
		write_cmos_sensor_8(0x034F, 0x90);
		write_cmos_sensor_8(0x0900, 0x00);
		write_cmos_sensor_8(0x0901, 0x00);
		write_cmos_sensor_8(0x0381, 0x01);
		write_cmos_sensor_8(0x0383, 0x01);
		write_cmos_sensor_8(0x0385, 0x01);
		write_cmos_sensor_8(0x0387, 0x01);
		write_cmos_sensor_8(0x0101, 0x00);
		write_cmos_sensor_8(0x0340, 0x09);
		write_cmos_sensor_8(0x0341, 0xE2);
		write_cmos_sensor_8(0x0342, 0x0E);
		write_cmos_sensor_8(0x0343, 0x68);
		write_cmos_sensor_8(0x0200, 0x0D);
		write_cmos_sensor_8(0x0201, 0xD8);
		write_cmos_sensor_8(0x0202, 0x02);
		write_cmos_sensor_8(0x0203, 0x08);
		//write_cmos_sensor_8(0x3400, 0x01);
		//write_cmos_sensor_8(0x0100, 0x01);
	}
}

/*static void normal_video_setting(kal_uint16 currefps)
{
	LOG_INF("E! currefps:%d\n",currefps);
	//write_cmos_sensor_8(0x0100, 0x00);
	write_cmos_sensor_8(0x0136, 0x18);
	write_cmos_sensor_8(0x0137, 0x00);
	write_cmos_sensor_8(0x0305, 0x06);
	write_cmos_sensor_8(0x0306, 0x00);
	write_cmos_sensor_8(0x0307, 0x8C);
	write_cmos_sensor_8(0x030D, 0x06);
	write_cmos_sensor_8(0x030E, 0x00);
	write_cmos_sensor_8(0x030F, 0xB7); //mipi rate: AF:350M,B7:366M
	write_cmos_sensor_8(0x3C1F, 0x00);
	write_cmos_sensor_8(0x3C17, 0x00);
	write_cmos_sensor_8(0x3C1C, 0x05);
	write_cmos_sensor_8(0x3C1D, 0x15);
	write_cmos_sensor_8(0x0301, 0x04);
	write_cmos_sensor_8(0x0820, 0x02);
	write_cmos_sensor_8(0x0821, 0xBC);
	write_cmos_sensor_8(0x0822, 0x00);
	write_cmos_sensor_8(0x0823, 0x00);
	write_cmos_sensor_8(0x0112, 0x0A);
	write_cmos_sensor_8(0x0113, 0x0A);
	write_cmos_sensor_8(0x0114, 0x03);
	write_cmos_sensor_8(0x3906, 0x04);
	write_cmos_sensor_8(0x0344, 0x00);
	write_cmos_sensor_8(0x0345, 0x08);
	write_cmos_sensor_8(0x0346, 0x01);
	write_cmos_sensor_8(0x0347, 0x3A);
	write_cmos_sensor_8(0x0348, 0x0C);
	write_cmos_sensor_8(0x0349, 0xC7);
	write_cmos_sensor_8(0x034A, 0x08);
	write_cmos_sensor_8(0x034B, 0x65);
	write_cmos_sensor_8(0x034C, 0x0C);
	write_cmos_sensor_8(0x034D, 0xC0);
	write_cmos_sensor_8(0x034E, 0x07);
	write_cmos_sensor_8(0x034F, 0x2C);
	write_cmos_sensor_8(0x0900, 0x00);
	write_cmos_sensor_8(0x0901, 0x00);
	write_cmos_sensor_8(0x0381, 0x01);
	write_cmos_sensor_8(0x0383, 0x01);
	write_cmos_sensor_8(0x0385, 0x01);
	write_cmos_sensor_8(0x0387, 0x01);
	write_cmos_sensor_8(0x0101, 0x00);
	write_cmos_sensor_8(0x0340, 0x09);
	write_cmos_sensor_8(0x0341, 0xE2);
	write_cmos_sensor_8(0x0342, 0x0E);
	write_cmos_sensor_8(0x0343, 0x68);
	write_cmos_sensor_8(0x0200, 0x0D);
	write_cmos_sensor_8(0x0201, 0xD8);
	write_cmos_sensor_8(0x0202, 0x02);
	write_cmos_sensor_8(0x0203, 0x08);
	write_cmos_sensor_8(0x3400, 0x01);
	//write_cmos_sensor_8(0x0100, 0x01);
}*/
static void hs_video_setting(void)
{    LOG_INF("EVGA 120fps");
	//write_cmos_sensor_8(0x0100, 0x00);
	write_cmos_sensor_8(0x0136, 0x18);
	write_cmos_sensor_8(0x0137, 0x00);
	write_cmos_sensor_8(0x0305, 0x06);
	write_cmos_sensor_8(0x0306, 0x00);
	write_cmos_sensor_8(0x0307, 0x8C);
	write_cmos_sensor_8(0x030D, 0x06);
	write_cmos_sensor_8(0x030E, 0x00);
	write_cmos_sensor_8(0x030F, 0xB7); //mipi rate: AF:350M,B7:366M
	write_cmos_sensor_8(0x3C1F, 0x00);
	write_cmos_sensor_8(0x3C17, 0x00);
	write_cmos_sensor_8(0x3C1C, 0x05);
	write_cmos_sensor_8(0x3C1D, 0x15);
	write_cmos_sensor_8(0x0301, 0x04);
	write_cmos_sensor_8(0x0820, 0x02);
	write_cmos_sensor_8(0x0821, 0xBC);
	write_cmos_sensor_8(0x0822, 0x00);
	write_cmos_sensor_8(0x0823, 0x00);
	write_cmos_sensor_8(0x0112, 0x0A);
	write_cmos_sensor_8(0x0113, 0x0A);
	write_cmos_sensor_8(0x0114, 0x03);
	write_cmos_sensor_8(0x3906, 0x00);
	write_cmos_sensor_8(0x0344, 0x01);
	write_cmos_sensor_8(0x0345, 0x68);
	write_cmos_sensor_8(0x0346, 0x01);
	write_cmos_sensor_8(0x0347, 0x10);
	write_cmos_sensor_8(0x0348, 0x0B);
	write_cmos_sensor_8(0x0349, 0x67);
	write_cmos_sensor_8(0x034A, 0x08);
	write_cmos_sensor_8(0x034B, 0x8F);
	write_cmos_sensor_8(0x034C, 0x02);
	write_cmos_sensor_8(0x034D, 0x80);
	write_cmos_sensor_8(0x034E, 0x01);
	write_cmos_sensor_8(0x034F, 0xE0);
	write_cmos_sensor_8(0x0900, 0x01);
	write_cmos_sensor_8(0x0901, 0x44);
	write_cmos_sensor_8(0x0381, 0x01);
	write_cmos_sensor_8(0x0383, 0x01);
	write_cmos_sensor_8(0x0385, 0x01);
	write_cmos_sensor_8(0x0387, 0x07);
	write_cmos_sensor_8(0x0101, 0x00);
	write_cmos_sensor_8(0x0340, 0x02);
	write_cmos_sensor_8(0x0341, 0x78);
	write_cmos_sensor_8(0x0342, 0x0E);
	write_cmos_sensor_8(0x0343, 0x68);
	write_cmos_sensor_8(0x0200, 0x0D);
	write_cmos_sensor_8(0x0201, 0xD8);
	write_cmos_sensor_8(0x0202, 0x02);
	write_cmos_sensor_8(0x0203, 0x08);
	//write_cmos_sensor_8(0x3400, 0x01);
	//write_cmos_sensor_8(0x0100, 0x01);    // Streaming On
}

static void slim_video_setting(void)
{
	LOG_INF("E");
	//write_cmos_sensor_8(0x0100, 0x00);
	write_cmos_sensor_8(0x0136, 0x18);
	write_cmos_sensor_8(0x0137, 0x00);
	write_cmos_sensor_8(0x0305, 0x06);
	write_cmos_sensor_8(0x0306, 0x00);
	write_cmos_sensor_8(0x0307, 0x8C);
	write_cmos_sensor_8(0x030D, 0x06);
	write_cmos_sensor_8(0x030E, 0x00);
	write_cmos_sensor_8(0x030F, 0xB7); //mipi rate: AF:350M,B7:366M
	write_cmos_sensor_8(0x3C1F, 0x00);
	write_cmos_sensor_8(0x3C17, 0x00);
	write_cmos_sensor_8(0x3C1C, 0x05);
	write_cmos_sensor_8(0x3C1D, 0x15);
	write_cmos_sensor_8(0x0301, 0x04);
	write_cmos_sensor_8(0x0820, 0x02);
	write_cmos_sensor_8(0x0821, 0xBC);
	write_cmos_sensor_8(0x0822, 0x00);
	write_cmos_sensor_8(0x0823, 0x00);
	write_cmos_sensor_8(0x0112, 0x0A);
	write_cmos_sensor_8(0x0113, 0x0A);
	write_cmos_sensor_8(0x0114, 0x03);
	write_cmos_sensor_8(0x3906, 0x00);
	write_cmos_sensor_8(0x0344, 0x01);
	write_cmos_sensor_8(0x0345, 0x68);
	write_cmos_sensor_8(0x0346, 0x02);
	write_cmos_sensor_8(0x0347, 0x00);
	write_cmos_sensor_8(0x0348, 0x0B);
	write_cmos_sensor_8(0x0349, 0x67);
	write_cmos_sensor_8(0x034A, 0x07);
	write_cmos_sensor_8(0x034B, 0x9F);
	write_cmos_sensor_8(0x034C, 0x05);
	write_cmos_sensor_8(0x034D, 0x00);
	write_cmos_sensor_8(0x034E, 0x02);
	write_cmos_sensor_8(0x034F, 0xD0);
	write_cmos_sensor_8(0x0900, 0x01);
	write_cmos_sensor_8(0x0901, 0x22);
	write_cmos_sensor_8(0x0381, 0x01);
	write_cmos_sensor_8(0x0383, 0x01);
	write_cmos_sensor_8(0x0385, 0x01);
	write_cmos_sensor_8(0x0387, 0x03);
	write_cmos_sensor_8(0x0101, 0x00);
	write_cmos_sensor_8(0x0340, 0x09);
	write_cmos_sensor_8(0x0341, 0xE2);
	write_cmos_sensor_8(0x0342, 0x0E);
	write_cmos_sensor_8(0x0343, 0x68);
	write_cmos_sensor_8(0x0200, 0x0D);
	write_cmos_sensor_8(0x0201, 0xD8);
	write_cmos_sensor_8(0x0202, 0x02);
	write_cmos_sensor_8(0x0203, 0x08);
	//write_cmos_sensor_8(0x3400, 0x01);
	//write_cmos_sensor_8(0x0100, 0x01);
}

/*************************************************************************
* FUNCTION
*	get_imgsensor_id
*
* DESCRIPTION
*	This function get the sensor ID
*
* PARAMETERS
*	*sensorID : return the sensor ID
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 get_imgsensor_id(UINT32 *sensor_id)
{
	kal_uint8 i = 0;
	int  retry = 1;
	//sensor have two i2c address 0x5a 0x5b & 0x21 0x20, we should detect the module used i2c address
	/*if(2 != main_sub_flag)
	{
		*sensor_id = 0xffffffff;
		return ERROR_SENSOR_CONNECT_FAIL;
	}*/
	while (imgsensor_info.i2c_addr_table[i] != 0xff) {
		spin_lock(&imgsensor_drv_lock);
		imgsensor.i2c_write_id = imgsensor_info.i2c_addr_table[i];
		spin_unlock(&imgsensor_drv_lock);
		do {
			*sensor_id = ((read_cmos_sensor_8(0x0000) << 8) | read_cmos_sensor_8(0x0001)) + 1;
			printk("guang main2  0x%x, 0x%x\n", imgsensor.i2c_write_id, *sensor_id);
			if (*sensor_id == imgsensor_info.sensor_id) {
			// #ifdef VENDOR_EDIT
			// /* 2017/10/18 add for register device info*/
			// imgsensor_info.module_id = s5k4h7yxfront_get_module_id();
			// if (deviceInfo_register_value == 0x00) {
			    // register_imgsensor_deviceinfo("Cam_f", DEVICE_VERSION_S5K4H7YX, imgsensor_info.module_id);
			    // deviceInfo_register_value=0x01;
			// }
			// #endif
			//LOG_INF("i2c write id: 0x%x, sensor id: 0x%x module_id 0x%x\n", imgsensor.i2c_write_id,*sensor_id,imgsensor_info.module_id);
#if OTP_4H7F
				valid = s5k4h7yxfront_read_data_kernel();
#if INCLUDE_NO_OTP_4H7F
				if ((s5k4h7yxfront_otp_data.moduleid > 0) && (s5k4h7yxfront_otp_data.moduleid < 0xFFFF)) {
#endif
					if (s5k4h7yxfront_otp_data.moduleid != S5K4H7YXFRONT_OFILM_MODULE_ID) {
						*sensor_id = 0xFFFFFFFF;
						return ERROR_SENSOR_CONNECT_FAIL;
					} else {
						LOG_INF("This is ofilm --->s5k4h7front otp data vaild ...");
						hq_regiser_hw_info(HWID_SUB_CAM, "ofilm s5k4h7yxfront 8m");
					}
#if INCLUDE_NO_OTP_4H7F
				} else {
					LOG_INF("This is s5k4h7front, but no otp data ...");
				}
#endif
#endif
			break;
			}
			LOG_INF("Read sensor id fail, id: 0x%x,0x%x\n", imgsensor.i2c_write_id,*sensor_id);
			retry--;
		} while(retry > 0);
		i++;
		retry = 2;
	}
	if (*sensor_id != imgsensor_info.sensor_id) {
		// if Sensor ID is not correct, Must set *sensor_id to 0xFFFFFFFF
		*sensor_id = 0xFFFFFFFF;
		return ERROR_NONE;
	}
	return ERROR_NONE;
}



/*************************************************************************
* FUNCTION
*	open
*
* DESCRIPTION
*	This function initialize the registers of CMOS sensor
*
* PARAMETERS
*	None
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 open(void)
{
	//const kal_uint8 i2c_addr[] = {IMGSENSOR_WRITE_ID_1, IMGSENSOR_WRITE_ID_2};
	kal_uint8 retry = 1;
	kal_uint16 sensor_id = 0;
	#ifdef VENDOR_EDIT
	/*2017/10/18 add for otp*/
	//bool otp_flag=0;
	#endif
	//printk("guang main2  open %d \n", main_sub_flag);
	LOG_INF("PLATFORM:MT6595,MIPI 2LANE\n");
	LOG_INF("preview 1280*960@30fps,864Mbps/lane; video 1280*960@30fps,864Mbps/lane; capture 5M@30fps,864Mbps/lane\n");
	/*if(2 != main_sub_flag)
	{
		return ERROR_SENSOR_CONNECT_FAIL;
	}*/
	//sensor have two i2c address 0x5a 0x5b & 0x21 0x20, we should detect the module used i2c address
	//while (imgsensor_info.i2c_addr_table[i] != 0xff) {
		spin_lock(&imgsensor_drv_lock);
		imgsensor.i2c_write_id = 0x20;//imgsensor_info.i2c_addr_table[i];
		spin_unlock(&imgsensor_drv_lock);
		do {
			sensor_id = ((read_cmos_sensor_8(0x0000) << 8) | read_cmos_sensor_8(0x0001)) + 1;
			if (sensor_id == imgsensor_info.sensor_id) {
				LOG_INF("i2c write id: 0x%x, sensor id: 0x%x\n", imgsensor.i2c_write_id,sensor_id);
				break;
			}
			LOG_INF("Read sensor id fail, id: 0x%x,0x%x\n", imgsensor.i2c_write_id,sensor_id);
			retry--;
		} while(retry > 0);
		//i++;
		//if (sensor_id == imgsensor_info.sensor_id)
		//	break;
		//retry = 2;
	//}
	if (imgsensor_info.sensor_id != sensor_id)
		return ERROR_SENSOR_CONNECT_FAIL;

	/* initail sequence write in  */
	sensor_init();
	// #ifdef VENDOR_EDIT
	// /* 2017/10/18 add for otp*/
	// otp_flag = S5K4H7YX_otp_update();
	// if(otp_flag)
	    // LOG_INF("Load otp succeed\n");
	// else
	    // LOG_INF("Load otp failed\n");
	// #endif

#if OTP_4H7F
	if (valid == 0)
		update_otp();
#endif

	spin_lock(&imgsensor_drv_lock);

	imgsensor.autoflicker_en= KAL_FALSE;
	imgsensor.sensor_mode = IMGSENSOR_MODE_INIT;
	imgsensor.shutter = 0x3D0;
	imgsensor.gain = 0x100;
	imgsensor.pclk = imgsensor_info.pre.pclk;
	imgsensor.frame_length = imgsensor_info.pre.framelength;
	imgsensor.line_length = imgsensor_info.pre.linelength;
	imgsensor.min_frame_length = imgsensor_info.pre.framelength;
	imgsensor.dummy_pixel = 0;
	imgsensor.dummy_line = 0;
	imgsensor.ihdr_en = 0;
	imgsensor.test_pattern = KAL_FALSE;
	imgsensor.current_fps = imgsensor_info.pre.max_framerate;
	spin_unlock(&imgsensor_drv_lock);
	return ERROR_NONE;
}	/*	open  */



/*************************************************************************
* FUNCTION
*	close
*
* DESCRIPTION
*
*
* PARAMETERS
*	None
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 close(void)
{
	LOG_INF("E\n");

	/*No Need to implement this function*/

	return ERROR_NONE;
}	/*	close  */


/*************************************************************************
* FUNCTION
* preview
*
* DESCRIPTION
*	This function start the sensor preview.
*
* PARAMETERS
*	*image_window : address pointer of pixel numbers in one period of HSYNC
*  *sensor_config_data : address pointer of line numbers in one period of VSYNC
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 preview(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	LOG_INF("E\n");

	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_PREVIEW;
	imgsensor.pclk = imgsensor_info.pre.pclk;
	//imgsensor.video_mode = KAL_FALSE;
	imgsensor.line_length = imgsensor_info.pre.linelength;
	imgsensor.frame_length = imgsensor_info.pre.framelength;
	imgsensor.min_frame_length = imgsensor_info.pre.framelength;
	imgsensor.current_fps = imgsensor.current_fps;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	preview_setting();
	return ERROR_NONE;
}   /*  s5k4h7yxfrontMIPIPreview   */

/*************************************************************************
* FUNCTION
*	capture
*
* DESCRIPTION
*	This function setup the CMOS sensor in capture MY_OUTPUT mode
*
* PARAMETERS
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 capture(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
						  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	LOG_INF("E\n");
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_CAPTURE;
	if (imgsensor.current_fps == imgsensor_info.cap1.max_framerate) {//PIP capture: 24fps for less than 13M, 20fps for 16M,15fps for 20M
		imgsensor.pclk = imgsensor_info.cap1.pclk;
		imgsensor.line_length = imgsensor_info.cap1.linelength;
		imgsensor.frame_length = imgsensor_info.cap1.framelength;
		imgsensor.min_frame_length = imgsensor_info.cap1.framelength;
		imgsensor.autoflicker_en = KAL_FALSE;
	} else {
		if (imgsensor.current_fps != imgsensor_info.cap.max_framerate)
			LOG_INF("Warning: current_fps %d fps is not support, so use cap1's setting: %d fps!\n",imgsensor.current_fps,imgsensor_info.cap1.max_framerate/10);
		imgsensor.pclk = imgsensor_info.cap.pclk;
		imgsensor.line_length = imgsensor_info.cap.linelength;
		imgsensor.frame_length = imgsensor_info.cap.framelength;
		imgsensor.min_frame_length = imgsensor_info.cap.framelength;
		imgsensor.autoflicker_en = KAL_FALSE;
	}

	spin_unlock(&imgsensor_drv_lock);

	capture_setting(imgsensor.current_fps);

	return ERROR_NONE;
}	/* capture() */
static kal_uint32 normal_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
					  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	LOG_INF("E\n");

	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_VIDEO;
	imgsensor.pclk = imgsensor_info.normal_video.pclk;
	imgsensor.line_length = imgsensor_info.normal_video.linelength;
	imgsensor.frame_length = imgsensor_info.normal_video.framelength;
	imgsensor.min_frame_length = imgsensor_info.normal_video.framelength;
	//imgsensor.current_fps = 300;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	capture_setting(imgsensor.current_fps);
	return ERROR_NONE;
}   /*  s5k4h7yxfrontMIPIPreview   */

static kal_uint32 hs_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
					  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	LOG_INF("E\n");

	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_HIGH_SPEED_VIDEO;
	imgsensor.pclk = imgsensor_info.hs_video.pclk;
	//imgsensor.video_mode = KAL_TRUE;
	imgsensor.line_length = imgsensor_info.hs_video.linelength;
	imgsensor.frame_length = imgsensor_info.hs_video.framelength;
	imgsensor.min_frame_length = imgsensor_info.hs_video.framelength;
	imgsensor.dummy_line = 0;
	imgsensor.dummy_pixel = 0;
	//imgsensor.current_fps = 300;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	hs_video_setting();
	return ERROR_NONE;
}   /*  s5k4h7yxfrontMIPIPreview   */

static kal_uint32 slim_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
					  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	LOG_INF("E\n");

	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_SLIM_VIDEO;
	imgsensor.pclk = imgsensor_info.slim_video.pclk;
	//imgsensor.video_mode = KAL_TRUE;
	imgsensor.line_length = imgsensor_info.slim_video.linelength;
	imgsensor.frame_length = imgsensor_info.slim_video.framelength;
	imgsensor.min_frame_length = imgsensor_info.slim_video.framelength;
	imgsensor.dummy_line = 0;
	imgsensor.dummy_pixel = 0;
	//imgsensor.current_fps = 300;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	slim_video_setting();
	return ERROR_NONE;
}   /*  s5k4h7yxfrontMIPIPreview   */



static kal_uint32 get_resolution(MSDK_SENSOR_RESOLUTION_INFO_STRUCT *sensor_resolution)
{
	LOG_INF("E\n");
	sensor_resolution->SensorFullWidth = imgsensor_info.cap.grabwindow_width;
	sensor_resolution->SensorFullHeight = imgsensor_info.cap.grabwindow_height;

	sensor_resolution->SensorPreviewWidth = imgsensor_info.pre.grabwindow_width;
	sensor_resolution->SensorPreviewHeight = imgsensor_info.pre.grabwindow_height;

	sensor_resolution->SensorVideoWidth = imgsensor_info.normal_video.grabwindow_width;
	sensor_resolution->SensorVideoHeight = imgsensor_info.normal_video.grabwindow_height;


	sensor_resolution->SensorHighSpeedVideoWidth	 = imgsensor_info.hs_video.grabwindow_width;
	sensor_resolution->SensorHighSpeedVideoHeight	 = imgsensor_info.hs_video.grabwindow_height;

  sensor_resolution->SensorSlimVideoWidth	 = imgsensor_info.slim_video.grabwindow_width;
	sensor_resolution->SensorSlimVideoHeight	 = imgsensor_info.slim_video.grabwindow_height;
	return ERROR_NONE;
}	/*	get_resolution	*/

static kal_uint32 get_info(enum MSDK_SCENARIO_ID_ENUM scenario_id,
					  MSDK_SENSOR_INFO_STRUCT *sensor_info,
					  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	LOG_INF("scenario_id = %d\n", scenario_id);


	//sensor_info->SensorVideoFrameRate = imgsensor_info.normal_video.max_framerate/10; /* not use */
	//sensor_info->SensorStillCaptureFrameRate= imgsensor_info.cap.max_framerate/10; /* not use */
	//imgsensor_info->SensorWebCamCaptureFrameRate= imgsensor_info.v.max_framerate; /* not use */

	sensor_info->SensorClockPolarity = SENSOR_CLOCK_POLARITY_LOW;
	sensor_info->SensorClockFallingPolarity = SENSOR_CLOCK_POLARITY_LOW; /* not use */
	sensor_info->SensorHsyncPolarity = SENSOR_CLOCK_POLARITY_LOW; // inverse with datasheet
	sensor_info->SensorVsyncPolarity = SENSOR_CLOCK_POLARITY_LOW;
	sensor_info->SensorInterruptDelayLines = 4; /* not use */
	sensor_info->SensorResetActiveHigh = FALSE; /* not use */
	sensor_info->SensorResetDelayCount = 5; /* not use */

	sensor_info->SensroInterfaceType = imgsensor_info.sensor_interface_type;
    sensor_info->MIPIsensorType = imgsensor_info.mipi_sensor_type;
    sensor_info->SettleDelayMode = imgsensor_info.mipi_settle_delay_mode;
	sensor_info->SensorOutputDataFormat = imgsensor_info.sensor_output_dataformat;

	sensor_info->CaptureDelayFrame = imgsensor_info.cap_delay_frame;
	sensor_info->PreviewDelayFrame = imgsensor_info.pre_delay_frame;
	sensor_info->VideoDelayFrame = imgsensor_info.video_delay_frame;
    sensor_info->HighSpeedVideoDelayFrame = imgsensor_info.hs_video_delay_frame;
    sensor_info->SlimVideoDelayFrame = imgsensor_info.slim_video_delay_frame;

	sensor_info->SensorMasterClockSwitch = 0; /* not use */
	sensor_info->SensorDrivingCurrent = imgsensor_info.isp_driving_current;

	sensor_info->AEShutDelayFrame = imgsensor_info.ae_shut_delay_frame; 		 /* The frame of setting shutter default 0 for TG int */
	sensor_info->AESensorGainDelayFrame = imgsensor_info.ae_sensor_gain_delay_frame;	/* The frame of setting sensor gain */
	sensor_info->AEISPGainDelayFrame = imgsensor_info.ae_ispGain_delay_frame;
	sensor_info->IHDR_Support = imgsensor_info.ihdr_support;
	sensor_info->IHDR_LE_FirstLine = imgsensor_info.ihdr_le_firstline;
	sensor_info->SensorModeNum = imgsensor_info.sensor_mode_num;

	sensor_info->SensorMIPILaneNumber = imgsensor_info.mipi_lane_num;
	sensor_info->SensorClockFreq = imgsensor_info.mclk;
	sensor_info->SensorClockDividCount = 3; /* not use */
	sensor_info->SensorClockRisingCount = 0;
	sensor_info->SensorClockFallingCount = 2; /* not use */
	sensor_info->SensorPixelClockCount = 3; /* not use */
	sensor_info->SensorDataLatchCount = 2; /* not use */

	sensor_info->MIPIDataLowPwr2HighSpeedTermDelayCount = 0;
	sensor_info->MIPICLKLowPwr2HighSpeedTermDelayCount = 0;
	sensor_info->SensorWidthSampling = 0;  // 0 is default 1x
	sensor_info->SensorHightSampling = 0;	// 0 is default 1x
	sensor_info->SensorPacketECCOrder = 1;

	switch (scenario_id) {
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
			sensor_info->SensorGrabStartX = imgsensor_info.pre.startx;
			sensor_info->SensorGrabStartY = imgsensor_info.pre.starty;

			sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.pre.mipi_data_lp2hs_settle_dc;

			break;
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			sensor_info->SensorGrabStartX = imgsensor_info.cap.startx;
			sensor_info->SensorGrabStartY = imgsensor_info.cap.starty;
			sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.cap.mipi_data_lp2hs_settle_dc;

			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:

			sensor_info->SensorGrabStartX = imgsensor_info.normal_video.startx;
			sensor_info->SensorGrabStartY = imgsensor_info.normal_video.starty;
			sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.normal_video.mipi_data_lp2hs_settle_dc;

			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			sensor_info->SensorGrabStartX = imgsensor_info.hs_video.startx;
			sensor_info->SensorGrabStartY = imgsensor_info.hs_video.starty;
			sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.hs_video.mipi_data_lp2hs_settle_dc; 

			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			sensor_info->SensorGrabStartX = imgsensor_info.slim_video.startx;
			sensor_info->SensorGrabStartY = imgsensor_info.slim_video.starty;

			sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.slim_video.mipi_data_lp2hs_settle_dc;

			break;
		default:
			sensor_info->SensorGrabStartX = imgsensor_info.pre.startx;
			sensor_info->SensorGrabStartY = imgsensor_info.pre.starty;

			sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.pre.mipi_data_lp2hs_settle_dc;
			break;
	}

	return ERROR_NONE;
}	/*	get_info  */


static kal_uint32 control(enum MSDK_SCENARIO_ID_ENUM scenario_id, MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
					  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	LOG_INF("scenario_id = %d\n", scenario_id);
	spin_lock(&imgsensor_drv_lock);
	imgsensor.current_scenario_id = scenario_id;
	spin_unlock(&imgsensor_drv_lock);
	switch (scenario_id) {
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
			preview(image_window, sensor_config_data);
			break;
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			capture(image_window, sensor_config_data);
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			normal_video(image_window, sensor_config_data);
			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			hs_video(image_window, sensor_config_data);
			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			slim_video(image_window, sensor_config_data);
			break;
		default:
			LOG_INF("Error ScenarioId setting");
			preview(image_window, sensor_config_data);
			return ERROR_INVALID_SCENARIO_ID;
	}
	return ERROR_NONE;
}	/* control() */



static kal_uint32 set_video_mode(UINT16 framerate)
	{
		LOG_INF("framerate = %d\n ", framerate);
		// SetVideoMode Function should fix framerate
		if (framerate == 0)
			// Dynamic frame rate
			return ERROR_NONE;
		spin_lock(&imgsensor_drv_lock);
		if ((framerate == 300) && (imgsensor.autoflicker_en == KAL_TRUE))
			imgsensor.current_fps = 296;
		else if ((framerate == 150) && (imgsensor.autoflicker_en == KAL_TRUE))
			imgsensor.current_fps = 146;
		else
			imgsensor.current_fps = framerate;
		spin_unlock(&imgsensor_drv_lock);
		set_max_framerate(imgsensor.current_fps,1);

		return ERROR_NONE;
	}


static kal_uint32 set_auto_flicker_mode(kal_bool enable, UINT16 framerate)
{
	LOG_INF("enable = %d, framerate = %d \n", enable, framerate);
	spin_lock(&imgsensor_drv_lock);
	if (enable) //enable auto flicker
		imgsensor.autoflicker_en = KAL_TRUE;
	else //Cancel Auto flick
		imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	return ERROR_NONE;
}


static kal_uint32 set_max_framerate_by_scenario(enum MSDK_SCENARIO_ID_ENUM scenario_id, MUINT32 framerate) 
{
	kal_uint32 frameHeight;

	LOG_INF("scenario_id = %d, framerate = %d\n", scenario_id, framerate);
				if(framerate == 0)
				return ERROR_NONE;
	switch (scenario_id) {
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
			frameHeight = imgsensor_info.pre.pclk / framerate * 10 / imgsensor_info.pre.linelength;
			spin_lock(&imgsensor_drv_lock);
			if (frameHeight > imgsensor_info.pre.framelength)
				imgsensor.dummy_line = frameHeight - imgsensor_info.pre.framelength;
			else
				imgsensor.dummy_line = 0;
			imgsensor.frame_length =imgsensor_info.pre.framelength + imgsensor.dummy_line;
			imgsensor.min_frame_length = imgsensor.frame_length;
			spin_unlock(&imgsensor_drv_lock);
			set_dummy();
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			if(framerate == 0)
				return ERROR_NONE;
			frameHeight = imgsensor_info.normal_video.pclk / framerate * 10 / imgsensor_info.normal_video.linelength;
			spin_lock(&imgsensor_drv_lock);
			if (frameHeight > imgsensor_info.normal_video.framelength)
				imgsensor.dummy_line = frameHeight - imgsensor_info.normal_video.framelength;
			else
				imgsensor.dummy_line = 0;
			imgsensor.frame_length = imgsensor_info.normal_video.framelength + imgsensor.dummy_line;
			imgsensor.min_frame_length = imgsensor.frame_length;
			spin_unlock(&imgsensor_drv_lock);

			set_dummy();
			break;
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
		//case MSDK_SCENARIO_ID_CAMERA_ZSD:
			if (imgsensor.current_fps == imgsensor_info.cap1.max_framerate) {
			    frameHeight = imgsensor_info.cap1.pclk / framerate * 10 / imgsensor_info.cap1.linelength;
			    spin_lock(&imgsensor_drv_lock);
			    if (frameHeight > imgsensor_info.cap1.framelength)
				    imgsensor.dummy_line = frameHeight - imgsensor_info.cap1.framelength;
			    else
				    imgsensor.dummy_line = 0;
			    imgsensor.frame_length = imgsensor_info.cap1.framelength + imgsensor.dummy_line;
			    imgsensor.min_frame_length = imgsensor.frame_length;
			    spin_unlock(&imgsensor_drv_lock);
			} else if (imgsensor.current_fps != imgsensor_info.cap.max_framerate) {
			    LOG_INF("Warning: current_fps %d fps is not support, so use cap's setting: %d fps!\n",framerate,imgsensor_info.cap.max_framerate/10);
			    frameHeight = imgsensor_info.cap.pclk / framerate * 10 / imgsensor_info.cap.linelength;
			    spin_lock(&imgsensor_drv_lock);
			    if (frameHeight > imgsensor_info.cap.framelength)
				    imgsensor.dummy_line = frameHeight - imgsensor_info.cap.framelength;
			    else
				    imgsensor.dummy_line = 0;
			    imgsensor.frame_length = imgsensor_info.cap.framelength + imgsensor.dummy_line;
			    imgsensor.min_frame_length = imgsensor.frame_length;
			    spin_unlock(&imgsensor_drv_lock);
			}

			set_dummy();
			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			frameHeight = imgsensor_info.hs_video.pclk / framerate * 10 / imgsensor_info.hs_video.linelength;
			spin_lock(&imgsensor_drv_lock);
			if (frameHeight > imgsensor_info.hs_video.framelength)
				imgsensor.dummy_line = frameHeight - imgsensor_info.hs_video.framelength;
			else
				imgsensor.dummy_line = 0;
			imgsensor.min_frame_length = imgsensor.frame_length;
			spin_unlock(&imgsensor_drv_lock);
			set_dummy();
			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			frameHeight = imgsensor_info.slim_video.pclk / framerate * 10 / imgsensor_info.slim_video.linelength;
			spin_lock(&imgsensor_drv_lock);
			if (frameHeight > imgsensor_info.slim_video.framelength)
				imgsensor.dummy_line = frameHeight - imgsensor_info.slim_video.framelength;
			else
				imgsensor.dummy_line = 0;
			imgsensor.frame_length =imgsensor_info.hs_video.framelength + imgsensor.dummy_line;
			imgsensor.min_frame_length = imgsensor.frame_length;
			spin_unlock(&imgsensor_drv_lock);
			set_dummy();
			break;
		default:  //coding with  preview scenario by default
			frameHeight = imgsensor_info.pre.pclk / framerate * 10 / imgsensor_info.pre.linelength;
			spin_lock(&imgsensor_drv_lock);
			if (frameHeight > imgsensor_info.pre.framelength)
				imgsensor.dummy_line = frameHeight - imgsensor_info.pre.framelength;
			else
				imgsensor.dummy_line = 0;
			imgsensor.frame_length =imgsensor_info.pre.framelength + imgsensor.dummy_line;
			imgsensor.min_frame_length = imgsensor.frame_length;
			spin_unlock(&imgsensor_drv_lock);
			set_dummy();
			LOG_INF("error scenario_id = %d, we use preview scenario \n", scenario_id);
			break;
	}
	return ERROR_NONE;
}


static kal_uint32 get_default_framerate_by_scenario(enum MSDK_SCENARIO_ID_ENUM scenario_id, MUINT32 *framerate)
{
	LOG_INF("scenario_id = %d\n", scenario_id);

	switch (scenario_id) {
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
			*framerate = imgsensor_info.pre.max_framerate;
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			*framerate = imgsensor_info.normal_video.max_framerate;
			break;
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			*framerate = imgsensor_info.cap.max_framerate;
			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			*framerate = imgsensor_info.hs_video.max_framerate;
			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			*framerate = imgsensor_info.slim_video.max_framerate;
			break;
		default:
			break;
	}

	return ERROR_NONE;
}

static kal_uint32 set_test_pattern_mode(kal_bool enable)
{
	LOG_INF("enable: %d\n", enable);

	if(enable)
		write_cmos_sensor_8(0x0601, 0x02);
	else
		write_cmos_sensor_8(0x0601, 0x00);

	spin_lock(&imgsensor_drv_lock);
	imgsensor.test_pattern = enable;
	spin_unlock(&imgsensor_drv_lock);
	return ERROR_NONE;
}

static kal_uint32 feature_control(MSDK_SENSOR_FEATURE_ENUM feature_id,
							 UINT8 *feature_para,UINT32 *feature_para_len)
{
	UINT16 *feature_return_para_16=(UINT16 *) feature_para;
	UINT16 *feature_data_16=(UINT16 *) feature_para;
	UINT32 *feature_return_para_32=(UINT32 *) feature_para;
	UINT32 *feature_data_32=(UINT32 *) feature_para;
	unsigned long long *feature_data=(unsigned long long *) feature_para;
	struct SENSOR_WINSIZE_INFO_STRUCT *wininfo;
	MSDK_SENSOR_REG_INFO_STRUCT *sensor_reg_data=(MSDK_SENSOR_REG_INFO_STRUCT *) feature_para;

	//LOG_INF("feature_id = %d\n", feature_id);
	switch (feature_id) {
		case SENSOR_FEATURE_GET_PERIOD:
			*feature_return_para_16++ = imgsensor.line_length;
			*feature_return_para_16 = imgsensor.frame_length;
			*feature_para_len=4;
			break;
		case SENSOR_FEATURE_GET_PIXEL_CLOCK_FREQ:
			*feature_return_para_32 = imgsensor.pclk;
			*feature_para_len=4;
			break;
                case SENSOR_FEATURE_GET_MIPI_PIXEL_RATE:
                        switch (*feature_data) {
                        case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
                                *(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
                                        imgsensor_info.cap.mipi_pixel_rate;
                                break;
                        case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
                                *(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
                                        imgsensor_info.normal_video.mipi_pixel_rate;
                                break;
                        case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
                                *(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
                                        imgsensor_info.hs_video.mipi_pixel_rate;
                                break;
                        case MSDK_SCENARIO_ID_SLIM_VIDEO:
                                *(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
                                        imgsensor_info.slim_video.mipi_pixel_rate;
                                break;
                        case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
                        default:
                                *(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
                                        imgsensor_info.pre.mipi_pixel_rate;
                                break;
                        }
                        break;
		case SENSOR_FEATURE_SET_ESHUTTER:
			set_shutter(*feature_data);
			break;
		case SENSOR_FEATURE_SET_NIGHTMODE:
			night_mode((BOOL) *feature_data);
			break;
		case SENSOR_FEATURE_SET_GAIN:
			set_gain((UINT16) *feature_data);
			break;
		case SENSOR_FEATURE_SET_FLASHLIGHT:
			break;
		case SENSOR_FEATURE_SET_ISP_MASTER_CLOCK_FREQ:
			break;
		case SENSOR_FEATURE_SET_REGISTER:
			write_cmos_sensor_8(sensor_reg_data->RegAddr, sensor_reg_data->RegData);
			break;
		case SENSOR_FEATURE_GET_REGISTER:
			sensor_reg_data->RegData = read_cmos_sensor(sensor_reg_data->RegAddr);
			break;
		case SENSOR_FEATURE_GET_LENS_DRIVER_ID:
			// get the lens driver ID from EEPROM or just return LENS_DRIVER_ID_DO_NOT_CARE
			// if EEPROM does not exist in camera module.
			*feature_return_para_32=LENS_DRIVER_ID_DO_NOT_CARE;
			*feature_para_len=4;
			break;
		case SENSOR_FEATURE_SET_VIDEO_MODE:
			set_video_mode(*feature_data);
			break;
		case SENSOR_FEATURE_CHECK_SENSOR_ID:
			get_imgsensor_id(feature_return_para_32);
			break; 
		case SENSOR_FEATURE_SET_AUTO_FLICKER_MODE:
			set_auto_flicker_mode((BOOL)*feature_data_16,*(feature_data_16+1));
			break;
		case SENSOR_FEATURE_SET_MAX_FRAME_RATE_BY_SCENARIO:
            set_max_framerate_by_scenario((enum MSDK_SCENARIO_ID_ENUM)*feature_data, *(feature_data+1));
			break;
		case SENSOR_FEATURE_GET_DEFAULT_FRAME_RATE_BY_SCENARIO:
			get_default_framerate_by_scenario((enum MSDK_SCENARIO_ID_ENUM)*(feature_data), (MUINT32 *)(uintptr_t)(*(feature_data+1)));
			//get_default_framerate_by_scenario((MSDK_SCENARIO_ID_ENUM)*feature_data_32, (MUINT32 *)(*(feature_data_32+1)));
			break;
		case SENSOR_FEATURE_SET_TEST_PATTERN:
			set_test_pattern_mode((BOOL)*feature_data);
			break;
		case SENSOR_FEATURE_GET_TEST_PATTERN_CHECKSUM_VALUE: //for factory mode auto testing
			*feature_return_para_32 = imgsensor_info.checksum_value;
			*feature_para_len=4;
			break;
		case SENSOR_FEATURE_SET_FRAMERATE:
            LOG_INF("current fps :%d\n", (UINT32)*feature_data);
			spin_lock(&imgsensor_drv_lock);
			imgsensor.current_fps = *feature_data;
			spin_unlock(&imgsensor_drv_lock);
			break;
//		case SENSOR_FEATURE_SET_HDR:
	//		LOG_INF("ihdr enable :%d\n", *feature_data_16);
	//		spin_lock(&imgsensor_drv_lock);
//			imgsensor.ihdr_en = *feature_data_16;
	//		spin_unlock(&imgsensor_drv_lock);
//			break;
		case SENSOR_FEATURE_GET_CROP_INFO:
			//LOG_INF("SENSOR_FEATURE_GET_CROP_INFO scenarioId:%d\n", *feature_data_32);
			//wininfo = (SENSOR_WINSIZE_INFO_STRUCT *)(*(feature_data_32+1));
			wininfo = (struct SENSOR_WINSIZE_INFO_STRUCT *)(uintptr_t)(*(feature_data+1));
			switch (*feature_data_32) {
				case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
					memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[1],sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
					break;
				case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
					memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[2],sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
					break;
				case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
					memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[3],sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
					break;
				case MSDK_SCENARIO_ID_SLIM_VIDEO:
					memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[4],sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
					break;
				case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
				default:
					memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[0],sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
					break;
			}
		case SENSOR_FEATURE_SET_IHDR_SHUTTER_GAIN:
            LOG_INF("SENSOR_SET_SENSOR_IHDR LE=%d, SE=%d, Gain=%d\n",(UINT16)*feature_data,(UINT16)*(feature_data+1),(UINT16)*(feature_data+2));
            //ihdr_write_shutter_gain((UINT16)*feature_data,(UINT16)*(feature_data+1),(UINT16)*(feature_data+2));
			break;
		case SENSOR_FEATURE_SET_STREAMING_SUSPEND:
			LOG_INF("SENSOR_FEATURE_SET_STREAMING_SUSPEND\n");
			streaming_control(KAL_FALSE);
			break;
		case SENSOR_FEATURE_SET_STREAMING_RESUME:
			LOG_INF("SENSOR_FEATURE_SET_STREAMING_RESUME, shutter:%llu\n", *feature_data);
			if (*feature_data != 0)
				set_shutter(*feature_data);
			streaming_control(KAL_TRUE);
			break;
		default:
			break;
	}

	return ERROR_NONE;
}	/*	feature_control()  */

static struct SENSOR_FUNCTION_STRUCT sensor_func = {
	open,
	get_info,
	get_resolution,
	feature_control,
	control,
	close
};

UINT32 S5K4H7YXFRONT_MIPI_RAW_SensorInit(struct SENSOR_FUNCTION_STRUCT **pfFunc)
{
	/* To Do : Check Sensor status here */
	if (pfFunc!=NULL)
		*pfFunc=&sensor_func;
	return ERROR_NONE;
}	/*	s5k4h7yxfront_MIPI_RAW_SensorInit	*/
