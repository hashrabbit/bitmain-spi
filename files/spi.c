#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <asm/system.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include "spi.h"

void *spi_vaddr, *gpio3_vaddr, *gpio2_vaddr;
McSPI spi_dev;
#if SPI_USE_INTERRUPT
static irqreturn_t mspi_interrupt(int irq, void *dev_id)
{
	McSPI *p_spi_dev = (McSPI *)dev_id;
	unsigned int intCode = 0;
	uint8_t data = 0;
	//iowrite32(0x01<<8, gpio2_vaddr + GPIO_SETDATAOUT);
	//printk(KERN_ERR "enter spi intterrupt\n");
	//printk(KERN_ERR "11OMAP2_MCSPI_IRQSTATUS{%#x}\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQSTATUS));
	//printk(KERN_ERR "11OMAP2_MCSPI_CHSTAT0{%#x}\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
	intCode = ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQSTATUS);
	//& ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE);
	//disable tx0_empty interrupt
	iowrite32(ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE) & (~ (EOW | TX0_EMPTY)), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE);
	//clear tx empty
	iowrite32(EOW | 0x0f, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQSTATUS);
	#if 0
	//no empty
	while( 00 == (ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0) & (0x01<<5)))
	{
		spi_dev.p_rx[spi_dev.rev_len++]= ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0);
		if(data++ >32)
		{
			printk(KERN_ERR "detect spi rx err\n");
			break;
		}
	}
	spi_dev.p_rx[spi_dev.rev_len++]= ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0);
	#endif
	//printk(KERN_ERR "OMAP2_MCSPI_CHSTAT0{%#x}spi_dev.rev_len{%d}\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0), spi_dev.rev_len);
	//wake_up_interruptible(&p_spi_dev->wait_transfer_complete);
	spi_dev.have_wake_up = true;
	wake_up_interruptible(&spi_dev.wait_transfer_complete);
	//iowrite32(0x01<<8, gpio2_vaddr + GPIO_CLEARDATAOUT);
	return IRQ_HANDLED;
}
#endif
void spi_init(void)
{
	void  *cm_per_vaddr, *ctl_md_vaddr;
	int32_t value32;
	int32_t i;
	int ret;
	//uint8_t value8 = 0x55;
	uint8_t rddata[0x110] = {0x55}, txdata[0x110] = {0x33};
	uint16_t rx_len;
	cm_per_vaddr = ioremap_nocache(CM_PER_BASE, CM_PER_Size);
	iowrite32(0x02 , cm_per_vaddr + CM_PER_SPI1_CLKCTRL); //Enable clk
	iowrite32(0x02 , cm_per_vaddr + CM_PER_GPIO3_CLKCTRL); //Enable clk
	iowrite32(0x02 , cm_per_vaddr + CM_PER_GPIO2_CLKCTRL); //Enable clk
	printk("CM_PER_SPI1_CLKCTRL %#x\n", ioread32(cm_per_vaddr + CM_PER_SPI1_CLKCTRL));
	while(ioread32(cm_per_vaddr + CM_PER_SPI1_CLKCTRL) & (0x03 << 16)) ;
	iounmap(cm_per_vaddr);
	ctl_md_vaddr = ioremap_nocache(CONTROL_MODULE_BASE, CONTROL_MODULE_Size);
	iowrite32(PAD_REV | PAD_PULLUP | 0x3, ctl_md_vaddr + conf_mcasp0_fsx); //D0 MISO
	iowrite32(PAD_REV | PAD_PULLUP | 0x3, ctl_md_vaddr + conf_mcasp0_axr0); //D1 MOSI
	iowrite32(PAD_PULLUP | 0x3, ctl_md_vaddr + conf_mcasp0_ahclkr); //cs0
	iowrite32(PAD_REV | PAD_PULLUP | 0x3, ctl_md_vaddr + conf_mcasp0_aclkx); //clk need input to feedback
	//iowrite32(PAD_PULLUP | 0x7, ctl_md_vaddr + conf_mcasp0_fsr); //GPIO3_19
	//iowrite32(PAD_PULLUP | 0x7, ctl_md_vaddr + conf_mcasp0_ahclkx); //GPIO3_21
	iowrite32(PAD_PULLUP | 0x7, ctl_md_vaddr + 0x8a0); //GPIO2_6  lcd data0
	iowrite32(PAD_PULLUP | 0x7, ctl_md_vaddr + 0x8a8); //GPIO2_8 lcd data2
	#if defined S2
	iowrite32(PAD_PULLUP | 0x7, ctl_md_vaddr + 0x8a4); //GPIO2_7  lcd data1
	#endif
	iounmap(ctl_md_vaddr);
	gpio2_vaddr = ioremap_nocache(GPIO2_BASE, GPIO2_SIZE);
	value32 = ioread32(gpio2_vaddr + GPIO_OE);
	iowrite32(value32 & (~(0x01 << 6)), gpio2_vaddr + GPIO_OE); //set output
	value32 = ioread32(gpio2_vaddr + GPIO_OE);
	iowrite32(value32 & (~(0x01 << 8)), gpio2_vaddr + GPIO_OE); //set output
	#if defined S2
	value32 = ioread32(gpio2_vaddr + GPIO_OE);
	iowrite32(value32 & (~(0x01 << 7)), gpio2_vaddr + GPIO_OE); //set output
	#endif
	spi_vaddr = ioremap_nocache(McSpi1_Base, McSpi1_Size);
	printk("spi_vaddr %#x\n", (uint32_t)spi_vaddr);
	printk("version %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_REVISION));
	//reset don't sleep
	iowrite32((0x01<<3) | (0x01 << 1), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_SYSSCTL);
	value32 = 0;
	while((ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_SYSSTATUS) & 0x01) == 0)
	{
		if(value32++ == 0xffff)
		{
			printk("Reset spi module timeout\n");
			break;
		}
	}
	printk("OMAP2_MCSPI_SYSSTATUS %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_SYSSTATUS));
	//4针模式（CLK，D0，D1，CS）,即CS使能
	value32 = ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_MODULCTRL);
	iowrite32(value32 & (~(0x01 << 1)), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_MODULCTRL);
	//主模式
	value32 = ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_MODULCTRL);
	iowrite32(value32 & (~(0x01 << 2)), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_MODULCTRL);
	//配置单通道模式，发送/接收模式
	value32 = ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_MODULCTRL);
	iowrite32(value32 | (0x01 << 0), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_MODULCTRL);//单通道
	printk("OMAP2_MCSPI_MODULCTRL %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_MODULCTRL));
	//set dir: clk out, d0 MISO, d1 MOSI
	//value32 = ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_SYST);
	//iowrite32((value32 & (~(0x01 << 10))) | (0x01 << 8) | (0x01 << 5), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_SYST);

	//DEP1 transmission IS=d0
	value32 = ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	iowrite32((value32 & (~(0x01 << 18)) & (~(0x01 << 17))) | (0x01 << 16), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	//spi总线时钟配置: 1 =2 分频1clk精度mode1: clk平时高，上升沿采样
	value32 = ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	iowrite32(((((value32 & (~(0x0f << 2))) | (0x01 << 2)) /*& (~(0x03 << 0))*/ & (~(0x01 << 29))) & (~(0x03 << 0)))|(0x01 << 0), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	//mcspi字长 8bit
	value32 = ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	iowrite32(value32 | (0x07 << 7), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	//spics 极性low ative TCS=0.5
	value32 = ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	iowrite32(value32 | (0x00 << 25) | (0x01 << 6), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	//使能FIFO Transmit/receive modes
	value32 = ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	iowrite32((value32 | (0x03 << 27)) & (~(0x03 << 12)), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	value32 = ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	printk("OMAP2_MCSPI_CHCONF0 %#x\n", value32);
	//iowrite32((27<<8) | (27), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_XFERLEVEL);
	//interrupt rx full
	iowrite32((31<<8) | (31), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_XFERLEVEL);
	#if SPI_USE_INTERRUPT
	spi_dev.irq = 141;
	if (ret = request_irq(spi_dev.irq, mspi_interrupt, IRQF_TRIGGER_LOW, "mspi", (void*)&spi_dev)) {
		printk(KERN_ERR
			"mspi: Failed req IRQ %d, error{%d}\n", spi_dev.irq, ret);
	}
	mutex_init(&spi_dev.transfer_lock);
	init_waitqueue_head(&spi_dev.wait_transfer_complete);
	printk("Initial OMAP2_MCSPI_IRQSTATUS{%#x}OMAP2_MCSPI_IRQENABLE{%#x}\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQSTATUS),
	ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE));
	#endif
	#if 0
	iowrite32(CH_ENA, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCTRL0);
	printk("OMAP2_MCSPI_CHCTRL0 %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCTRL0));
	//cs low  FORCE==1
	iowrite32(ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0) | (0x01 << 20), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	value32 = ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	printk("OMAP2_MCSPI_CHCONF0 %#x\n", value32);
	printk( "000status %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
	for(i=0; i< 0x0ff; i++)
	{
		iowrite32(0x01<<19, gpio3_vaddr + GPIO_SETDATAOUT);
		iowrite32(i, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_TX0);
		//printk( "status %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
		//wait rev
		cnt = 0;
		while( 00 == (ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0) & (0x01<<0)))
		{
			if(cnt++ == 0xff)
			{
				printk( "status %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
				break;
			}
		}
		rddata[i]= ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0);
		//printk("OMAP2_MCSPI_RX0 %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0));
		iowrite32(0x01<<19, gpio3_vaddr + GPIO_CLEARDATAOUT);
	}
	for(i=0; i< 0x0ff; i++)
	{
		if(0 == (i%16))
			printk("\n 0x%02x: ", i);
		printk("%#x, ", rddata[i]);
	}
	//cs high
	iowrite32(ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0) & (~(0x01 << 20)), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	iowrite32(0, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCTRL0);
	#endif
	//while(1)
	{
		for(i= 0; i< 0x100; i++)
		{
			txdata[i] = i;
		}
		i = spi_tranfer(0x55, txdata, 52, rddata, &rx_len);
	}
}
void spi_close(void)
{
	void *ctl_md_vaddr;
	iowrite32(0, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE);
	ctl_md_vaddr = ioremap_nocache(CONTROL_MODULE_BASE, CONTROL_MODULE_Size);
	iowrite32(PAD_PULL_DIS | 0x7, ctl_md_vaddr + conf_mcasp0_fsx); //D0 MISO
	iounmap(ctl_md_vaddr);
	#if SPI_USE_INTERRUPT
	free_irq(spi_dev.irq, &spi_dev);
	#endif
}
#if 0
int spi_tranfer(uint8_t cmd, uint8_t *tx_data, uint16_t tx_len, uint8_t *rx_data, uint16_t *rx_len)
{
	uint16_t i, j;
	uint32_t cnt;
	int32_t value32;
	uint16_t h_tx_len;
	unsigned long flags;
	iowrite32(CH_ENA, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCTRL0);
	//printk("OMAP2_MCSPI_CHCTRL0 %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCTRL0));
	//cs low  FORCE==1
	iowrite32(ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0) | (0x01 << 20), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	value32 = ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	//printk("OMAP2_MCSPI_CHCONF0 %#x\n", value32);
	//printk( "000status %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
#if SPI_USE_INTERRUPT
		spi_dev.p_rx = rx_data;
		spi_dev.p_tx = tx_data;
		spi_dev.length = tx_len;
		spi_dev.rev_len = 0;
		spi_dev.have_wake_up = false;
		h_tx_len = tx_len < 32 ? tx_len: tx_len - 32;
		while((ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0) & (0x01<<5)) == 0)
		{
			spi_dev.p_rx[spi_dev.rev_len]= ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0);
		}
		cnt = 0;
		while((ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0) & (0x01<<4)) != 0)
		{
			if(cnt++ == 0xff)
			{
				printk(KERN_ERR "wait tx fifo empty timeout\n");
				return 0;
			}
		}
		local_irq_save(flags);
		for(i = 0; i < 32; i++)//32字节 FIFO, 数据长度52字节
		{
			iowrite32(tx_data[i], spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_TX0);
			/*
			if((ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0) & (0x01<<4)) != 0)
			{
				printk(KERN_ERR "fifo full status %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
			}
			if(i == (tx_len - 1)) //total < 32
				break;
			*/
		}
		//while((spi_dev.rev_len < tx_len ) && (spi_dev.rev_len < (tx_len - 32)))
		while(spi_dev.rev_len < h_tx_len)
		{
			if((ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0) & (0x01<<5)) == 0)
			{
				spi_dev.p_rx[spi_dev.rev_len++]= ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0);
				cnt = 0;
			}
			else if(cnt++ == 0xff)
			{
				printk(KERN_ERR "status %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
				break;
			}
		}
		iowrite32(0x01<<8, gpio2_vaddr + GPIO_SETDATAOUT);
		if(spi_dev.rev_len != tx_len)
		{
			for(; i< tx_len; i++)
			{
				iowrite32(tx_data[i], spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_TX0);
				/*
				if((ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0) & (0x01<<4)) != 0)
				{
					printk(KERN_ERR "fifo full status %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
				}
				*/
			}
			//clear tx empty
			iowrite32(0x0f, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQSTATUS);
			//enable interrupt
			iowrite32(ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE) | (TX0_EMPTY), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE);
			local_irq_restore(flags);
			iowrite32(0x01<<8, gpio2_vaddr + GPIO_CLEARDATAOUT);
			//printk(KERN_ERR "OMAP2_MCSPI_IRQENABLE{%#x}\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQSTATUS),\
			ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE));
			#if 0
			if(spi_dev.have_wake_up == false)
			{
				//if(0 == interruptible_sleep_on_timeout(&spi_dev.wait_transfer_complete, 10 * HZ/1000))//10ms
				if(0 == wait_event_interruptible_timeout(&spi_dev.wait_transfer_complete, spi_dev.have_wake_up == true, 10 * HZ/1000))//10ms
				{
					iowrite32(0x01<<6, gpio2_vaddr + GPIO_SETDATAOUT);
					printk(KERN_ERR "spi transfer timeout spi_dev.rev_len{%d}\n", spi_dev.rev_len);
					//*rx_len = 0;
					iowrite32(0x01<<6, gpio2_vaddr + GPIO_CLEARDATAOUT);
				}
			}
			#endif
			if(0 == wait_event_interruptible_timeout(spi_dev.wait_transfer_complete, spi_dev.have_wake_up == true, 10 * HZ/1000))//10ms
			{
				iowrite32(0x01<<6, gpio2_vaddr + GPIO_SETDATAOUT);
				printk(KERN_ERR "spi transfer timeout spi_dev.rev_len{%d}\n", spi_dev.rev_len);
				//*rx_len = 0;
				iowrite32(0x01<<6, gpio2_vaddr + GPIO_CLEARDATAOUT);
			}
			*rx_len = spi_dev.rev_len;
			//printk(KERN_ERR "tx_len %d\n", i);
		}
		else
		{
			local_irq_restore(flags);
			*rx_len = tx_len;
		}
#elif 1
	//iowrite32(0x01<<19, gpio3_vaddr + GPIO_SETDATAOUT);
	//clear RX0_FULL
	iowrite32(0x01<<2, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQSTATUS);
	for(i = 0, j =0; i< tx_len; i++)
	{
		iowrite32(tx_data[i], spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_TX0);
		//printk( "status %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
		//receive fifo almost full
		if((ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQSTATUS) & (0x01<<2)))
		{
			//no empty
			while( 00 == (ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0) & (0x01<<5)))
			{
				rx_data[j++]= ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0);
				// transfer fifo almost empty
				if((ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0) & (0x01<<3)))
				{
					break;
				}
			}
			iowrite32(0x01<<2, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQSTATUS);
		}
		//printk("OMAP2_MCSPI_RX0 %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0));
	}
	cnt = 0;
	while(j < tx_len)
	{
		if((ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0) & (0x01<<0)))
		{
			rx_data[j++]= ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0);
			cnt = 0;
		}
		else if(cnt++ == 0xff)
		{
			printk( "status %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
			break;
		}
	}
	*rx_len = i;
	//iowrite32(0x01<<19, gpio3_vaddr + GPIO_CLEARDATAOUT);
#else
	i = 0;
	iowrite32(cmd, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_TX0);
	//printk( "status %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
	//wait rev
	cnt = 0;
	while( 00 == (ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0) & (0x01<<0)))
	{
		if(cnt++ == 0xff)
		{
			printk( "status %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
			break;
		}
	}
	rx_data[i]= ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0);
	//tx rx data;
	//printk("tx_len %#x\n", tx_len);
	for(i = 1; i< (tx_len + 1); i++)
	{
		iowrite32(0x01<<19, gpio3_vaddr + GPIO_SETDATAOUT);
		iowrite32(tx_data[i - 1], spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_TX0);
		//printk( "status %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
		//wait rev
		cnt = 0;
		while( 00 == (ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0) & (0x01<<0)))
		{
			if(cnt++ == 0xff)
			{
				printk( "status %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
				break;
			}
		}
		rx_data[i]= ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0);
		//printk("OMAP2_MCSPI_RX0 %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0));
		iowrite32(0x01<<19, gpio3_vaddr + GPIO_CLEARDATAOUT);
	}
	*rx_len = i;
#endif
	//cs high
	iowrite32(ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0) & (~(0x01 << 20)), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	iowrite32(0, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCTRL0);
#if 0//SPI_USE_INTERRUPT
	printk(KERN_ERR "rx_len %#x\n", *rx_len);
	for(i = 0; i < *rx_len; i++)
	{
		if(0 == (i%16))
			printk(KERN_ERR "\n 0x%02x: ", i);
		printk(KERN_ERR "0x%02x, ", rx_data[i]);
	}
	printk(KERN_ERR "\n");
#endif
	#if 0
	if(*rx_len != (tx_len + 1))// 1 cmd
		return -1;
	#else
	if(*rx_len != tx_len)
	{
		printk(KERN_ERR "SPI Transfer error\n");
		return -1;
	}
	#endif
	return *rx_len;
}
#else
int spi_tranfer(uint8_t cmd, uint8_t *tx_data, uint16_t tx_len, uint8_t *rx_data, uint16_t *rx_len)
{
	uint16_t i, j;
	uint32_t cnt, cnt1;
	int32_t value32;
	uint16_t h_tx_len;
	unsigned long flags;
	int ret;
	mutex_lock(&spi_dev.transfer_lock);
	spi_dev.have_wake_up = false;
	switch(cmd)
	{
		case 1: //query
			spi_dev.p_rx = rx_data;
			spi_dev.p_tx = tx_data;
			spi_dev.length = tx_len;
			spi_dev.rev_len = 0;
			spi_dev.have_wake_up = false;
			h_tx_len = tx_len < 32 ? tx_len: tx_len - 32;
			//使能Transmit and rx FIFO
			value32 = ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
			value32 &= ~((0x03<<27) | (0x03<<12));
			iowrite32(value32 | (0x03 << 27) | (0x00 << 12), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
			iowrite32((tx_len << 16) | (31<<8) | 31, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_XFERLEVEL);
			//cs low  FORCE==1
			iowrite32(ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0) | (0x01 << 20), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
			iowrite32(EOW | 0x0f, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQSTATUS);
			//enable EOW interrupt
			iowrite32(ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE) | (EOW), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE);
			iowrite32(CH_ENA, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCTRL0);
			local_irq_save(flags);
			for(i = 0; i < 32; i++)//32字节 FIFO, 数据长度52字节
			//for(i = 0; i < h_tx_len; i++)
			{
				iowrite32(tx_data[i], spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_TX0);
			}
			while(spi_dev.rev_len < h_tx_len)
			{
				if((ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0) & (0x01<<5)) == 0)
				{
					spi_dev.p_rx[spi_dev.rev_len++]= ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0);
					cnt = 0;
				}
				else if(cnt++ == 0xff)
				{
					printk(KERN_ERR "status %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
					break;
				}
			}
			//iowrite32(0x01<<8, gpio2_vaddr + GPIO_SETDATAOUT);
			if(spi_dev.rev_len != tx_len)
			{
				//iowrite32((31<<8) | h_tx_len, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_XFERLEVEL);
				for(; i< tx_len; i++)
				{
					iowrite32(tx_data[i], spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_TX0);

					if((ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0) & (0x01<<4)) != 0)
					{
						printk(KERN_ERR "fifo full status %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
					}
				/*	*/
				}
				local_irq_restore(flags);
				//iowrite32(0x01<<8, gpio2_vaddr + GPIO_CLEARDATAOUT);
				//printk(KERN_ERR "OMAP2_MCSPI_IRQENABLE{%#x}\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQSTATUS),\
				ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE));
				if(0 == wait_event_interruptible_timeout(spi_dev.wait_transfer_complete, spi_dev.have_wake_up == true, 10 * HZ/1000))//10ms
				{
					iowrite32(ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE) & (~ (EOW|TX0_EMPTY)), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE);
					//iowrite32(0x01<<6, gpio2_vaddr + GPIO_SETDATAOUT);
					cnt = 0;
					while(00 == (ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0) & (0x01<<5)))
					{
						spi_dev.p_rx[spi_dev.rev_len++]= ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0);
						if(cnt++ >32)
						{
							printk(KERN_ERR "detect spi rx err\n");
							break;
						}
					}
					spi_dev.p_rx[spi_dev.rev_len++]= ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0);
					if(spi_dev.have_wake_up == false)
						printk(KERN_ERR "22spi transfer timeout IRQSTATUS{%#x}XFERLEVEL{%#x}tx_len{%d}CHSTAT0{%#x}\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQSTATUS),
							ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_XFERLEVEL), i ,ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
					else
						printk("22wk up err\n");
					iowrite32(ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE) & (~EOW), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE);
					//*rx_len = 0;
					//iowrite32(0x01<<6, gpio2_vaddr + GPIO_CLEARDATAOUT);
				}
				else
				{
					cnt = 0;
					cnt1 = 0;
					while(1)
					{
						//no empty
						while(00 == (ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0) & (0x01<<5)))
						{
							spi_dev.p_rx[spi_dev.rev_len++]= ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0);
							if(cnt++ >32)
							{
								printk(KERN_ERR "detect spi rx err\n");
								break;
							}
							cnt1 = 0;
						}
						value32 = ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0);
						//发送接收完成
						if((value32 & (TXFFE | RXFFE)) == (TXFFE | RXFFE))
						{
							spi_dev.p_rx[spi_dev.rev_len++]= ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_RX0);
							break;
						}
						if(cnt1++ >10000)
						{
							printk(KERN_ERR "detect spi rx tx empty err{%#x}\n", value32);
							break;
						}
					}
				}
				*rx_len = spi_dev.rev_len;
				if(*rx_len != tx_len)
				{
					printk(KERN_ERR "SPI Transfer error *rx_len{%d}\n", *rx_len);
					while(*rx_len < tx_len)
					{
						spi_dev.p_rx[*rx_len++] = 0;
					}
				}
			}
			else
			{
				local_irq_restore(flags);
				*rx_len = tx_len;
			}
			ret = *rx_len;
			/*
			printk(KERN_ERR "rx_len %#x\n", *rx_len);
			for(i = 0; i < *rx_len; i++)
			{
				if(0 == (i%16))
					printk(KERN_ERR "\n 0x%02x: ", i);
				printk(KERN_ERR "0x%02x, ", rx_data[i]);
			}
			printk(KERN_ERR "\n");
			*/
			break;
		case 0: //to asic cmd
		default: //tw
			ret = -1; //don't care return data
			//使能only Transmit FIFO
			value32 = ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
			value32 &= ~((0x03<<27) | (0x03<<12));
			iowrite32((value32 | (0x01 << 27)) | (0x02 << 12), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
			iowrite32(((tx_len-1)<<16) | (tx_len-1), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_XFERLEVEL);
			//cs low  FORCE==1
			iowrite32(ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0) | (0x01 << 20), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
			iowrite32(EOW | 0x0f, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQSTATUS);
			//enable EOW interrupt
			iowrite32(ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE) | (EOW), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE);
			iowrite32(CH_ENA, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCTRL0);
			//printk("OMAP2_MCSPI_CHCTRL0 %#x\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCTRL0));
			local_irq_save(flags);
			//iowrite32(0x01<<8, gpio2_vaddr + GPIO_SETDATAOUT);
			for(i = 0; i < tx_len; i++)
			{
				iowrite32(tx_data[i], spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_TX0);
			}
			//iowrite32(0x01<<8, gpio2_vaddr + GPIO_CLEARDATAOUT);
			local_irq_restore(flags);
			if(cmd == 0)
				udelay(10);
				//printk("tx_len = %d\n", tx_len);
			if(0 == wait_event_interruptible_timeout(spi_dev.wait_transfer_complete, spi_dev.have_wake_up == true, 10 * HZ/1000))//10ms
			{
				//iowrite32(0x01<<6, gpio2_vaddr + GPIO_SETDATAOUT);
				if(spi_dev.have_wake_up == false)
					printk(KERN_ERR "111spi transfer timeout IRQSTATUS{%#x}IRQENABLE{%#x}XFERLEVEL{%#x}CHSTAT0{%#x}\n", ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQSTATUS),
						ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE),ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_XFERLEVEL),ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHSTAT0));
				else
					printk("11wk up err\n");
				iowrite32(ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE) & (~EOW), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_IRQENABLE);
				//iowrite32(0x01<<6, gpio2_vaddr + GPIO_CLEARDATAOUT);
			}
			break;
	}
	//cs high
	iowrite32(ioread32(spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0) & (~(0x01 << 20)), spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCONF0);
	iowrite32(0, spi_vaddr + OMAP2_MCSPI_OFFSET + OMAP2_MCSPI_CHCTRL0);
	mutex_unlock(&spi_dev.transfer_lock);
	return ret;
}

#endif
