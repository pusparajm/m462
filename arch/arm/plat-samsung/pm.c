/* linux/arch/arm/plat-s3c/pm.c
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2004-2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C common power management (suspend to ram) support.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/serial_core.h>
#include <linux/io.h>

#include <asm/cacheflush.h>
#include <asm/suspend.h>
#include <mach/hardware.h>
#include <mach/map.h>

#include <plat/regs-serial.h>
#include <mach/regs-clock.h>
#include <mach/regs-irq.h>
#include <mach/irqs.h>
#include <asm/irq.h>

#include <plat/pm.h>
#include <mach/pm-core.h>

/* for external use */

unsigned long s3c_pm_flags;

/* Debug code:
 *
 * This code supports debug output to the low level UARTs for use on
 * resume before the console layer is available.
*/

#ifdef CONFIG_SAMSUNG_PM_DEBUG
extern void printascii(const char *);

void s3c_pm_dbg(const char *fmt, ...)
{
	va_list va;
	char buff[256];

	va_start(va, fmt);
	vsnprintf(buff, sizeof(buff), fmt, va);
	va_end(va);

	printascii(buff);
}

static inline void s3c_pm_debug_init(void)
{
	/* restart uart clocks so we can use them to output */
	s3c_pm_debug_init_uart();
}

#else
#define s3c_pm_debug_init() do { } while(0)

#endif /* CONFIG_SAMSUNG_PM_DEBUG */

/* Save the UART configurations if we are configured for debug. */

unsigned char pm_uart_udivslot;

#ifdef CONFIG_SAMSUNG_PM_DEBUG

static struct pm_uart_save uart_save[CONFIG_SERIAL_SAMSUNG_UARTS];

static void s3c_pm_save_uart(unsigned int uart, struct pm_uart_save *save)
{
	void __iomem *regs = S3C_VA_UARTx(uart);

	save->ulcon = __raw_readl(regs + S3C2410_ULCON);
	save->ucon = __raw_readl(regs + S3C2410_UCON);
	save->ufcon = __raw_readl(regs + S3C2410_UFCON);
	save->umcon = __raw_readl(regs + S3C2410_UMCON);
	save->ubrdiv = __raw_readl(regs + S3C2410_UBRDIV);

	if (pm_uart_udivslot)
		save->udivslot = __raw_readl(regs + S3C2443_DIVSLOT);

	S3C_PMDBG("UART[%d]: ULCON=%04x, UCON=%04x, UFCON=%04x, UBRDIV=%04x\n",
		  uart, save->ulcon, save->ucon, save->ufcon, save->ubrdiv);
}

static void s3c_pm_save_uarts(void)
{
	struct pm_uart_save *save = uart_save;
	unsigned int uart;

	for (uart = 0; uart < CONFIG_SERIAL_SAMSUNG_UARTS; uart++, save++)
		s3c_pm_save_uart(uart, save);
}

static void s3c_pm_restore_uart(unsigned int uart, struct pm_uart_save *save)
{
	void __iomem *regs = S3C_VA_UARTx(uart);

	s3c_pm_arch_update_uart(regs, save);

	__raw_writel(save->ulcon, regs + S3C2410_ULCON);
	__raw_writel(save->ucon,  regs + S3C2410_UCON);
	__raw_writel(save->ufcon, regs + S3C2410_UFCON);
	__raw_writel(save->umcon, regs + S3C2410_UMCON);
	__raw_writel(save->ubrdiv, regs + S3C2410_UBRDIV);

	if (pm_uart_udivslot)
		__raw_writel(save->udivslot, regs + S3C2443_DIVSLOT);
}

static void s3c_pm_restore_uarts(void)
{
	struct pm_uart_save *save = uart_save;
	unsigned int uart;

	for (uart = 0; uart < CONFIG_SERIAL_SAMSUNG_UARTS; uart++, save++)
		s3c_pm_restore_uart(uart, save);
}
#else
static void s3c_pm_save_uarts(void) { }
static void s3c_pm_restore_uarts(void) { }
#endif

/* The IRQ ext-int code goes here, it is too small to currently bother
 * with its own file. */

unsigned long s3c_irqwake_intmask	= 0xffffffffL;
unsigned long s3c_irqwake_eintmask	= 0xffffffffL;

int s3c_irqext_wake(struct irq_data *data, unsigned int state)
{
	unsigned long bit = 1L << IRQ_EINT_BIT(data->irq);

	if (!(s3c_irqwake_eintallow & bit))
		return -ENOENT;

	printk(KERN_INFO "wake %s for irq %d\n",
	       state ? "enabled" : "disabled", data->irq);

	if (!state)
		s3c_irqwake_eintmask |= bit;
	else
		s3c_irqwake_eintmask &= ~bit;

	return 0;
}

/* helper functions to save and restore register state */

/**
 * s3c_pm_do_save() - save a set of registers for restoration on resume.
 * @ptr: Pointer to an array of registers.
 * @count: Size of the ptr array.
 *
 * Run through the list of registers given, saving their contents in the
 * array for later restoration when we wakeup.
 */
void s3c_pm_do_save(struct sleep_save *ptr, int count)
{
	for (; count > 0; count--, ptr++) {
		ptr->val = __raw_readl(ptr->reg);
		pr_debug("saved %p value %08lx\n", ptr->reg, ptr->val);
	}
}

/**
 * s3c_pm_do_restore() - restore register values from the save list.
 * @ptr: Pointer to an array of registers.
 * @count: Size of the ptr array.
 *
 * Restore the register values saved from s3c_pm_do_save().
 *
 * Note, we do not use S3C_PMDBG() in here, as the system may not have
 * restore the UARTs state yet
*/

void s3c_pm_do_restore(struct sleep_save *ptr, int count)
{
	for (; count > 0; count--, ptr++) {
		printk(KERN_DEBUG "restore %p (restore %08lx, was %08x)\n",
		       ptr->reg, ptr->val, __raw_readl(ptr->reg));

		__raw_writel(ptr->val, ptr->reg);
	}
}

/**
 * s3c_pm_do_restore_core() - early restore register values from save list.
 *
 * This is similar to s3c_pm_do_restore() except we try and minimise the
 * side effects of the function in case registers that hardware might need
 * to work has been restored.
 *
 * WARNING: Do not put any debug in here that may effect memory or use
 * peripherals, as things may be changing!
*/

void s3c_pm_do_restore_core(struct sleep_save *ptr, int count)
{
	for (; count > 0; count--, ptr++)
		__raw_writel(ptr->val, ptr->reg);
}

/* s3c2410_pm_show_resume_irqs
 *
 * print any IRQs asserted at resume time (ie, we woke from)
*/
static void __maybe_unused s3c_pm_show_resume_irqs(int start,
						   unsigned long which,
						   unsigned long mask)
{
	int i;

	which &= ~mask;

	for (i = 0; i <= 31; i++) {
		if (which & (1L<<i)) {
			S3C_PMDBG("IRQ %d asserted at resume\n", start+i);
		}
	}
}

void (*pm_logic_prep)(void);
void (*pm_logic_finish)(void);
void (*pm_cpu_prep)(void);
int (*pm_cpu_sleep)(unsigned long);

#define any_allowed(mask, allow) (((mask) & (allow)) != (allow))
#define CONFIG_RTC_TICK_SLEEP_AGING_TEST
#ifdef CONFIG_RTC_TICK_SLEEP_AGING_TEST
#define TICK_RESOLUTION 0.03
#define WAKEUP_TICK_START (unsigned int)3333.33 //100ms/TICK_RESOLUTION
#define WAKEUP_TICK_END (unsigned int)6666.66 //1030.92 //200ms/TICK_RESOLUTION
#define WAKEUP_TICK_INCREASEMENT (unsigned int)333.33   //10ms/TICK_RESOLUTION
#define WAKEUP_MASK_RTC_TICK 0x2
static int rtc_tick_wakeup_enable;

static void s3c_rtc_tick_control(int onoff)
{
	static void __iomem *base_tick_base = 0;
	static unsigned int tick_position = WAKEUP_TICK_START;
	static unsigned int backup_rtc_val = 0;
	static unsigned int backup_wakeup_mask_val = 0;

	unsigned int tmp;

	if(base_tick_base == 0)
		base_tick_base = ioremap(0x10590000, SZ_16K);

	if(onoff == 1) {
		tmp = __raw_readl(EXYNOS5430_WAKEUP_MASK);
		backup_wakeup_mask_val = tmp;
		__raw_writel(tmp & ~(0x1 << WAKEUP_MASK_RTC_TICK), EXYNOS5430_WAKEUP_MASK);

		tick_position = tick_position +	(unsigned int)WAKEUP_TICK_INCREASEMENT;
		if(tick_position > (unsigned int)WAKEUP_TICK_END)
			tick_position = WAKEUP_TICK_START;

		flush_cache_all();

		// RTCCON, TICKSEL[7:4], CLKRST[3], CNTSEL[2], CLKSEL[0], CTLEN[0]
		tmp = __raw_readl(base_tick_base + 0x40);
		backup_rtc_val = tmp;
		__raw_writel((tmp & ~(0x1F0)), base_tick_base + 0x40); // TICEN disable, TICCKSEL 32768Hz //1024Hz

		// TICNT, TICK_TIME_COUNT[31:0]
		__raw_writel(tick_position, base_tick_base + 0x44);

		// RTCCON, TICEN[8], TICEN enable
		tmp = __raw_readl(base_tick_base + 0x40);
		__raw_writel(tmp | 0x100, base_tick_base + 0x40); // TICEN enable
	}
	else {
		// TICEN disable, TICCKSEL 1024Hz
		__raw_writel(backup_rtc_val, base_tick_base + 0x40);
		// INTP, Timer TIC
		__raw_writel(0x01, base_tick_base + 0x30);
		__raw_writel(backup_wakeup_mask_val, EXYNOS5430_WAKEUP_MASK);
	}
}

static ssize_t s3c_rtc_tick_test_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
        return sprintf(buf, "%d\n", rtc_tick_wakeup_enable);
}

static ssize_t s3c_rtc_tick_test_store(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t n)
{
        int val;

        if (sscanf(buf, "%d", &val) == 1) {
                rtc_tick_wakeup_enable = !!val;
                return n;
        }

        return -EINVAL;
}
static DEVICE_ATTR(s3c_tick_wakeup_enable, 0660,
			s3c_rtc_tick_test_show, s3c_rtc_tick_test_store);
#endif

/* s3c_pm_enter
 *
 * central control for sleep/resume process
*/

static int s3c_pm_enter(suspend_state_t state)
{
	int ret;
	/* ensure the debug is initialised (if enabled) */

	s3c_pm_debug_init();

	printk("%s(%d)\n", __func__, state);

	if (pm_cpu_prep == NULL || pm_cpu_sleep == NULL) {
		printk(KERN_ERR "%s: error: no cpu sleep function\n", __func__);
		return -EINVAL;
	}

	/* check if we have anything to wake-up with... bad things seem
	 * to happen if you suspend with no wakeup (system will often
	 * require a full power-cycle)
	*/

	if (!of_have_populated_dt() &&
	    !any_allowed(s3c_irqwake_intmask, s3c_irqwake_intallow) &&
	    !any_allowed(s3c_irqwake_eintmask, s3c_irqwake_eintallow)) {
		printk(KERN_ERR "%s: No wake-up sources!\n", __func__);
		printk(KERN_ERR "%s: Aborting sleep\n", __func__);
		return -EINVAL;
	}

	/* save all necessary core registers not covered by the drivers */

	if (!of_have_populated_dt()) {
		samsung_pm_save_gpios();
		samsung_pm_saved_gpios();
	}

	s3c_pm_save_uarts();
	s3c_pm_save_core();

	/* set the irq configuration for wake */

	s3c_pm_configure_extint();
	s3c_pm_arch_prepare_irqs();

	/* call cpu specific preparation */

	pm_cpu_prep();

	s3c_pm_check_store();

	/* send the cpu to sleep... */

	s3c_pm_arch_stop_clocks();

#ifdef CONFIG_RTC_TICK_SLEEP_AGING_TEST
	if (rtc_tick_wakeup_enable)
		s3c_rtc_tick_control(1);
#endif

	/* this will also act as our return point from when
	 * we resume as it saves its own register state and restores it
	 * during the resume.  */

	ret = cpu_suspend(0, pm_cpu_sleep);

#ifdef CONFIG_RTC_TICK_SLEEP_AGING_TEST
	if (rtc_tick_wakeup_enable)
		s3c_rtc_tick_control(0);
#endif

	if (ret)
		return ret;

	/* restore the system state */

	s3c_pm_restore_core();
	s3c_pm_restore_uarts();

	if (!of_have_populated_dt()) {
		samsung_pm_restore_gpios();
		s3c_pm_restored_gpios();
	}

	s3c_pm_debug_init();

	/* check what irq (if any) restored the system */

	s3c_pm_arch_show_resume_irqs();

	S3C_PMDBG("%s: post sleep, preparing to return\n", __func__);

	/* LEDs should now be 1110 */
	s3c_pm_debug_smdkled(1 << 1, 0);

	s3c_pm_check_restore();

	/* ok, let's return from sleep */

	S3C_PMDBG("S3C PM Resume (post-restore)\n");
	return 0;
}

static int s3c_pm_prepare(void)
{
	/* prepare check area if configured */

	s3c_pm_check_prepare();
	if (pm_logic_prep)
		pm_logic_prep();
	return 0;
}

static void s3c_pm_finish(void)
{
	s3c_pm_check_cleanup();
	if (pm_logic_finish)
		pm_logic_finish();
}

static const struct platform_suspend_ops s3c_pm_ops = {
	.enter		= s3c_pm_enter,
	.prepare	= s3c_pm_prepare,
	.finish		= s3c_pm_finish,
	.valid		= suspend_valid_only_mem,
};

/* s3c_pm_init
 *
 * Attach the power management functions. This should be called
 * from the board specific initialisation if the board supports
 * it.
*/

int __init s3c_pm_init(void)
{
#ifdef CONFIG_RTC_TICK_SLEEP_AGING_TEST
	int err = -EINVAL;
#endif
	printk("S3C Power Management, Copyright 2004 Simtec Electronics\n");

	suspend_set_ops(&s3c_pm_ops);

#ifdef CONFIG_RTC_TICK_SLEEP_AGING_TEST
	if (!power_kobj)
                goto obj_err;

        err = sysfs_create_file(power_kobj, &dev_attr_s3c_tick_wakeup_enable.attr);

        if (err) {
                pr_err("%s: failed to create /sys/power/s3c_tick_wakeup_enable\n", __func__);
        }

obj_err:
        return err;
#else
	return 0;
#endif
}
