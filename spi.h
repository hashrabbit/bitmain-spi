#ifndef __OMAP2_SPI_H__
#define __OMAP2_SPI_H__

#define CM_PER_BASE		0x44E00000
#define CM_PER_Size		1024		//1K

#define CM_PER_SPI1_CLKCTRL		0x50
#define	CM_PER_GPIO2_CLKCTRL	0xB0
#define	CM_PER_GPIO3_CLKCTRL	0xB4

#define CONTROL_MODULE_BASE		0x44E10000
#define CONTROL_MODULE_Size		(128 * 1024)		//128K
#define conf_uart0_ctsn		0x968	//SP1 D0	mode=4
#define conf_uart0_rtsn		0x96c	//SP1 D1	mode=4
#define conf_uart1_ctsn		0x978	//SP1 CS0	mode=4
#define conf_uart1_rtsn		0x97c	//SP1 CS1	mode=4
#define conf_mcasp0_aclkx	0x990	//SP1 clk	mode=3
#define conf_mcasp0_fsx		0x994	//SP1 d0	mode=3
#define conf_mcasp0_axr0	0x998	//SP1 d1	mode=3
#define conf_mcasp0_ahclkr	0x99c	//SP1 CS0	mode=3
#define conf_mcasp0_fsr		0x9a4	//GPIO3_19 mode=7
#define conf_mcasp0_ahclkx  0x9ac	//GPIO3_21 mode=7


#define	PAD_REV			(0x01<<5)
#define PAD_PULL_DIS	(0x01<<3)
#define PAD_PULLUP		(0x02<<3)
#define PAD_PULLDONE	(0x00<<3)

#define GPIO0_BASE		0x44e07000
#define GPIO0_SIZE		0x0fff	//4K
#define GPIO1_BASE		0x4804C000
#define GPIO1_SIZE		0x0fff	//4K


#define GPIO2_BASE		0x481AC000
#define GPIO2_SIZE		0x0fff	//4K

#define GPIO3_BASE		0x481AE000
#define GPIO3_SIZE		0x0fff	//4K
	#define	GPIO_OE				0x134
	#define GPIO_DATAIN			0x138
	#define GPIO_DATAOUT		0x13C
	#define GPIO_CLEARDATAOUT	0x190
	#define GPIO_SETDATAOUT		0x194
#define McSpi1_Base		0x481A0000
#define McSpi1_Size		0x0fff	//4K

#define OMAP2_MCSPI_MAX_FREQ		48000000
#define SPI_AUTOSUSPEND_TIMEOUT		2000

#define OMAP2_MCSPI_REVISION		0x00
#define	OMAP2_MCSPI_OFFSET			0x100
#define OMAP2_MCSPI_SYSSCTL			0x10
#define OMAP2_MCSPI_SYSSTATUS		0x14
#define OMAP2_MCSPI_IRQSTATUS		0x18
	#define TX0_EMPTY	(0x01<<0)
	#define RX0_FULL	(0x01<<2)
	#define EOW			(0x01<<17)
#define OMAP2_MCSPI_IRQENABLE		0x1c
#define OMAP2_MCSPI_WAKEUPENABLE	0x20
#define OMAP2_MCSPI_SYST			0x24
	#define SPIEN_0			(0x01 << 0)
#define OMAP2_MCSPI_MODULCTRL		0x28
#define OMAP2_MCSPI_XFERLEVEL		0x7c

/* per-channel banks, 0x14 bytes each, first is: */
#define OMAP2_MCSPI_CHCONF0		0x2c
#define OMAP2_MCSPI_CHSTAT0		0x30
	#define TXFFE	(0x01<<3)
	#define RXFFE	(0x01<<5)
#define OMAP2_MCSPI_CHCTRL0		0x34
	#define CH_ENA				0x01
#define OMAP2_MCSPI_TX0			0x38
#define OMAP2_MCSPI_RX0			0x3c

typedef struct __mspi_struct
{
	int irq;
	void *spi_vaddr;
	unsigned char *p_tx;
	unsigned char *p_rx;
	unsigned int length;
	unsigned int rev_len;
	struct mutex transfer_lock;
	bool have_wake_up;
	wait_queue_head_t wait_transfer_complete;

}McSPI;

#define SPI_USE_INTERRUPT	1

extern void *gpio3_vaddr ,*gpio2_vaddr;
extern void spi_init(void);
void spi_close(void);
extern int spi_tranfer(uint8_t cmd, uint8_t *tx_data, uint16_t tx_len, uint8_t *rx_data, uint16_t *rx_len);
#endif

