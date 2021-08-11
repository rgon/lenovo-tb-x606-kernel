/************************************************************************
 *
 *  WILLSEMI TypeC Chipset Driver for Linux & Android.
 *
 *
 * ######################################################################
 *
 *  Author: lei.huang (lhuang@sh-willsemi.com)
 *
 * Copyright (c) 2019, WillSemi Inc. All rights reserved.
 *
 ************************************************************************/

/************************************************************************
 *  Include files
 ************************************************************************/
#define __TEST_CC_PATCH__
#define __WITH_KERNEL_VER4__
//#define __RESET_PATCH__
#define __DEBOUNCE_TIMER__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/power_supply.h>
#include <linux/irq.h>
#include <linux/delay.h>
#ifndef __WITH_KERNEL_VER4__
//#include <linux/wakelock.h>
#endif /* __WITH_KERNEL_VER4__ */
#include <linux/workqueue.h>
#ifdef __DEBOUNCE_TIMER__
#include	<linux/kthread.h>
#endif /* __DEBOUNCE_TIMER__ */


//#include <mt-plat/mt_gpio.h>
//#include <mt-plat/mt_gpio_core.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>





#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>




/*TODO:  Define GPIO IRQ here or use DTS to describe interrupt*/
#if 0
#ifndef WUSB3801X_INT_PIN
#define WUSB3801X_INT_PIN 15
//#error "No Interrupt GPIO specified."
#endif /*WUSB3801X_INT_PIN*/
#endif



/***************************************************************************************************************************
* Example DTS for msm platform, User shall change the interrupt type, number, gpio ... according to their speific board design. 
****************************************************************************************************************************
        i2c@78b6000 {
                wusb3801@60 {
                        compatible = "qcom,wusb3801";
                        reg = <0xc0>;
                        qcom,irq-gpio = <&msm_gpio 21 0x8008>;
                        interrupt-parent = <&msm_gpio>;
                        interrupts = <21 0>;
                        interrupt-names = "wusb3801_int_irq";
                        wusb3801,irq-gpio = <&msm_gpio 21 0x8008>;
                        wusb3801,reset-gpio = <&msm_gpio 12 0x0>;
                        wusb3801,init-mode = <0x24>;
                        wusb3801,host-current = <0x01>;
                        wusb3801,drp-toggle-time = <40>;
                };
        };
****************************************************************************************************************************
*/


/**
 * Options to enable force detection feature for DRP
 */

/*#define __WITH_POWER_BANK_MODE__*/

/*
 *Bit operations if we don't want to include #include <linux/bitops.h>
 */

#undef  __CONST_FFS
#define __CONST_FFS(_x) \
        ((_x) & 0x0F ? ((_x) & 0x03 ? ((_x) & 0x01 ? 0 : 1) :\
                                      ((_x) & 0x04 ? 2 : 3)) :\
                       ((_x) & 0x30 ? ((_x) & 0x10 ? 4 : 5) :\
                                      ((_x) & 0x40 ? 6 : 7)))

#undef  FFS
#define FFS(_x) \
        ((_x) ? __CONST_FFS(_x) : 0)

#undef  BITS
#define BITS(_end, _start) \
        ((BIT(_end) - BIT(_start)) + BIT(_end))

#undef  __BITS_GET
#define __BITS_GET(_byte, _mask, _shift) \
        (((_byte) & (_mask)) >> (_shift))

#undef  BITS_GET
#define BITS_GET(_byte, _bit) \
        __BITS_GET(_byte, _bit, FFS(_bit))

#undef  __BITS_SET
#define __BITS_SET(_byte, _mask, _shift, _val) \
        (((_byte) & ~(_mask)) | (((_val) << (_shift)) & (_mask)))

#undef  BITS_SET
#define BITS_SET(_byte, _bit, _val) \
        __BITS_SET(_byte, _bit, FFS(_bit), _val)

#undef  BITS_MATCH
#define BITS_MATCH(_byte, _bit) \
        (((_byte) & (_bit)) == (_bit))

/* Register Map */

#define WUSB3801_REG_VERSION_ID         0x01
#define WUSB3801_REG_CONTROL0           0x02
#define WUSB3801_REG_INTERRUPT          0x03
#define WUSB3801_REG_STATUS             0x04
#define WUSB3801_REG_CONTROL1           0x05
#define WUSB3801_REG_TEST0              0x06
#define WUSB3801_REG_TEST_01            0x07
#define WUSB3801_REG_TEST_02            0x08
#define WUSB3801_REG_TEST_03            0x09
#define WUSB3801_REG_TEST_04            0x0A
#define WUSB3801_REG_TEST_05            0x0B
#define WUSB3801_REG_TEST_06            0x0C
#define WUSB3801_REG_TEST_07            0x0D
#define WUSB3801_REG_TEST_08            0x0E
#define WUSB3801_REG_TEST_09            0x0F
#define WUSB3801_REG_TEST_0A            0x10
#define WUSB3801_REG_TEST_0B            0x11
#define WUSB3801_REG_TEST_0C            0x12
#define WUSB3801_REG_TEST_0D            0x13
#define WUSB3801_REG_TEST_0E            0x14
#define WUSB3801_REG_TEST_0F            0x15
#define WUSB3801_REG_TEST_10            0x16
#define WUSB3801_REG_TEST_11            0x17
#define WUSB3801_REG_TEST_12            0x18


#define WUSB3801_SLAVE_ADDR0            0xc0
#define WUSB3801_SLAVE_ADDR1            0xd0


/*Available modes*/
#define WUSB3801_DRP_ACC                (BIT_REG_CTRL0_RLE_DRP)
#define WUSB3801_DRP                    (BIT_REG_CTRL0_RLE_DRP | BIT_REG_CTRL0_DIS_ACC)
#define WUSB3801_SNK_ACC                (BIT_REG_CTRL0_RLE_SNK)
#define WUSB3801_SNK                    (BIT_REG_CTRL0_RLE_SNK | BIT_REG_CTRL0_DIS_ACC)
#define WUSB3801_SRC_ACC                (BIT_REG_CTRL0_RLE_SRC)
#define WUSB3801_SRC                    (BIT_REG_CTRL0_RLE_SRC | BIT_REG_CTRL0_DIS_ACC)
#define WUSB3801_DRP_PREFER_SRC_ACC     (WUSB3801_DRP_ACC | BIT_REG_CTRL0_TRY_SRC)
#define WUSB3801_DRP_PREFER_SRC         (WUSB3801_DRP     | BIT_REG_CTRL0_TRY_SRC)
#define WUSB3801_DRP_PREFER_SNK_ACC     (WUSB3801_DRP_ACC | BIT_REG_CTRL0_TRY_SNK)    
#define WUSB3801_DRP_PREFER_SNK         (WUSB3801_DRP     | BIT_REG_CTRL0_TRY_SNK)


/*TODO: redefine your prefer role here*/
#define WUSB3801_INIT_MODE              (WUSB3801_DRP_PREFER_SNK_ACC)

/*Registers relevant values*/                     
#define WUSB3801_VENDOR_ID              0x06

/*Switch to enable/disable feature of specified Registers*/
#define BIT_REG_CTRL0_DIS_ACC           (0x01 << 7)
#define BIT_REG_CTRL0_TRY_SRC           (0x02 << 5)
#define BIT_REG_CTRL0_TRY_SNK           (0x01 << 5)
#define BIT_REG_CTRL0_CUR_DEF           (0x00 << 3)
#define BIT_REG_CTRL0_CUR_1P5           (0x01 << 3)
#define BIT_REG_CTRL0_CUR_3P0           (0x02 << 3)
#define BIT_REG_CTRL0_RLE_SNK           (0x00 << 1)
#define BIT_REG_CTRL0_RLE_SRC           (0x01 << 1)
#define BIT_REG_CTRL0_RLE_DRP           (0x02 << 1)
#define BIT_REG_CTRL0_INT_MSK           (0x01 << 0)


#define BIT_REG_STATUS_VBUS             (0x01 << 7)
#define BIT_REG_STATUS_STANDBY          (0x00 << 5)
#define BIT_REG_STATUS_CUR_DEF          (0x01 << 5)
#define BIT_REG_STATUS_CUR_MID          (0x02 << 5)
#define BIT_REG_STATUS_CUR_HIGH         (0x03 << 5)

#define BIT_REG_STATUS_ATC_STB          (0x00 << 1)
#define BIT_REG_STATUS_ATC_SNK          (0x01 << 1)
#define BIT_REG_STATUS_ATC_SRC          (0x02 << 1)
#define BIT_REG_STATUS_ATC_ACC          (0x03 << 1)
#define BIT_REG_STATUS_ATC_DACC         (0x04 << 1)

#define BIT_REG_STATUS_PLR_STB          (0x00 << 0)
#define BIT_REG_STATUS_PLR_CC1          (0x01 << 0)
#define BIT_REG_STATUS_PLR_CC2          (0x02 << 0)
#define BIT_REG_STATUS_PLR_BOTH         (0x03 << 0)

#define BIT_REG_CTRL1_SW02_DIN          (0x01 << 4)
#define BIT_REG_CTRL1_SW02_EN           (0x01 << 3)
#define BIT_REG_CTRL1_SW01_DIN          (0x01 << 2)
#define BIT_REG_CTRL1_SW01_EN           (0x01 << 1)
#define BIT_REG_CTRL1_SM_RST            (0x01 << 0)



#define BIT_REG_TEST02_FORCE_ERR_RCY    (0x01)

#define WUSB3801_WAIT_VBUS               0x40
/*Fixed duty cycle period. 40ms:40ms*/
#define WUSB3801_TGL_40MS                0
#define WUSB3801_HOST_DEFAULT            0
#define WUSB3801_HOST_1500MA             1
#define WUSB3801_HOST_3000MA             2
#define WUSB3801_INT_ENABLE              0x00
#define WUSB3801_INT_DISABLE             0x01
#define WUSB3801_DISABLED                0x0A
#define WUSB3801_ERR_REC                 0x01
#define WUSB3801_VBUS_OK                 0x80

#define WUSB3801_SNK_0MA                (0x00 << 5)
#define WUSB3801_SNK_DEFAULT            (0x01 << 5)
#define WUSB3801_SNK_1500MA             (0x02 << 5)
#define WUSB3801_SNK_3000MA             (0x03 << 5)
#define WUSB3801_ATTACH                  0x1C

//#define WUSB3801_TYPE_PWR_ACC           (0x00 << 2) /*Ra/Rd treated as Open*/
#define WUSB3801_TYPE_INVALID           (0x00)
#define WUSB3801_TYPE_SNK               (0x01 << 2)
#define WUSB3801_TYPE_SRC               (0x02 << 2)
#define WUSB3801_TYPE_AUD_ACC           (0x03 << 2)
#define WUSB3801_TYPE_DBG_ACC           (0x04 << 2)

#define WUSB3801_INT_DETACH              (0x01 << 1)
#define WUSB3801_INT_ATTACH              (0x01 << 0)

#define WUSB3801_REV20                   0x02

/* Masks for Read-Modified-Write operations*/
#define WUSB3801_HOST_CUR_MASK           0x18  /*Host current for IIC*/
#define WUSB3801_INT_MASK                0x01
#define WUSB3801_BCLVL_MASK              0x60
#define WUSB3801_TYPE_MASK               0x1C
#define WUSB3801_MODE_MASK               0xE6  /*Roles relevant bits*/
#define WUSB3801_INT_STS_MASK            0x03
#define WUSB3801_FORCE_ERR_RCY_MASK      0x80  /*Force Error recovery*/
#define WUSB3801_ROLE_MASK               0x06
#define WUSB3801_VENDOR_ID_MASK          0x07
#define WUSB3801_VERSION_ID_MASK         0xF8
#define WUSB3801_VENDOR_SUB_ID_MASK         0xA0
#define WUSB3801_POLARITY_CC_MASK        0x03
#define WUSB3801_CC_STS_MASK            0x03


/* WUSB3801 STATES MACHINES */
#define WUSB3801_STATE_DISABLED             0x00
#define WUSB3801_STATE_ERROR_RECOVERY       0x01
#define WUSB3801_STATE_UNATTACHED_SNK       0x02
#define WUSB3801_STATE_UNATTACHED_SRC       0x03
#define WUSB3801_STATE_ATTACHWAIT_SNK       0x04
#define WUSB3801_STATE_ATTACHWAIT_SRC       0x05
#define WUSB3801_STATE_ATTACHED_SNK         0x06
#define WUSB3801_STATE_ATTACHED_SRC         0x07
#define WUSB3801_STATE_AUDIO_ACCESSORY      0x08
#define WUSB3801_STATE_DEBUG_ACCESSORY      0x09
#define WUSB3801_STATE_TRY_SNK              0x0A
#define WUSB3801_STATE_TRYWAIT_SRC          0x0B
#define WUSB3801_STATE_TRY_SRC              0x0C
#define WUSB3801_STATE_TRYWAIT_SNK          0x0D

#define WUSB3801_CC2_CONNECTED 1
#define WUSB3801_CC1_CONNECTED 0

/* wake lock timeout in ms */
#define WUSB3801_WAKE_LOCK_TIMEOUT          1000
/*1.5 Seconds timeout for force detection*/
#define ROLE_SWITCH_TIMEOUT		              1500
#define DEBOUNCE_TIME_OUT 	50





/*Private data*/
typedef struct wusb3801_data
{
	uint32_t  int_gpio;
	uint8_t  init_mode;
	uint8_t  dfp_power;
	uint8_t  dttime;
}wusb3801_data_t;
#ifdef  __WITH_KERNEL_VER4__
struct wusb3801_wakeup_source {
        struct wakeup_source    source;
        unsigned long           enabled_bitmap;
        spinlock_t              ws_lock;
};
#endif /* __WITH_KERNEL_VER4__ */
#ifdef __DEBOUNCE_TIMER__
typedef struct __WUSB3801_DRV_TIMER
{
	struct timer_list tl;
	void (*timer_function) (void *context);
	void *function_context;
	uint32_t time_period;
	uint8_t timer_is_canceled;
} WUSB3801_DRV_TIMER, *PWUSB3801_DRV_TIMER;

typedef struct
{
	struct task_struct *task;
	struct completion comp;
	pid_t pid;
	void *chip;
} wusb3801_thread;
#endif /* __DEBOUNCE_TIMER__ */
/*Working context structure*/
typedef struct wusb3801_chip
{
	struct      i2c_client *client;
	struct      wusb3801_data *pdata;
	struct      workqueue_struct  *cc_wq;
	int         irq_gpio;
	int         ufp_power;
#ifdef 	__TEST_CC_PATCH__
	//uint8_t     reg4_val;
	uint8_t     cc_test_flag;
	uint8_t     cc_sts;
#endif	/* __TEST_CC_PATCH__ */
	uint8_t     mode;
	uint8_t     dev_id;
	uint8_t     dev_sub_id;
	uint8_t     type;
	uint8_t     state;
	uint8_t     bc_lvl;
	uint8_t     dfp_power;
	uint8_t     dttime;
	uint8_t     attached;
	uint8_t     init_state;
	uint8_t     defer_init;	//work round the additional interrupt caused by mode change
	int         try_attcnt;
	struct      work_struct dwork;
#ifdef __WITH_KERNEL_VER4__
	struct wusb3801_wakeup_source    wusb3801_ws;
#else /* __WITH_KERNEL_VER4__ */
	struct      wake_lock wlock;
#endif /* __WITH_KERNEL_VER4__ */
	struct      mutex mlock;
	struct      power_supply *usb_psy;

#ifdef __DEBOUNCE_TIMER__
	uint8_t chip_remove;
	WUSB3801_DRV_TIMER current_timer;
	wusb3801_thread thread;
#endif /* __DEBOUNCE_TIMER__ */
}wusb3801_chip_t;

#define wusb3801_update_state(chip, st)                      \
	if(chip) {                                                 \
		chip->state = st;                                        \
		dev_info(&chip->client->dev, "%s: %s\n", __func__, #st); \
		wake_up_interruptible(&mode_switch);                     \
	}

#define STR(s)    #s
#define STRV(s)   STR(s)
static int wusb3801_power_set_icurrent_max(struct wusb3801_chip *chip,
						int icurrent);
static int wusb3801_reset_device(struct wusb3801_chip *chip);
static void wusb3801_detach(struct wusb3801_chip *chip);
//static unsigned wusb3801_is_vbus_on(struct wusb3801_chip *chip);
DECLARE_WAIT_QUEUE_HEAD(mode_switch);

#ifdef __DEBOUNCE_TIMER__
/* OS task reference */
static inline void
wusb3801_activate_thread(wusb3801_thread * thr)
{
	/** Record the thread pid */
	thr->pid = current->pid;
	init_completion(&thr->comp);
}

static inline void
wusb3801_deactivate_thread(wusb3801_thread * thr)
{
	/* Reset the pid */
	thr->pid = 0;
}

static inline void
wusb3801_create_thread(int (*wusb3801func) (void *), wusb3801_thread * thr, char *name)
{
	thr->task = kthread_run(wusb3801func, thr, "%s", name);
}

static inline int
wusb3801_terminate_thread(wusb3801_thread * thr)
{
	/* Check if the thread is active or not */
	if (!thr->pid) {
		printk(KERN_INFO "Thread does not exist\n");
		return -1;
	}
	kthread_stop(thr->task);
	return 0;
}

/* OS timer reference */
#define FreeTimer(x)	do {} while (0)
static inline void
wusb3801_set_timer(PWUSB3801_DRV_TIMER timer, uint32_t MillisecondPeriod)
{
	timer->time_period = MillisecondPeriod;
	//timer->tl.expires = jiffies +  (MillisecondPeriod * HZ) / 1000;
	timer->tl.expires = jiffies +  msecs_to_jiffies(MillisecondPeriod);
	add_timer(&timer->tl);
	timer->timer_is_canceled = false;
}

static inline void
wusb3801_mod_timer(PWUSB3801_DRV_TIMER timer, uint32_t MillisecondPeriod)
{
	timer->time_period = MillisecondPeriod;
	//mod_timer(&timer->tl, jiffies + (MillisecondPeriod * HZ) / 1000);
	mod_timer(&timer->tl, jiffies + msecs_to_jiffies(MillisecondPeriod));
	timer->timer_is_canceled = false;
}

static inline void
wusb3801_cancel_timer(WUSB3801_DRV_TIMER * timer)
{
	if(!timer->timer_is_canceled){
		del_timer(&timer->tl);
		timer->timer_is_canceled = true;
	}
}

void wusb3801_timer_handler(unsigned long fcontext)
{
	PWUSB3801_DRV_TIMER timer = (PWUSB3801_DRV_TIMER) fcontext;
	struct wusb3801_chip * chip = NULL;
	struct device *cdev = NULL;
	if(timer == NULL){
		printk("%s timer is null\n",  __func__);
		return;
	}
	chip = (struct wusb3801_chip *)timer->function_context;
	if(chip == NULL){
		printk("%s chip is null\n",  __func__);
		return;
	}
	cdev = &chip->client->dev;
	if(cdev == NULL){
		printk("%s cdev is null\n",  __func__);
		return;
	}
	complete(&chip->thread.comp);
	dev_info(cdev, "%s: [mod timer] end jiffies %lu\n", __func__, jiffies);
	wusb3801_cancel_timer(timer);
	return;
}

static inline void
wusb3801_initialize_timer(PWUSB3801_DRV_TIMER timer,
					  void (*TimerFunction) (void *context),
					  void *FunctionContext)
{
	/* first, setup the timer to trigger the wusb3801_timer_handler proxy */
	init_timer(&timer->tl);
	timer->tl.function = wusb3801_timer_handler;
	timer->tl.data = (unsigned long) timer;
	timer->tl.expires = jiffies + HZ;
	/* then tell the proxy which function to call and what to pass it */
	timer->timer_function = TimerFunction;
	timer->function_context = FunctionContext;
	timer->timer_is_canceled = true;
}
static inline void
wusb3801_sched_timeout(uint32_t millisec)
{
	unsigned long timeout = 0, expires = 0;
	expires = jiffies + msecs_to_jiffies(millisec);
	timeout = millisec;

	while(timeout)
	{
		timeout = schedule_timeout(timeout);

		if(time_after(jiffies, expires))
			break;
	}
}

static void wusb3801_read_current(struct wusb3801_chip *chip){
 	struct device *cdev = &chip->client->dev;
	int rc, limit;
	uint8_t status, type;

	rc = i2c_smbus_read_byte_data(chip->client,
				WUSB3801_REG_STATUS);
	if (rc<0) {
		dev_err(cdev, "%s: failed to read\n", __func__);
		if(wusb3801_reset_device(chip)< 0)
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}

	status = (rc & WUSB3801_TYPE_MASK) ? 1 : 0;
	type = status  ? rc & WUSB3801_TYPE_MASK : WUSB3801_TYPE_INVALID;
	dev_dbg(cdev, "%s: sts[0x%02x], type[0x%02x]\n",__func__, status, type);
	limit = (chip->bc_lvl == WUSB3801_SNK_3000MA ? 3000 :
		(chip->bc_lvl == WUSB3801_SNK_1500MA ? 1500 : 0));
	dev_dbg(cdev, "%s: limit = %d\n",__func__, limit);
	wusb3801_power_set_icurrent_max(chip, limit);
	return;
}

int wusb3801_read_thread(void *data)
{
	wusb3801_thread *thread = (wusb3801_thread *)data;
	struct wusb3801_chip *chip = thread->chip;
	pr_err("%s thread start\n", __func__);
	wusb3801_activate_thread(thread);
	current->flags |= PF_NOFREEZE;
	while(1){
		wait_for_completion_interruptible(&thread->comp);
		if (kthread_should_stop()){
			pr_info("%s: break from main thread \n", __func__);
			break;
		}
		if(chip->chip_remove == 1)
			break;
		wusb3801_read_current(chip);
	}

	wusb3801_deactivate_thread(thread);
	return 0;
}
#endif /* __DEBOUNCE_TIMER__ */
/******************************************************/
#ifdef  __WITH_KERNEL_VER4__
#if 0
static void wusb3801_stay_awake(struct wusb3801_wakeup_source *source,
                                        enum wakeup_src wk_src)
{
        unsigned long flags;
        spin_lock_irqsave(&source->ws_lock, flags);
        if (!__test_and_set_bit(wk_src, &source->enabled_bitmap))
                __pm_stay_awake(&source->source);
        spin_unlock_irqrestore(&source->ws_lock, flags);
}
static void wusb3801_relax(struct wusb3801_wakeup_source *source,
                                        enum wakeup_src wk_src)
{
        unsigned long flags;
        spin_lock_irqsave(&source->ws_lock, flags);
        if (__test_and_clear_bit(wk_src, &source->enabled_bitmap) &&
                !(source->enabled_bitmap & WAKEUP_SRC_MASK)) {
                __pm_relax(&source->source);
        }
        spin_unlock_irqrestore(&source->ws_lock, flags);
}
#endif
static void wusb3801_wakeup_src_init(struct wusb3801_chip *chip)
{
        spin_lock_init(&chip->wusb3801_ws.ws_lock);
        wakeup_source_init(&chip->wusb3801_ws.source, "wusb3801");
}
#endif /* __WITH_KERNEL_VER4__ */

/************************************************************************
 *
 *       wusb3801_write_masked_byte
 *
 *  Description :
 *  -------------
 *  Read-Modified-Writeback operation through I2C communication.
 *
 *  Parameter     :
 *  -----------
 *  Client         :    Pointer to I2C client.
 *  Addr           :    Address of internal register of slave device
 *  Mask           :    Mask to prevent irrelevant bit changes
 *  val            :    Data wants to write.
 *  Return values  :    Zero if no error, else error code.
 *  ---------------
 *  None
 *
 ************************************************************************/

static int wusb3801_write_masked_byte(struct i2c_client *client,
					uint8_t addr, uint8_t mask, uint8_t val)
{
	int rc;
	if (!mask){
		/* no actual access */
		rc = -EINVAL;
		goto out;
	}
	rc = i2c_smbus_read_byte_data(client, addr);
	if (rc<0)
	{
	}
	else
	{
		rc = i2c_smbus_write_byte_data(client,
			addr, BITS_SET((uint8_t)rc, mask, val));
	}
out:
	return rc;
}

static int wusb3801_read_device_id(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc;
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_VERSION_ID);
	if (rc<0)
		return rc;
	dev_info(cdev, "VendorID register: 0x%02x\n", rc );
	if((rc & WUSB3801_VENDOR_ID_MASK) != WUSB3801_VENDOR_ID){
		return -EINVAL;
	}
	chip->dev_id = rc;
	dev_info(cdev, "Vendor id: 0x%02x, Version id: 0x%02x\n", rc & WUSB3801_VENDOR_ID_MASK,
	                                                         (rc & WUSB3801_VERSION_ID_MASK) >> 3);
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_TEST_01);
	if (rc<0)
		return rc;
	chip->dev_sub_id = rc & WUSB3801_VENDOR_SUB_ID_MASK;
	dev_info(cdev, "VendorSUBID register: 0x%02x\n", rc & WUSB3801_VENDOR_SUB_ID_MASK );
	return 0;
}

static int wusb3801_update_status(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc;

	/* Get control0 register */
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_CONTROL0);
	if (rc<0) {
		dev_err(cdev, "%s: fail to read mode\n", __func__);
		return rc;
	}

	chip->mode      =  rc & WUSB3801_MODE_MASK;
	chip->dfp_power =  BITS_GET(rc, WUSB3801_HOST_CUR_MASK);
	chip->dttime    =  WUSB3801_TGL_40MS;

	return 0;
}


static int wusb3801_check_modes(uint8_t mode)
{
	switch(mode){
		case WUSB3801_DRP_ACC            :
		case WUSB3801_DRP                :
		case WUSB3801_SNK_ACC            :
		case WUSB3801_SNK                :
		case WUSB3801_SRC_ACC            :
		case WUSB3801_SRC                :
		case WUSB3801_DRP_PREFER_SRC_ACC :
		case WUSB3801_DRP_PREFER_SRC     :
		case WUSB3801_DRP_PREFER_SNK_ACC :
		case WUSB3801_DRP_PREFER_SNK     :
			return 0;
			break;
		default:
			break;
	}
	return -EINVAL;
}

/*
static int wusb3801_reset_chip_to_error_recovery(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	rc = i2c_smbus_write_byte_data(chip->client,
			WUSB3801_REG_CONTROL1, BIT_REG_CTRL1_SM_RST);

	if (IS_ERR_VALUE(rc))
	{
		dev_err(cdev, "failed to write manual(%d)\n", rc);
	}

	return rc;
}
*/

/*
 *  Spec lets transitioning to below states from any state
 *  WUSB3801_STATE_DISABLED
 *  WUSB3801_STATE_ERROR_RECOVERY
 */
static int wusb3801_set_chip_state(struct wusb3801_chip *chip, uint8_t state)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	if(state > WUSB3801_STATE_UNATTACHED_SRC)
		return -EINVAL;


  rc = i2c_smbus_write_byte_data(chip->client,
			   WUSB3801_REG_CONTROL1,
			   (state == WUSB3801_STATE_DISABLED) ? \
			             WUSB3801_DISABLED :        \
			             0);

	if (rc<0) {
		dev_err(cdev, "failed to write state machine(%d)\n", rc);
	}

	chip->init_state = state;

	return rc;
}


static int wusb3801_set_mode(struct wusb3801_chip *chip, uint8_t mode)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	if (mode != chip->mode) {
		/*
		rc = wusb3801_write_masked_byte(chip->client,
				WUSB3801_REG_CONTROL0,
				WUSB3801_MODE_MASK,
				mode);
				*/
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_CONTROL0);
	if (rc<0) {
		dev_err(cdev, "%s: fail to read mode\n", __func__);
		return rc;
	}
	rc &= ~WUSB3801_MODE_MASK;
	//rc |= mode;
	rc |= (mode | WUSB3801_INT_MASK);//Disable the chip interrupt
 	rc = i2c_smbus_write_byte_data(chip->client,
			   WUSB3801_REG_CONTROL0, rc);

	if (rc<0) {
		dev_err(cdev, "failed to write mode(%d)\n", rc);
		return rc;
	}

	//Clear the chip interrupt
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_INTERRUPT);
	if (rc<0) {
		dev_err(cdev, "%s: fail to clear chip interrupt\n", __func__);
		return rc;
	}

	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_CONTROL0);
	if (rc<0) {
		dev_err(cdev, "%s: fail to read chip interrupt\n", __func__);
		return rc;
	}
	rc &= ~WUSB3801_INT_MASK;//enable the chip interrupt
	rc = i2c_smbus_write_byte_data(chip->client,
			   WUSB3801_REG_CONTROL0, rc);

	if (rc<0) {
		dev_err(cdev, "failed to enable chip interrupt(%d)\n", rc);
		return rc;
	}

	chip->mode = mode;
  }

 	dev_dbg(cdev, "%s: mode (0x%02x) (0x%02x)\n", __func__, chip->mode , mode);

	return rc;
}

static int wusb3801_set_dfp_power(struct wusb3801_chip *chip, uint8_t hcurrent)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	if (hcurrent == chip->dfp_power) {
		dev_dbg(cdev, "vaule is not updated(%d)\n",
					hcurrent);
		return rc;
	}

	rc = wusb3801_write_masked_byte(chip->client,
					WUSB3801_REG_CONTROL0,
					WUSB3801_HOST_CUR_MASK,
					hcurrent);
	if (rc<0) {
		dev_err(cdev, "failed to write current(%d)\n", rc);
		return rc;
	}

	chip->dfp_power = hcurrent;

	dev_dbg(cdev, "%s: host current(%d)\n", __func__, hcurrent);

	return rc;
}

/**********************************************************************
 * When 3A capable DRP device is connected without VBUS,
 * DRP always detect it as SINK device erroneously.
 * Since USB Type-C specification 1.0 and 1.1 doesn't
 * consider this corner case, apply workaround for this case.
 * Set host mode current to 1.5A initially, and then change
 * it to default USB current right after detection SINK port.
 ***********************************************************************/
static int wusb3801_init_force_dfp_power(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	rc = wusb3801_write_masked_byte(chip->client,
					WUSB3801_REG_CONTROL0,
					WUSB3801_HOST_CUR_MASK,
					WUSB3801_HOST_3000MA);
	if (rc<0) {
		dev_err(cdev, "failed to write current\n");
		return rc;
	}

	chip->dfp_power = WUSB3801_HOST_3000MA;

	dev_dbg(cdev, "%s: host current (%d)\n", __func__, rc);

	return rc;
}

/************************************************************************
 *
 *       wusb3801_set_toggle_time
 *
 *  Description :
 *  -------------
 *  Wusb3801 varints only support for fixed duty cycles periods (40ms:40ms)
 *
 ************************************************************************/
static int wusb3801_set_toggle_time(struct wusb3801_chip *chip, uint8_t toggle_time)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	if (toggle_time != WUSB3801_TGL_40MS) {
		dev_err(cdev, "toggle_time(%d) is unavailable\n", toggle_time);
		return -EINVAL;
	}

	chip->dttime = WUSB3801_TGL_40MS;

	dev_dbg(cdev, "%s: Fixed toggle time (%d)\n", __func__, chip->dttime);

	return rc;
}


static int wusb3801_init_reg(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	/* change current */
	rc = wusb3801_init_force_dfp_power(chip);
	if (rc<0)
		dev_err(cdev, "%s: failed to force dfp power\n",
				__func__);


	/* change mode */
	rc = wusb3801_set_mode(chip, chip->pdata->init_mode);
	if (rc<0)
		dev_err(cdev, "%s: failed to set mode\n",
				__func__);

        rc = wusb3801_set_chip_state(chip,
				WUSB3801_STATE_ERROR_RECOVERY);

	if (rc<0)
		dev_err(cdev, "%s: Reset state failed.\n",
				__func__);

	return rc;
}

static int wusb3801_reset_device(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;
/*
	rc = i2c_smbus_write_byte_data(chip->client,
			WUSB3801_REG_CONTROL1, BIT_REG_CTRL1_SM_RST);

	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "reset fails\n");
		return rc;
	}
	*/

	rc = wusb3801_update_status(chip);
	if (rc<0)
		dev_err(cdev, "fail to read status\n");

	rc = wusb3801_init_reg(chip);
	if (rc<0)
		dev_err(cdev, "fail to init reg\n");

	wusb3801_detach(chip);

	/* clear global interrupt mask */
	rc = wusb3801_write_masked_byte(chip->client,
				WUSB3801_REG_CONTROL0,
				WUSB3801_INT_MASK,
				WUSB3801_INT_ENABLE);
	if (rc<0) {
		dev_err(cdev, "%s: fail to init\n", __func__);
		return rc;
	}

	dev_info(cdev, "mode[0x%02x], host_cur[0x%02x], dttime[0x%02x]\n",
			chip->mode, chip->dfp_power, chip->dttime);

	return rc;
}

/************************************************************************
 *
 *       wregdump_show
 *
 *  Description :
 *  -------------
 *  Dump registers to user space. there is side-effects for Read/Clear
 *  registers. For example interrupt status.
 *
 ************************************************************************/
static ssize_t wregdump_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int i, rc, ret = 0;

	mutex_lock(&chip->mlock);
	for (i = WUSB3801_REG_VERSION_ID ; i <= WUSB3801_REG_TEST_12; i++)
	{
		rc = i2c_smbus_read_byte_data(chip->client, (uint8_t)i);
		if (rc<0)
		{
			pr_err("cannot read 0x%02x\n", i);
			rc = 0;
		}
		ret += snprintf(buf + ret, 1024 - ret, "from 0x%02x read 0x%02x\n", (uint8_t)i, rc);
	}
	mutex_unlock(&chip->mlock);

	return ret;
}

DEVICE_ATTR(wregdump, S_IRUGO, wregdump_show, NULL);


#ifdef __TEST_CC_PATCH__
static int test_cc_patch(struct wusb3801_chip *chip)
{
	int rc;
	struct device *cdev = &chip->client->dev;
	dev_err(cdev, "%s \n",__func__);
	mutex_lock(&chip->mlock);

	i2c_smbus_write_byte_data(chip->client,
			WUSB3801_REG_TEST_02, 0x82);
	msleep(100);
	i2c_smbus_write_byte_data(chip->client,
			WUSB3801_REG_TEST_09, 0xC0);
	msleep(100);
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_TEST0);
	msleep(10);
	i2c_smbus_write_byte_data(chip->client,
			WUSB3801_REG_TEST_09, 0x00);
	msleep(10);
	i2c_smbus_write_byte_data(chip->client,
			WUSB3801_REG_TEST_02, 0x80);
	msleep(10);
	i2c_smbus_write_byte_data(chip->client,
			WUSB3801_REG_TEST_02, 0x00);
	mutex_unlock(&chip->mlock);
    return BITS_GET(rc, 0x40);
}
#endif /* __TEST_CC_PATCH__ */

#ifdef __TEST_CC_PATCH__
/************************************************************************
 *
 *       wcc_status_show
 *
 *  Description :
 *  -------------
 *  show cc_status
 *
 ************************************************************************/
static ssize_t wcc_status_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int rc, ret = 0;

	mutex_lock(&chip->mlock);
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_STATUS);
	mutex_unlock(&chip->mlock);


	if (rc<0)
	{
		pr_err("cannot read WUSB3801_REG_STATUS\n");
		rc = 0xFF;
		ret = snprintf(buf, PAGE_SIZE, "cc_sts (%d)\n", rc);
	}

	chip->cc_sts = test_cc_patch(chip);

	rc  =  BITS_GET(rc, WUSB3801_CC_STS_MASK);
	if(rc == 0 && chip->cc_sts != 0xFF)
		rc = chip->cc_sts;
	else
		rc -= 1;
	ret = snprintf(buf, PAGE_SIZE, "cc_sts (%d)\n", rc);
	return ret;
}

DEVICE_ATTR(wcc_status, S_IRUGO, wcc_status_show, NULL);
#endif /*  __TEST_CC_PATCH__	 */

/************************************************************************
 *
 *       wtype_show
 *
 *  Description :
 *  -------------
 *  Dump types of attached devices
 *
 ************************************************************************/
static ssize_t wtype_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	switch (chip->type) {
	case WUSB3801_TYPE_SNK:
		ret = snprintf(buf, PAGE_SIZE, "SINK(%d)\n", chip->type);
		break;
	case WUSB3801_TYPE_SRC:
		ret = snprintf(buf, PAGE_SIZE, "SOURCE(%d)\n", chip->type);
		break;
	case WUSB3801_TYPE_DBG_ACC:
		ret = snprintf(buf, PAGE_SIZE, "DEBUGACC(%d)\n", chip->type);
		break;
	case WUSB3801_TYPE_AUD_ACC:
		ret = snprintf(buf, PAGE_SIZE, "AUDIOACC(%d)\n", chip->type);
		break;
	default:
		ret = snprintf(buf, PAGE_SIZE, "NOTYPE(%d)\n", chip->type);
		break;
	}
	mutex_unlock(&chip->mlock);

	return ret;
}

DEVICE_ATTR(wtype, S_IRUGO , wtype_show, NULL);

/************************************************************************
 *
 *  wchip_state_show/wchip_state_store
 *
 *  Description :
 *  -------------
 *  Get/Set state to/from user spaces.
 *
 ************************************************************************/

static ssize_t wchip_state_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE,
			STRV(WUSB3801_STATE_DISABLED) " - WUSB3801_STATE_DISABLED\n"
			STRV(WUSB3801_STATE_ERROR_RECOVERY) " - WUSB3801_STATE_ERROR_RECOVERY\n"
			STRV(WUSB3801_STATE_UNATTACHED_SNK) " - WUSB3801_STATE_UNATTACHED_SNK\n"
			STRV(WUSB3801_STATE_UNATTACHED_SRC) " - WUSB3801_STATE_UNATTACHED_SRC\n");

}


static ssize_t wchip_state_store(struct device *dev,
				struct device_attribute *attr,
				const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int state = 0;
	int rc = 0;

	if (sscanf(buff, "%d", &state) == 1) {
		mutex_lock(&chip->mlock);
	  if(((state == WUSB3801_STATE_UNATTACHED_SNK) &&
	    ((chip->mode & WUSB3801_ROLE_MASK) == BIT_REG_CTRL0_RLE_SRC)) || \
	    ((state == WUSB3801_STATE_UNATTACHED_SRC) &&
	    ((chip->mode & WUSB3801_ROLE_MASK) == BIT_REG_CTRL0_RLE_SNK))){
			mutex_unlock(&chip->mlock);
			return -EINVAL;
		}


		rc = wusb3801_set_chip_state(chip, (uint8_t)state);
		if (rc<0) {
			mutex_unlock(&chip->mlock);
			return rc;
		}

		wusb3801_detach(chip);
		mutex_unlock(&chip->mlock);
		return size;
	}

	return -EINVAL;
}

DEVICE_ATTR(wchip_state, S_IRUGO | S_IWUSR, wchip_state_show, wchip_state_store);

/************************************************************************
 *
 *  wmode_show/wmode_store
 *
 *  Description :
 *  -------------
 *  Dump/Set role to/from user spaces.
 ************************************************************************/
static ssize_t wmode_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	switch (chip->mode) {
	case WUSB3801_DRP_ACC:
		ret = snprintf(buf, PAGE_SIZE, "DRP+ACC(%d)\n", chip->mode);
		break;
	case WUSB3801_DRP:
		ret = snprintf(buf, PAGE_SIZE, "DRP(%d)\n", chip->mode);
		break;
  case WUSB3801_SNK_ACC:
		ret = snprintf(buf, PAGE_SIZE, "SNK+ACC(%d)\n", chip->mode);
		break;
	case WUSB3801_SNK:
		ret = snprintf(buf, PAGE_SIZE, "SNK(%d)\n", chip->mode);
		break;
	case WUSB3801_SRC_ACC:
		ret = snprintf(buf, PAGE_SIZE, "SRC+ACC(%d)\n", chip->mode);
		break;
	case WUSB3801_SRC:
		ret = snprintf(buf, PAGE_SIZE, "SRC(%d)\n", chip->mode);
		break;
  case WUSB3801_DRP_PREFER_SRC_ACC:
  	ret = snprintf(buf, PAGE_SIZE, "DRP+ACC+PREFER_SRC(%d)\n", chip->mode);
    break;
  case WUSB3801_DRP_PREFER_SRC:
  	ret = snprintf(buf, PAGE_SIZE, "DRP+PREFER_SRC(%d)\n", chip->mode);
    break;
  case WUSB3801_DRP_PREFER_SNK_ACC:
  	ret = snprintf(buf, PAGE_SIZE, "DRP+ACC+PREFER_SNK(%d)\n", chip->mode);
    break;
  case WUSB3801_DRP_PREFER_SNK:
  	ret = snprintf(buf, PAGE_SIZE, "DRP+PREFER_SNK(%d)\n", chip->mode);
    break;
	default:
		ret = snprintf(buf, PAGE_SIZE, "UNKNOWN(%d)\n", chip->mode);
		break;
	}
	mutex_unlock(&chip->mlock);

	return ret;
}

static ssize_t wmode_store(struct device *dev,
				struct device_attribute *attr,
				const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int mode = 0;
	int rc = 0;

	if (sscanf(buff, "%d", &mode) == 1) {
		mutex_lock(&chip->mlock);

		/*
		 * since device trigger to usb happens independent
		 * from charger based on vbus, setting SRC modes
		 * doesn't prevent usb enumeration as device
		 * KNOWN LIMITATION
		 */
		rc = wusb3801_set_mode(chip, (uint8_t)mode);
		if (rc<0) {
			mutex_unlock(&chip->mlock);
			return rc;
		}

		rc = wusb3801_set_chip_state(chip,
					WUSB3801_STATE_ERROR_RECOVERY);
		if (rc<0) {
			mutex_unlock(&chip->mlock);
			return rc;
		}


		wusb3801_detach(chip);
		mutex_unlock(&chip->mlock);
		return size;
	}

	return -EINVAL;
}
DEVICE_ATTR(wmode, S_IRUGO | S_IWUSR, wmode_show, wmode_store);

/************************************************************************
 *
 *  wdttime_show/wdttime_store
 *
 *  Description :
 *  -------------
 *  Get/Set duty cycles period from/to user spaces.
 *  Noted that: Wusb3801 uses fixed duty cycles percentage.
 ************************************************************************/
static ssize_t wdttime_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	ret = snprintf(buf, PAGE_SIZE, "%u\n", chip->dttime);
	mutex_unlock(&chip->mlock);
	return ret;
}

static ssize_t wdttime_store(struct device *dev,
				struct device_attribute *attr,
				const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int dttime = 0;
	int rc = 0;

	if (sscanf(buff, "%d", &dttime) == 1) {
		mutex_lock(&chip->mlock);
		rc = wusb3801_set_toggle_time(chip, (uint8_t)dttime);
		mutex_unlock(&chip->mlock);
		if (rc<0)
			return rc;

		return size;
	}

	return -EINVAL;
}
DEVICE_ATTR(wdttime, S_IRUGO | S_IWUSR, wdttime_show, wdttime_store);

/************************************************************************
 *
 *  whostcur_show/whostcur_store
 *
 *  Description :
 *  -------------
 *  Get/Set host current from/to user spaces.
 ************************************************************************/
static ssize_t whostcur_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	ret = snprintf(buf, PAGE_SIZE, "%u\n", chip->dfp_power);
	mutex_unlock(&chip->mlock);
	return ret;
}

static ssize_t whostcur_store(struct device *dev,
				struct device_attribute *attr,
				const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int buf = 0;
	int rc = 0;

	if (sscanf(buff, "%d", &buf) == 1) {
		mutex_lock(&chip->mlock);
		rc = wusb3801_set_dfp_power(chip, (uint8_t)buf);
		mutex_unlock(&chip->mlock);
		if (rc<0)
			return rc;

		return size;
	}

	return -EINVAL;
}
DEVICE_ATTR(whostcur, S_IRUGO | S_IWUSR, whostcur_show, whostcur_store);

/************************************************************************
 *
 *  wclientcur_show
 *
 *  Description :
 *  -------------
 *  dumps client current to user spaces.
 ************************************************************************/
static ssize_t wclientcur_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	ret = snprintf(buf, PAGE_SIZE, "%d\n", chip->ufp_power);
	mutex_unlock(&chip->mlock);
	return ret;
}
DEVICE_ATTR(wclientcur, S_IRUGO, wclientcur_show, NULL);

/************************************************************************
 *
 *  wreset_store
 *
 *  Description :
 *  -------------
 *  Reset state machine of WUSB3801
 ************************************************************************/

static ssize_t wreset_store(struct device *dev,
				struct device_attribute *attr,
				const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	uint32_t reset = 0;
	int rc = 0;

	if (sscanf(buff, "%u", &reset) == 1) {
		mutex_lock(&chip->mlock);
		rc = wusb3801_reset_device(chip);
		mutex_unlock(&chip->mlock);
		if (rc<0)
			return rc;

		return size;
	}

	return -EINVAL;
}
DEVICE_ATTR(wreset, S_IWUSR, NULL, wreset_store);



static int wusb3801_create_devices(struct device *cdev)
{
	int ret = 0;

	ret = device_create_file(cdev, &dev_attr_wchip_state);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_wchip_state\n");
		ret = -ENODEV;
		goto err0;
	}

	ret = device_create_file(cdev, &dev_attr_wtype);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_wtype\n");
		ret = -ENODEV;
		goto err1;
	}

	ret = device_create_file(cdev, &dev_attr_wmode);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_wmode\n");
		ret = -ENODEV;
		goto err2;
	}

	ret = device_create_file(cdev, &dev_attr_wreset);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_wreset\n");
		ret = -ENODEV;
		goto err3;
	}

	ret = device_create_file(cdev, &dev_attr_wdttime);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_wdttime\n");
		ret = -ENODEV;
		goto err4;
	}

	ret = device_create_file(cdev, &dev_attr_whostcur);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_whostcur\n");
		ret = -ENODEV;
		goto err5;
	}

	ret = device_create_file(cdev, &dev_attr_wclientcur);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_wclientcur\n");
		ret = -ENODEV;
		goto err6;
	}

	ret = device_create_file(cdev, &dev_attr_wregdump);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_wregdump\n");
		ret = -ENODEV;
		goto err7;
	}
#ifdef __TEST_CC_PATCH__
	ret = device_create_file(cdev, &dev_attr_wcc_status);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_wcc_status\n");
		ret = -ENODEV;
		goto err8;
	}

	return ret;


err8:
	device_remove_file(cdev, &dev_attr_wregdump);
#endif /* __TEST_CC_PATCH__ */

err7:
	device_remove_file(cdev, &dev_attr_wclientcur);
err6:
	device_remove_file(cdev, &dev_attr_whostcur);
err5:
	device_remove_file(cdev, &dev_attr_wdttime);
err4:
	device_remove_file(cdev, &dev_attr_wreset);
err3:
	device_remove_file(cdev, &dev_attr_wmode);
err2:
	device_remove_file(cdev, &dev_attr_wtype);
err1:
	device_remove_file(cdev, &dev_attr_wchip_state);
err0:
	return ret;
}

static void wusb3801_destory_device(struct device *cdev)
{
	device_remove_file(cdev, &dev_attr_wchip_state);
	device_remove_file(cdev, &dev_attr_wtype);
	device_remove_file(cdev, &dev_attr_wmode);
	device_remove_file(cdev, &dev_attr_wreset);
	device_remove_file(cdev, &dev_attr_wdttime);
	device_remove_file(cdev, &dev_attr_whostcur);
	device_remove_file(cdev, &dev_attr_wclientcur);
	device_remove_file(cdev, &dev_attr_wregdump);
#ifdef __TEST_CC_PATCH__
	device_remove_file(cdev, &dev_attr_wcc_status);
#endif /* __TEST_CC_PATCH__ */
}

//POWER_SUPPLY_PROP_CURRENT_MAX

static int wusb3801_power_set_icurrent_max(struct wusb3801_chip *chip,
						int icurrent)
{
/*
* TODO: Set your PMIC function
*/

	//if (chip->usb_psy->desc->set_property)
//		return chip->usb_psy->desc->set_property(chip->usb_psy,
//				POWER_SUPPLY_PROP_INPUT_CURRENT_MAX, &ret);

  return 0;
}

static void wusb3801_bclvl_changed(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc, limit, hcurrent;
	uint8_t status, type;

	rc = i2c_smbus_read_byte_data(chip->client,
				WUSB3801_REG_STATUS);
	if (rc<0) {
		dev_err(cdev, "%s: failed to read\n", __func__);
		if(wusb3801_reset_device(chip)<0)
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}

	status = (rc & WUSB3801_TYPE_MASK) ? 1 : 0;
	type = status  ? rc & WUSB3801_TYPE_MASK : WUSB3801_TYPE_INVALID;
	hcurrent = rc & WUSB3801_BCLVL_MASK;
	dev_dbg(cdev, "%s: sts[0x%02x], type[0x%02x]\n",__func__, status, type);

	if (type == WUSB3801_TYPE_SNK) {
		/* do nothing */
	}
	else if(type == WUSB3801_TYPE_SRC){
#ifdef __DEBOUNCE_TIMER__
		if(chip->bc_lvl == WUSB3801_SNK_3000MA && hcurrent == WUSB3801_SNK_1500MA){
			if(chip->current_timer.timer_is_canceled == true){
				wusb3801_mod_timer(&chip->current_timer, DEBOUNCE_TIME_OUT);
				dev_err(cdev, "%s: [mod timer] start jiffies %lu\n", __func__, jiffies);
			}
			else{
				dev_err(cdev, "%s: [mod timer] continuing jiffies %lu\n", __func__, jiffies);	
			}
			return;
		}
		else{
			chip->bc_lvl = hcurrent;
		}
#endif /* __DEBOUNCE_TIMER__ */
		limit = (chip->bc_lvl == WUSB3801_SNK_3000MA ? 3000 :
			(chip->bc_lvl == WUSB3801_SNK_1500MA ? 1500 : 0));
		dev_dbg(cdev, "%s: limit = %d\n",__func__, limit);
		wusb3801_power_set_icurrent_max(chip, limit);

	}
	else{
		/* do nothing */
	}
}




#if 0
static void wusb3801_usb20_host_detected(struct wusb3801_chip *chip)
{
	//struct device *cdev = &chip->client->dev;
	/*
	* TODO: Add your code here if you want do something during
	*       USB2.0 host has been detected.
	*/
}
#endif

static void wusb3801_src_detected(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
   if((chip->mode & WUSB3801_ROLE_MASK) == BIT_REG_CTRL0_RLE_SRC){
		dev_err(cdev, "not support in source mode\n");
		if(wusb3801_reset_device(chip)<0)
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}
	wusb3801_update_state(chip, WUSB3801_STATE_ATTACHED_SNK);

	//if(wusb3801_is_vbus_on(chip))
	//{
	//	dev_err(cdev, "USB2.0 detected %s.\n", __func__);
	//	wusb3801_usb20_host_detected(chip);
	//}
	/**
	*  TODO: Add you code here if you want doing something special during
	*        TYPEC source has detected.
	*/

	chip->type = WUSB3801_TYPE_SRC;
}

static void wusb3801_snk_detected(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	dev_info(cdev, "%s \n",__func__);
	if((chip->mode & WUSB3801_ROLE_MASK) == BIT_REG_CTRL0_RLE_SNK){
		dev_err(cdev, "not support in sink mode\n");
		if(wusb3801_reset_device(chip)<0)
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}

	/**
	*  TODO: Add you code here if you want doing something special during
	*        TYPEC Sink has detected.
	*/
		/*
		 * mode == WUSB3801_SRC/WUSB3801_SRC_ACC
		 */
		wusb3801_set_dfp_power(chip, chip->pdata->dfp_power);
		wusb3801_update_state(chip, WUSB3801_STATE_ATTACHED_SRC);
		chip->type = WUSB3801_TYPE_SNK;
}

static void wusb3801_dbg_acc_detected(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	dev_info(cdev, "%s \n",__func__);
	if (chip->mode & BIT_REG_CTRL0_DIS_ACC) {
		dev_err(cdev, "not support accessory mode\n");
		if(wusb3801_reset_device(chip)<0)
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}

	/**
	*  TODO: Add you code here if you want doing something special during
	*        TYPEC Debug accessory has detected.
	*/
	wusb3801_update_state(chip, WUSB3801_STATE_DEBUG_ACCESSORY);
}

static void wusb3801_aud_acc_detected(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	dev_info(cdev, "%s \n",__func__);
	if (chip->mode & BIT_REG_CTRL0_DIS_ACC) {
		dev_err(cdev, "not support accessory mode\n");
		if(wusb3801_reset_device(chip)<0)
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}

	/**
	*  TODO: Add you code here if you want doing something special during
	*        TYPEC Audio accessory has detected.
	*/
	wusb3801_update_state(chip, WUSB3801_STATE_AUDIO_ACCESSORY);
}

static void wusb3801_aud_acc_detach(struct wusb3801_chip *chip)
{
		//struct device *cdev = &chip->client->dev;
/**
	*  TODO: Add you code here if you want doing something special during
	*        TYPEC Audio accessory pull out.
	*/
}
#ifdef __RESET_PATCH__
static int reset_patch(struct wusb3801_chip *chip)
{
	int rc;
	struct device *cdev = &chip->client->dev;
	//reg10 write 0x55
	rc = i2c_smbus_write_byte_data(chip->client,
			WUSB3801_REG_TEST_04, 0x55);
	if (rc<0) {
		dev_err(cdev, "write reg3 0x1 failed 0x%02x\n", rc);
		return rc;
	}
	msleep(25);
	//reg10 write 0x00
	rc = i2c_smbus_write_byte_data(chip->client,
			WUSB3801_REG_TEST_04, 0x00);
	if (rc<0) {
		dev_err(cdev, "write reg3 0x1 failed 0x%02x\n", rc);
		return rc;
	}
	return 0;
}
#endif /* __RESET_PATCH__ */
static void wusb3801_detach(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
#ifdef __RESET_PATCH__
	int ret;
#endif /* __RESET_PATCH__ */
	dev_err(cdev, "%s: type[0x%02x] chipstate[0x%02x]\n",
			__func__, chip->type, chip->state);

	switch (chip->state) {
	case WUSB3801_STATE_ATTACHED_SRC:
		wusb3801_init_force_dfp_power(chip);
		break;
	case WUSB3801_STATE_ATTACHED_SNK:
		wusb3801_power_set_icurrent_max(chip, 0);
		break;
	case WUSB3801_STATE_DEBUG_ACCESSORY:
	  break;
	case WUSB3801_STATE_AUDIO_ACCESSORY:
	  wusb3801_aud_acc_detach(chip);
		break;
	case WUSB3801_STATE_DISABLED:
	case WUSB3801_STATE_ERROR_RECOVERY:
		break;
	default:
		dev_err(cdev, "%s: Invaild chipstate[0x%02x]\n",
				__func__, chip->state);
		break;
	}
#ifdef __DEBOUNCE_TIMER__
	wusb3801_cancel_timer(&chip->current_timer);
#endif /* __DEBOUNCE_TIMER__ */
	chip->type = WUSB3801_TYPE_INVALID;
	chip->bc_lvl = WUSB3801_SNK_0MA;
	chip->ufp_power  = 0;
	chip->try_attcnt = 0;
	chip->attached   = 0;
#ifdef __RESET_PATCH__
	ret = reset_patch(chip);
	if(ret != 0)
		dev_err(cdev, "%s: reset patch failed\n", __func__);
#endif /* __RESET_PATCH__ */
	//Excute the defered init mode into drp+try.snk when detached
	if(chip->defer_init == 1) {
		dev_err(cdev, "%s: reset to init mode\n", __func__);
		if (wusb3801_set_mode(chip, chip->pdata->init_mode)<0)
			dev_err(cdev, "%s: failed to set init mode\n", __func__);
		chip->defer_init = 0;
	}
	wusb3801_update_state(chip, WUSB3801_STATE_ERROR_RECOVERY);

}


#if 0
static unsigned wusb3801_is_vbus_off(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc;

	rc = i2c_smbus_read_byte_data(chip->client,
				WUSB3801_REG_STATUS);
	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "%s: failed to read status\n", __func__);
		return false;
	}

	return !((rc & WUSB3801_TYPE_MASK) && (rc & WUSB3801_VBUS_OK));
}
#endif /*if 0*/
#if 0
static unsigned wusb3801_is_vbus_on(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc;

	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_STATUS);
	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "%s: failed to read status\n", __func__);
		return false;
	}

	return !!(rc & WUSB3801_VBUS_OK);
}
#endif

static void wusb3801_attach(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;
	uint8_t status, type;
	/* get status */
	rc = i2c_smbus_read_byte_data(chip->client,
			WUSB3801_REG_STATUS);
	if (rc<0) {
		dev_err(cdev, "%s: failed to read status\n", __func__);
		return;
	}
#ifdef __TEST_CC_PATCH__
	if(chip->dev_sub_id != 0xA0){
		if(chip->cc_test_flag == 0 &&  BITS_GET(rc, WUSB3801_CC_STS_MASK) == 0 ){
			//chip->reg4_val = rc;
			chip->cc_sts = test_cc_patch(chip);
			chip->cc_test_flag = 1;
			dev_err(cdev, "%s: cc_sts[0x%02x]\n", __func__, chip->cc_sts);
			return;
		}
		if(chip->cc_test_flag == 1){
			chip->cc_test_flag = 0;
			if(chip->cc_sts == WUSB3801_CC2_CONNECTED)
				rc = rc | 0x02;
			else if(chip->cc_sts == WUSB3801_CC1_CONNECTED)
				rc = rc | 0x01;
			dev_err(cdev, "%s: cc_test_patch rc[0x%02x]\n", __func__, rc);
		}
	}
#endif	/* __TEST_CC_PATCH__ */
	status = (rc & WUSB3801_ATTACH) ? true : false;
	type = status ? \
			rc & WUSB3801_TYPE_MASK : WUSB3801_TYPE_INVALID;

	if (chip->state != WUSB3801_STATE_ERROR_RECOVERY) {
		rc = wusb3801_set_chip_state(chip,
				WUSB3801_STATE_ERROR_RECOVERY);
		if (rc<0)
			dev_err(cdev, "%s: failed to set error recovery\n",
					__func__);
		wusb3801_detach(chip);
		dev_err(cdev, "%s: Invaild chipstate[0x%02x]\n",
				__func__, chip->state);
		return;
	}
	chip->bc_lvl = rc & WUSB3801_BCLVL_MASK;
	if(chip->bc_lvl == 0){
		chip->bc_lvl = WUSB3801_SNK_DEFAULT;
		dev_err(cdev, "%s: chip current is 0, set default\n",
				__func__);
	}
	switch (type) {
	case WUSB3801_TYPE_SRC:
		wusb3801_src_detected(chip);
		break;
	case WUSB3801_TYPE_SNK:
		wusb3801_snk_detected(chip);
		break;
	case WUSB3801_TYPE_DBG_ACC:
		wusb3801_dbg_acc_detected(chip);
		chip->type = type;
		break;
	case WUSB3801_TYPE_AUD_ACC:
		wusb3801_aud_acc_detected(chip);
		chip->type = type;
		break;
	case WUSB3801_TYPE_INVALID:
		wusb3801_detach(chip);
		dev_err(cdev, "%s: Invaild type[0x%02x]\n", __func__, type);
		break;
	default:
		rc = wusb3801_set_chip_state(chip,
				WUSB3801_STATE_ERROR_RECOVERY);
		if (rc<0)
			dev_err(cdev, "%s: failed to set error recovery\n",
					__func__);

		wusb3801_detach(chip);
		dev_err(cdev, "%s: Unknwon type[0x%02x]\n", __func__, type);
		break;
	}
  chip->attached = true;
}
static void wusb3801_work_handler(struct work_struct *work)
{
	struct wusb3801_chip *chip =
			container_of(work, struct wusb3801_chip, dwork);
	struct device *cdev = &chip->client->dev;
	int rc;
	uint8_t int_sts;
	mutex_lock(&chip->mlock);
	/* get interrupt */
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_INTERRUPT);
	mutex_unlock(&chip->mlock);
	if (rc<0) {
		dev_err(cdev, "%s: failed to read interrupt\n", __func__);
		goto work_unlock;
	}
	int_sts = rc & WUSB3801_INT_STS_MASK;

	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_STATUS);
	if (rc<0) {
		dev_err(cdev, "%s: failed to read reg status\n", __func__);
		goto work_unlock;
	}
	printk("%s WUSB3801_REG_STATUS : 0x%02x\n", __func__, rc);

	printk("%s: int_sts[0x%02x]\n", __func__, int_sts);
	if (int_sts & WUSB3801_INT_DETACH){
		wusb3801_detach(chip);
	}
	if (int_sts & WUSB3801_INT_ATTACH){
		if(chip->attached){
			wusb3801_bclvl_changed(chip);
		}
		else{
			wusb3801_attach(chip);
		}
	}
work_unlock:
	enable_irq(chip->irq_gpio);
}

static irqreturn_t wusb3801_interrupt(int irq, void *data)
{
	struct wusb3801_chip *chip = (struct wusb3801_chip *)data;
	if (!chip) {
		dev_err(&chip->client->dev, "%s : called before init.\n", __func__);
		return IRQ_HANDLED;
	}
	disable_irq_nosync(irq);

	/*
	 * wake_lock_timeout, prevents multiple suspend entries
	 * before charger gets chance to trigger usb core for device
	 */
#ifdef __WITH_KERNEL_VER4__
	__pm_wakeup_event(&chip->wusb3801_ws.source, WUSB3801_WAKE_LOCK_TIMEOUT);
#else /* __WITH_KERNEL_VER4__ */
	wake_lock_timeout(&chip->wlock,
				msecs_to_jiffies(WUSB3801_WAKE_LOCK_TIMEOUT));
#endif /* __WITH_KERNEL_VER4__ */
	if (!queue_work(chip->cc_wq, &chip->dwork))
	{
		dev_err(&chip->client->dev, "%s: can't alloc work\n", __func__);
		enable_irq(irq);
	}

	return IRQ_HANDLED;
}



static int wusb3801_init_gpio(struct wusb3801_chip *chip)
{

	int rc = 0;

	return rc;
}

static void wusb3801_free_gpio(struct wusb3801_chip *chip)
{
	if (gpio_is_valid(chip->pdata->int_gpio))
		gpio_free(chip->pdata->int_gpio);
}

static int wusb3801_parse_dt(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	struct device_node *dev_node = cdev->of_node;
	struct wusb3801_data *data = chip->pdata;
	u32 val = 0;
	int rc = 0;
	dev_err(cdev, "dev_node->name=%s.\n",dev_node->name);
	data->int_gpio = of_get_named_gpio_flags(dev_node,
				"wusb3801,irq-gpio", 0,NULL);
	dev_err(cdev, "data->int_gpio=%d",data->int_gpio);
	if (data->int_gpio < 0) {
		dev_err(cdev, "int_gpio is not available\n");
		rc = data->int_gpio;
		goto out;
	}

	rc = of_property_read_u32(dev_node,
				"wusb3801,init-mode", &val);
	data->init_mode = (u8)val;
	dev_err(cdev, "data->init_mode=%d.\n",data->init_mode);
	if (rc || wusb3801_check_modes(data->init_mode)) {
		dev_err(cdev, "init mode is not available and set default\n");
		data->init_mode = WUSB3801_INIT_MODE; //WUSB3801_DRP_ACC;
		rc = 0;
	}

	rc = of_property_read_u32(dev_node,
				"wusb3801,host-current", &val);
	data->dfp_power = (u8)val;
	dev_err(cdev, "data->dfp_power=%d.\n",data->dfp_power);
	if (rc || (data->dfp_power > WUSB3801_HOST_3000MA)) {
		dev_err(cdev, "host current is not available and set default\n");
		data->dfp_power = WUSB3801_HOST_DEFAULT;
		rc = 0;
	}

	rc = of_property_read_u32(dev_node,
				"wusb3801,drp-toggle-time", &val);
	data->dttime = (u8)val;
	if (rc || (data->dttime != WUSB3801_TGL_40MS)) {
		dev_err(cdev, "Fixed drp time and set default (40ms:40ms)\n");
		data->dttime = WUSB3801_TGL_40MS;
		rc = 0;
	}

	dev_dbg(cdev, "init_mode:%d dfp_power:%d toggle_time:%d\n",
			data->init_mode, data->dfp_power, data->dttime);

out:
	return rc;
}



static int wusb3801_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct wusb3801_chip *chip;
	struct device *cdev = &client->dev;


#ifdef CONFIG_OF
	struct device_node *node;
	unsigned int ints[2] = { 0, 0 };
	unsigned int debounce, gpiopin;
#endif /* CONFIG_OF  */
	int ret = 0;
	struct wusb3801_data *data;


	dev_info(cdev,"wusb3801_probe start \n");

	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_BYTE_DATA |
				I2C_FUNC_SMBUS_WORD_DATA)) {
		return -EIO;
	}

	chip = devm_kzalloc(cdev, sizeof(struct wusb3801_chip), GFP_KERNEL);
	if (!chip) {
		return -ENOMEM;
	}

	chip->client = client;
	i2c_set_clientdata(client, chip);

	ret = wusb3801_read_device_id(chip);
	if (ret){
		dev_err(cdev, "wusb3801 doesn't find, try again\n");
		ret = wusb3801_read_device_id(chip);
		if(ret){
			dev_err(cdev, "wusb3801 doesn't find, stop find\n");
			goto err1;
		}
	}

		 data = devm_kzalloc(cdev,
				sizeof(struct wusb3801_data), GFP_KERNEL);

		if (!data) {
			dev_err(cdev, "can't alloc wusb3801_data\n");
			ret = -ENOMEM;
			goto err1;
		}

		chip->pdata = data;

		ret = wusb3801_parse_dt(chip);
		if (ret) {
			dev_err(cdev, "can't parse dt\n");
			goto err2;
		}


	ret = wusb3801_init_gpio(chip);
	if (ret) {
		dev_err(cdev, "fail to init gpio\n");
		goto err2;
	}

	chip->type      = WUSB3801_TYPE_INVALID;
	chip->state     = WUSB3801_STATE_ERROR_RECOVERY;
	chip->attached  = 0;
	chip->bc_lvl    = WUSB3801_SNK_0MA;
	chip->ufp_power = 0;
	chip->defer_init = 0;	// Do the mode init when chip init
	//chip->usb_psy   = usb_psy;
#ifdef __TEST_CC_PATCH__
	chip->cc_sts = 0xFF;
	chip->cc_test_flag = 0;
#endif /* __TEST_CC_PATCH__ */

	chip->cc_wq = alloc_ordered_workqueue("wusb3801-wq", WQ_HIGHPRI);
	if (!chip->cc_wq) {
		dev_err(cdev, "unable to create workqueue wusb3801-wq\n");
		goto err2;
	}
	INIT_WORK(&chip->dwork, wusb3801_work_handler);
#ifdef __WITH_KERNEL_VER4__
	wusb3801_wakeup_src_init(chip);
#else /* __WITH_KERNEL_VER4__ */
	wake_lock_init(&chip->wlock, WAKE_LOCK_SUSPEND, "wusb3801_wake");
#endif /* __WITH_KERNEL_VER4__ */
	mutex_init(&chip->mlock);

	ret = wusb3801_create_devices(cdev);
	if (ret<0) {
		dev_err(cdev, "could not create devices\n");
		goto err3;
	}

#ifdef __DEBOUNCE_TIMER__
	chip->chip_remove = 0;
	wusb3801_initialize_timer(&chip->current_timer, NULL, chip);
	chip->thread.chip = chip;
	wusb3801_create_thread(wusb3801_read_thread,
	                   &chip->thread, "wusb3801_i2c_thread");

#endif /* __DEBOUNCE_TIMER__ */


	node = of_find_compatible_node(NULL, NULL, "mediatek, EINT_TYPEC-eint");
	if (node) {
		of_property_read_u32_array(node, "debounce", ints, ARRAY_SIZE(ints));
		debounce = ints[1];
		gpiopin = ints[0];
		gpio_set_debounce(gpiopin, debounce);
	}
	else {
		dev_err(&client->dev, "request_irq node fail, node  !!\n" );
		ret = -ENXIO;
		goto err4;
	}
	chip->irq_gpio= irq_of_parse_and_map(node, 0);

	dev_info(&client->dev, "request_irq gpiopin=%d, irqnum=%d\n", gpiopin,chip->irq_gpio);

	ret =
	    request_irq(chip->irq_gpio, wusb3801_interrupt, IRQF_TRIGGER_FALLING, "wusb3801_int_irq", chip);
	if (ret != 0) {
		dev_err(cdev, "request_irq fail, ret %d, irqnum %d!!!\n", ret,
			    chip->irq_gpio);
		ret = -ENXIO;
		goto err4;
	}

	//dual_role_service.drv_data = client;
	//chip->dual_role = &dual_role_service;
	 ret = wusb3801_reset_device(chip);
	 if (ret) {
		dev_err(cdev, "failed to initialize\n");
		goto err4;
	}

	enable_irq_wake(chip->irq_gpio);
	//huanglei add for debug init reg status begin
	ret = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_STATUS);
	if (ret<0) {
		dev_err(cdev, "%s: failed to read reg status\n", __func__);
	}
	dev_err(cdev, "%s WUSB3801_REG_STATUS : 0x%02x\n", __func__, ret);
	//huanglei add for debug init reg status end

#ifdef __TEST_CC_PATCH__
	if(BITS_GET(ret, 0x80) == 1 && BITS_GET(ret, 0x60) != 0){
		dev_info(cdev, "wusb3801_probe end with no sm rst\n");
		return 0;
	}
#endif	/* __TEST_CC_PATCH__ */

	i2c_smbus_write_byte_data(chip->client,
			WUSB3801_REG_CONTROL1, BIT_REG_CTRL1_SM_RST);
#ifdef __DEBOUNCE_TIMER__
	while(chip->thread.pid == 0)
		schedule();
#endif /*__DEBOUNCE_TIMER__  */
	dev_info(cdev,"wusb3801_probe end \n");
	return 0;



#ifdef __DEBOUNCE_TIMER__
	wusb3801_cancel_timer(&chip->current_timer);
#endif /* __DEBOUNCE_TIMER__ */
err4:
	wusb3801_destory_device(cdev);
err3:
	destroy_workqueue(chip->cc_wq);
	mutex_destroy(&chip->mlock);
#ifdef __WITH_KERNEL_VER4__
	wakeup_source_trash(&chip->wusb3801_ws.source);
#else /* __WITH_KERNEL_VER4__ */
	wake_lock_destroy(&chip->wlock);
#endif /* __WITH_KERNEL_VER4__ */
	wusb3801_free_gpio(chip);
err2:
	devm_kfree(cdev, chip->pdata);
err1:
	i2c_set_clientdata(client, NULL);
	devm_kfree(cdev, chip);

	return ret;
}

static int wusb3801_remove(struct i2c_client *client)
{
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	struct device *cdev = &client->dev;

	if (!chip) {
		dev_err(cdev, "%s : chip is null\n", __func__);
		return -ENODEV;
	}

	if (chip->irq_gpio > 0)
		devm_free_irq(cdev, chip->irq_gpio, chip);


#ifdef __DEBOUNCE_TIMER__
	wusb3801_cancel_timer(&chip->current_timer);
	chip->chip_remove = 1;
	while(chip->thread.pid){
		complete(&chip->thread.comp);
		wusb3801_sched_timeout(2);
	}
#endif /*__DEBOUNCE_TIMER__  */
	wusb3801_destory_device(cdev);
	destroy_workqueue(chip->cc_wq);
	mutex_destroy(&chip->mlock);
#ifdef __WITH_KERNEL_VER4__
	wakeup_source_trash(&chip->wusb3801_ws.source);
#else /* __WITH_KERNEL_VER4__ */
	wake_lock_destroy(&chip->wlock);
#endif /* __WITH_KERNEL_VER4__ */
	wusb3801_free_gpio(chip);

	devm_kfree(cdev, chip->pdata);

	i2c_set_clientdata(client, NULL);
	devm_kfree(cdev, chip);

	return 0;
}

static void wusb3801_shutdown(struct i2c_client *client)
{
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	struct device *cdev = &client->dev;

	if ((wusb3801_set_mode(chip, WUSB3801_SNK)<0) ||
			(wusb3801_set_chip_state(chip,
					WUSB3801_STATE_ERROR_RECOVERY)<0))
		dev_err(cdev, "%s: failed to set sink mode\n", __func__);
}

#ifdef CONFIG_PM
static int wusb3801_suspend(struct device *dev)
{
	return 0;
}

static int wusb3801_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops wusb3801_dev_pm_ops = {
	.suspend = wusb3801_suspend,
	.resume  = wusb3801_resume,
};
#endif

static const struct i2c_device_id wusb3801_id_table[] = {
	{"wusb3801", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, wusb3801_id_table);



static struct of_device_id wusb3801_match_table[] = {
	{ .compatible = "mediatek,wusb3801x",},
	{ },
};


static const unsigned short normal_i2c[] = {WUSB3801_SLAVE_ADDR0, WUSB3801_SLAVE_ADDR1, I2C_CLIENT_END};
static struct i2c_driver wusb3801_i2c_driver = {
	.driver = {
		.name = "wusb3801",
		.owner = THIS_MODULE,
		.of_match_table = wusb3801_match_table,
#ifdef CONFIG_PM
		.pm = &wusb3801_dev_pm_ops,
#endif
	},
	.probe = wusb3801_probe,
	.remove = wusb3801_remove,
	.shutdown = wusb3801_shutdown,
	.id_table = wusb3801_id_table,
	.address_list = (const unsigned short *) normal_i2c,
};

static __init int wusb3801_i2c_init(void)
{
	return i2c_add_driver(&wusb3801_i2c_driver);
}

static __exit void wusb3801_i2c_exit(void)
{
	i2c_del_driver(&wusb3801_i2c_driver);
}

module_init(wusb3801_i2c_init);
module_exit(wusb3801_i2c_exit);

MODULE_AUTHOR("lhuang@sh-willsemi.com");
MODULE_DESCRIPTION("WUSB3801x USB Type-C driver for MSM and Qcom/Mediatek/Others Linux/Android Platform");
MODULE_LICENSE("GPL v2");
