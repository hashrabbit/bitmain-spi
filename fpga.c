#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include "bitmain-asic.h"
#include "sha2.h"
#include "spi.h"
#include "fpga.h"
#include "set_pll.h"

volatile struct ASIC_RESULT asic_result[512];
uint16_t asic_result_wr = 0, asic_result_rd = 0, asic_result_full = 0;
bool is_started = false;
uint32_t asic_result_status[512];
uint16_t asic_result_status_wr = 0, asic_result_status_rd = 0, asic_result_status_full = 0;

static uint8_t g_rx_data[sizeof(FPGA_RETURN)];
uint16_t g_FPGA_FIFO_SPACE = 100;
uint16_t g_TOTAL_FPGA_FIFO;
uint16_t g_FPGA_RESERVE_FIFO_SPACE = 1;
unsigned long open_core_time = 0;

void rev(unsigned char *s, size_t l)
{
	size_t i, j;
	unsigned char t;

	for (i = 0, j = l - 1; i < j; i++, j--) {
		t = s[i];
		s[i] = s[j];
		s[j] = t;
	}
}
unsigned char CRC5(unsigned char *ptr, unsigned char len)
{
    unsigned char i, j, k;
    unsigned char crc = 0x1f;

    unsigned char crcin[5] = {1, 1, 1, 1, 1};
    unsigned char crcout[5] = {1, 1, 1, 1, 1};
    unsigned char din = 0;

    j = 0x80;
    k = 0;
    for (i = 0; i < len; i++)
    {
    	if (*ptr & j) {
    	 din = 1;
    	} else {
    	 din = 0;
    	}
    	crcout[0] = crcin[4] ^ din;
    	crcout[1] = crcin[0];
    	crcout[2] = crcin[1] ^ crcin[4] ^ din;
    	crcout[3] = crcin[2];
    	crcout[4] = crcin[3];

        j = j >> 1;
        k++;
        if (k == 8)
        {
            j = 0x80;
            k = 0;
            ptr++;
        }
        memcpy(crcin, crcout, 5);
    }
    crc = 0;
    if(crcin[4]) {
    	crc |= 0x10;
    }
    if(crcin[3]) {
    	crc |= 0x08;
    }
    if(crcin[2]) {
    	crc |= 0x04;
    }
    if(crcin[1]) {
    	crc |= 0x02;
    }
    if(crcin[0]) {
    	crc |= 0x01;
    }
    return crc;
}

extern int bitmain_asic_get_status(char* buf, char chain, char mode, char chip_addr, char reg_addr)
{
    unsigned char cmd_buf[4];
    if (buf == NULL)
        buf = cmd_buf;
    memset(buf, 0, 4);
    buf[0] = 4;
    buf[1] = chip_addr;
    buf[2] = reg_addr;
	if (mode)//all
        buf[0] |= 0x80;
    buf[3] = CRC5(buf, 4*8 - 5);
	printk(KERN_ERR "get chain%d reg%#x\n", chain, reg_addr);
	send_BC_to_fpga(chain, buf);
    return 4;
}

int bitmain_asic_inactive(char* buf, char chain)
{
	unsigned char cmd_buf[4];
	if (buf == NULL)
        buf = cmd_buf;
    memset(buf, 0, 4);
	buf[0] = 5;
	buf[0] |= 0x80;
	buf[3] = CRC5(buf, 4*8 - 5);
	printk(KERN_ERR "chain%d inactive\n", chain);
	send_BC_to_fpga(chain, buf);
	return 4;
}

int bitmain_asic_set_addr(char* buf, char chain, char mode, char chip_addr)
{
	unsigned char cmd_buf[4];
	if (buf == NULL)
        buf = cmd_buf;
	memset(buf, 0, 4);
	buf[0] = 1;
	buf[1] = chip_addr;
	if (mode)//all
		buf[0] |= 0x80;
	buf[3] = CRC5(buf, 4*8 - 5);
	send_BC_to_fpga(chain, buf);
	return 4;
}

void bitmain_set_voltage(BT_AS_INFO dev, unsigned short voltage)
{
	unsigned char cmd_buf[4];
	char* buf = NULL;
	unsigned char i;
	wait_queue_head_t timeout_wq;
	init_waitqueue_head(&timeout_wq);
    if (buf == NULL)
        buf = cmd_buf;
	memset(buf, 0, 4);
	buf[0] = 0xaa;
	buf[1] = (unsigned char )((voltage>>8) & 0xff);
	buf[1] &=0x0f;
	buf[1] |=0xb0;
	buf[2]= (unsigned char )(voltage & 0xff);
	buf[3] = CRC5(buf, 4*8 - 5);
	buf[3] |=0xc0;
	printk(KERN_ERR "set_voltage cmd_buf[0]{%#x}cmd_buf[1]{%#x}cmd_buf[2]{%#x}cmd_buf[3]{%#x}\n",
		cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3]);
	for(i = 0; (i < sizeof(dev->chain_exist) * 8); i++)
    {
		if((dev->chain_exist) & (0x01 << i))
			send_BC_to_fpga(i, cmd_buf);
		interruptible_sleep_on_timeout(&timeout_wq, 5 * HZ/1000);//5ms
    }
}

int bitmain_asic_set_frequency(char* buf, char mode, char chip_addr , char reg_addr, char reg_value)
{
	memset(buf, 0, 4);
	buf[0] = 2;
	buf[1] = chip_addr;
	buf[3] = CRC5(buf, 4);
	if(mode)//all
		buf[0] |= 0x80;
	return 4;
}

/***************************************************************
freq = 25M / (NR+1) *(M+1) / 2^OD  reg default 0xc01e0002 193MHz
****************************************************************/

#ifdef S5_S_VL
void set_frequency(BT_AS_INFO dev, unsigned int freq)
{
    unsigned char cmd_buf[4] = {0};
	unsigned char gateblk[4] = {0};
	struct ASIC_TASK asic_work;
	unsigned char chain_num = 0;
	unsigned char chain_id;
	unsigned int i,j,k;
	unsigned int cnt;
	unsigned char chip_addr = 0;
	unsigned char chip_interval = 2;
	unsigned char bauddiv = 0;
	unsigned char save_timeout = 0;
	unsigned char have_clear_fifo = false;
	bool send_new_block = false;
	wait_queue_head_t timeout_wq;
	init_waitqueue_head(&timeout_wq);
    //bitmain_asic_set_frequency(cmd_buf, 1, freq);
	bauddiv = 26;
	gateblk[0] = 6;
    gateblk[1] = 00;//0x10; //16-23
    //gateblk[2] = 26 | 0x80; //8-15 gateblk=1
    gateblk[2] = bauddiv | 0x80; //8-15 gateblk=1
    gateblk[0] |= 0x80;
    gateblk[3] = CRC5(gateblk, 4*8 - 5);
	memset(asic_work.midstate, 0x00, sizeof(asic_work.midstate));
	memset(asic_work.data, 0x00, sizeof(asic_work.data));
	dev->timeout_valid = true;
	save_timeout = dev->asic_configure.timeout_data;
	dev->asic_configure.timeout_data = 7;
	nonce_query(dev);
	dev->timeout_valid = false;
	dev->asic_configure.timeout_data = save_timeout;
	iowrite32(0x01<<22, gpio0_vaddr + GPIO_SETDATAOUT); //set test
	udelay(100); // test  transfer delay
	#ifndef BM1385
    for(i = 0; (i < sizeof(dev->chain_exist) * 8); i++)
    {
		if((dev->chain_exist) & (0x01 << i))
		{
			printk(KERN_ERR "close chain%d core\n", i);
			cmd_buf[0] = 2;
		    cmd_buf[1] = (freq)&0xff; //16-23
		    cmd_buf[2] = (freq >> 8)&0xff; //8-15
		    cmd_buf[0] |= 0x80;
		    cmd_buf[3] = CRC5(cmd_buf, 4*8 - 5);
		    printk(KERN_ERR "set_frequency cmd_buf[0]{%#x}cmd_buf[1]{%#x}cmd_buf[2]{%#x}cmd_buf[3]{%#x}\n",
				cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3]);
			send_BC_to_fpga(i, cmd_buf);
			interruptible_sleep_on_timeout(&timeout_wq, 2 * HZ/1000);//2ms
			chip_addr = 0;
			bitmain_asic_inactive(cmd_buf, i);
			interruptible_sleep_on_timeout(&timeout_wq, 5 * HZ/1000);//5ms
			if(gChain_Asic_Interval[i] !=0 )
				chip_interval = gChain_Asic_Interval[i];
			for(j = 0; j < 0x100/chip_interval; j++)
			{
				bitmain_asic_set_addr(cmd_buf, i, 0, chip_addr);
				chip_addr += chip_interval;
				interruptible_sleep_on_timeout(&timeout_wq, 5 * HZ/1000);//5ms
			}

			#ifdef CTL_ASIC_CORE
			for(j = 0; j < 1; j++)
			{
				send_BC_to_fpga(i, gateblk);
				interruptible_sleep_on_timeout(&timeout_wq, 5 * HZ/1000);//2ms
				chain_id = 0xc0 | i;
				for(k = 0; k < BMSC_CORE/* j < 10000000*/; k++)
				{
					if(k <= j)
						asic_work.data[0] = 0;
					else
						asic_work.data[0] = 0;
					if(g_FPGA_FIFO_SPACE <= g_FPGA_RESERVE_FIFO_SPACE)
					{
						cnt = 0;
						do
						{
							interruptible_sleep_on_timeout(&timeout_wq, 50 * HZ/1000);// 50 = ms
							nonce_query(dev);
							if( cnt++ > 100) //100 * 50ms = 5s;
							{
								printk(KERN_ERR "close croe timeout1 g_FPGA_FIFO_SPACE{%d}\n", g_FPGA_FIFO_SPACE);
								return;
								break;
							}
						}while(g_FPGA_FIFO_SPACE < (g_FPGA_RESERVE_FIFO_SPACE + BMSC_CORE));
					}
					if((k == 0) && (j == 0) && (have_clear_fifo == false))
					{
						send_work_to_fpga(true, chain_id, dev, &asic_work);
						nonce_query(dev);
						have_clear_fifo == true;
					}
					else
						send_work_to_fpga(false, chain_id, dev, &asic_work);
					g_FPGA_FIFO_SPACE--;
					//if(j == 1)
					//	break;
				}
				//interruptible_sleep_on_timeout(&timeout_wq, 1 * 1000 * HZ/1000);// 1 *1000 = 1s
				//wait all send to asic
				cnt = 0;
				do
				{
					interruptible_sleep_on_timeout(&timeout_wq, 50 * HZ/1000);// 50 = ms
					nonce_query(dev);
					if( cnt++ > 100) //100 * 200ms = 20s;
					{
						printk(KERN_ERR "close core timeout2 g_FPGA_FIFO_SPACE{%d}\n", g_FPGA_FIFO_SPACE);
						break;
					}
				}while(g_FPGA_FIFO_SPACE < (g_TOTAL_FPGA_FIFO * 4/48 - 1));
				#ifdef S4_PLUS
				send_work_to_fpga(false, 0xc0 | i, dev, &asic_work);
				#else
				send_work_to_fpga(false, 0x80 | i, dev, &asic_work);
				#endif
			}
			#endif
			chain_num++;
		}
    }
    #else
    // BM1385 set_frequency
	#if 1
    uint32_t reg_data_pll = 0;
	uint16_t reg_data_pll2 = 0;
	printk("set freq = %d\n", freq);
    get_plldata(1385,freq,&reg_data_pll,&reg_data_pll2);
    for(i = 0; (i < sizeof(dev->chain_exist) * 8); i++)
    {
		if((dev->chain_exist) & (0x01 << i))
		{
			#if 1
			//set plldivider1
			//memcpy((char *)cmd_buf,&reg_data_pll,sizeof(reg_data_pll));
			cmd_buf[0] = 0;
			cmd_buf[0] |= 0x7;//cmd
			cmd_buf[1] = (reg_data_pll >> 16) & 0xff;
			cmd_buf[2] = (reg_data_pll >> 8) & 0xff;
			cmd_buf[3] = (reg_data_pll >> 0) & 0xff;
			cmd_buf[3] |= CRC5(cmd_buf, 4*8 - 5);

		    printk(KERN_ERR "plldivider1 cmd_buf[0]{%#x}cmd_buf[1]{%#x}cmd_buf[2]{%#x}cmd_buf[3]{%#x}\n",
				cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3]);
			send_BC_to_fpga(i, cmd_buf);

            interruptible_sleep_on_timeout(&timeout_wq, 2 * HZ/1000);//2ms
            //set plldivider2
            memset(cmd_buf,0,sizeof(cmd_buf));
			cmd_buf[0] = 0x2;	//cmd
			cmd_buf[0] |= 0x80;	//all
			cmd_buf[1] = 0;		//addr
			cmd_buf[2] = reg_data_pll2 >> 8;
			cmd_buf[3] = reg_data_pll2& 0x0ff;
			//memcpy((char *)cmd_buf + 2,&reg_data_pll2,sizeof(reg_data_pll2));	//postdiv data
			cmd_buf[3] |= CRC5(cmd_buf, 4*8 - 5);

            printk(KERN_ERR "plldivider2 cmd_buf[0]{%#x}cmd_buf[1]{%#x}cmd_buf[2]{%#x}cmd_buf[3]{%#x}\n",
				cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3]);
			send_BC_to_fpga(i, cmd_buf);

			interruptible_sleep_on_timeout(&timeout_wq, 2 * HZ/1000);//2ms
			#endif
			#if 1
			chip_addr = 0;
			bitmain_asic_inactive(cmd_buf, i);
			interruptible_sleep_on_timeout(&timeout_wq, 5 * HZ/1000);//5ms
			if(gChain_Asic_Interval[i] !=0 )
				chip_interval = gChain_Asic_Interval[i];
			chip_interval = 4;
			for(j = 0; j < 0x100/chip_interval; j++)
			{
				bitmain_asic_set_addr(cmd_buf, i, 0, chip_addr);
				chip_addr += chip_interval;
				interruptible_sleep_on_timeout(&timeout_wq, 10 * HZ/1000);//5ms
			}
			#endif
			chain_num++;
		}
    }
	#endif
	#endif
	iowrite32(0x01<<22, gpio0_vaddr + GPIO_CLEARDATAOUT); //clear test
	//interruptible_sleep_on_timeout(&timeout_wq, 20* 1000 * HZ/1000);
	#if 1
	//modify baud
	bauddiv = get_baud(freq);;
	cmd_buf[0] = 6;
	cmd_buf[1] = 0x10; //16-23
	cmd_buf[2] = bauddiv & 0x1f; //8-13
	cmd_buf[0] |= 0x80;
	cmd_buf[3] = CRC5(cmd_buf, 4*8 - 5);
	for(i = 0; (i < sizeof(dev->chain_exist) * 8); i++)
    {
		if((dev->chain_exist) & (0x01 << i))
		{
			send_BC_to_fpga(i, cmd_buf);
			interruptible_sleep_on_timeout(&timeout_wq, 2 * HZ/1000);//2ms
		}
	}
	dev->asic_configure.bauddiv = bauddiv;
	cmd_buf[3] = CRC5(gateblk, 4*8 - 5); //故意错误crc 只是修改fpga 波特率
	send_BC_to_fpga(i, cmd_buf);
	//interruptible_sleep_on_timeout(&timeout_wq, 2 * HZ/1000);//2ms
	#endif
	//interruptible_sleep_on_timeout(&timeout_wq, 50 * HZ/1000);//2ms
	//bitmain_asic_get_status(NULL,dev->chain_map[0], 1, 0, 0x00); //CHIP_ADDR_REG 4	PLL reg
	// 从新计算getblck命令
	gateblk[0] = 6;
    	gateblk[1] = 00;//0x10; //16-23
    	gateblk[2] = bauddiv | 0x80; //8-15 gateblk=1
    	gateblk[0] |= 0x80;
		gateblk[3] = 0x00;
    	gateblk[3] = CRC5(gateblk, 4*8 - 5);

	printk(KERN_ERR "bauddiv %d ",bauddiv);
	printk(KERN_ERR "gateblk2 %x\n",gateblk[2]);

	/**/
	#if 1
	#ifdef CTL_ASIC_CORE
	save_timeout = dev->asic_configure.timeout_data;
	dev->asic_configure.timeout_data *=32;
	dev->asic_configure.timeout_data = 130;
	dev->timeout_valid = true;
	nonce_query(dev);
	dev->timeout_valid = false;
	dev->wait_timeout = false;
	//\BF\AAcore
	memset(asic_work.midstate, 0xff, sizeof(asic_work.midstate));
	memset(asic_work.data, 0xff, sizeof(asic_work.data));
	printk(KERN_ERR "open core\n");
	for(j = 0; j < /*55*/1; j++)
	{
		iowrite32(0x01<<8, gpio2_vaddr + GPIO_SETDATAOUT);
		for(i = 0; (i < sizeof(dev->chain_exist) * 8); i++)
		{
			if((dev->chain_exist) & (0x01 << i))
			{
				send_BC_to_fpga(i, gateblk);
				interruptible_sleep_on_timeout(&timeout_wq, 10 * HZ/1000);//2ms
			}
		}
		for(k = 0; k < 64; k++)
		{
			if(k <= j)
			{
				asic_work.data[0] = 0xff;
				asic_work.data[11] = 0xff;
			}
			else
			{
				asic_work.data[0] = 0xff;
				asic_work.data[11] = 0xff;
			}
			for(i = 0; (i < sizeof(dev->chain_exist) * 8); i++)
			{
				if((dev->chain_exist) & (0x01 << i))
				{
					chain_id = 0x80 | i;
					if(g_FPGA_FIFO_SPACE <= g_FPGA_RESERVE_FIFO_SPACE)
					{
						cnt = 0;
						do
						{
							interruptible_sleep_on_timeout(&timeout_wq, 50 * HZ/1000);// 50 = ms
							nonce_query(dev);
							if( cnt++ > 100) //100 * 50ms = 5s;
							{
								printk(KERN_ERR "open core timeout1 g_FPGA_FIFO_SPACE{%d}\n", g_FPGA_FIFO_SPACE);
								//return;
								break;
							}
						}while(g_FPGA_FIFO_SPACE < (g_FPGA_RESERVE_FIFO_SPACE /*+ 63 - k *//*+ BMSC_CORE*/));
					}
					if(k == 63)
						chain_id = 0xc0 | i;
					if((k == 0) && (send_new_block == false))
					{
						send_new_block = true;
						send_work_to_fpga(true, chain_id, dev, &asic_work);
					}
					else
						send_work_to_fpga(false, chain_id, dev, &asic_work);
					g_FPGA_FIFO_SPACE--;
				}
			}
			//printk("k = %d\n", k);
		}
		//printk("j = %d\n", j);
		iowrite32(0x01<<8, gpio2_vaddr + GPIO_CLEARDATAOUT);
		//interruptible_sleep_on_timeout(&timeout_wq, 1 * 1000 * HZ/1000);// 2 *1000 = 1s
		//wait all send to asic
		/*
		cnt = 0;
		do
		{
			interruptible_sleep_on_timeout(&timeout_wq, 50 * HZ/1000);// 50 = ms
			nonce_query(dev);
			if( cnt++ > 100) //100 * 200ms = 20s;
			{
				printk(KERN_ERR "open core timeout2 g_FPGA_FIFO_SPACE{%d}\n", g_FPGA_FIFO_SPACE);
				return;
				break;
			}
		}while(g_FPGA_FIFO_SPACE < (g_TOTAL_FPGA_FIFO * 4/48 - 1));
		*/
		//if(!((k==55) && (j==54)))
		//	send_work_to_fpga(false, 0x80 | i, dev, &asic_work);
	}
	open_core_time = jiffies;
	/*
	for(i = 0; i < sizeof(asic_work.midstate); i++)
	{
		asic_work.midstate[i] = i%4;
	}
	for(i = 0; i < sizeof(asic_work.data); i++)
	{
		asic_work.data[i] = i%4;
	}


	for(k = 0; k < 15000; k++)
	{
		if(g_FPGA_FIFO_SPACE <= g_FPGA_RESERVE_FIFO_SPACE)
		{
			cnt = 0;
			do
			{
				interruptible_sleep_on_timeout(&timeout_wq, 50 * HZ/1000);// 50 = ms
				nonce_query(dev);
				if( cnt++ > 100) //100 * 50ms = 5s;
				{
					printk(KERN_ERR "open core timeout1 g_FPGA_FIFO_SPACE{%d}\n", g_FPGA_FIFO_SPACE);
					return;
					break;
				}
			}while(g_FPGA_FIFO_SPACE < (g_FPGA_RESERVE_FIFO_SPACE + 55));
		}
		send_work_to_fpga(false, chain_id, dev, &asic_work);
		g_FPGA_FIFO_SPACE--;
		printk("k = %d\n", k);
	}
	*/

	dev->wait_timeout = true;
	dev->asic_configure.timeout_data = save_timeout;
	//printk(KERN_ERR "FPGA start null work\n");
	//interruptible_sleep_on_timeout(&timeout_wq, 40 * 1000 * HZ/1000);
	#endif
	#endif
}
#else
void set_frequency(BT_AS_INFO dev, unsigned int freq)
{
    unsigned char cmd_buf[4] = {0};
	unsigned char i;
	wait_queue_head_t timeout_wq;
	init_waitqueue_head(&timeout_wq);
    //bitmain_asic_set_frequency(cmd_buf, 1, freq);
    cmd_buf[0] = 2;
    cmd_buf[1] = (freq)&0xff; //16-23
    cmd_buf[2] = (freq >> 8)&0xff; //8-15
    cmd_buf[0] |= 0x80;
    cmd_buf[3] = CRC5(cmd_buf, 4*8 - 5);
    printk(KERN_ERR "set_frequency cmd_buf[0]{%#x}cmd_buf[1]{%#x}cmd_buf[2]{%#x}cmd_buf[3]{%#x}\n",
		cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3]);
    for(i = 0; (i < sizeof(dev->chain_exist) * 8); i++)
    {
		if((dev->chain_exist) & (0x01 << i))
			send_BC_to_fpga(i, cmd_buf);
		interruptible_sleep_on_timeout(&timeout_wq, 5 * HZ/1000);//5ms
    }
}
#endif
void set_baud(BT_AS_INFO dev, unsigned char bauddiv)
{
	unsigned char cmd_buf[4] = {0};
	unsigned char i;
	wait_queue_head_t timeout_wq;
	init_waitqueue_head(&timeout_wq);
	if(dev->asic_configure.bauddiv == bauddiv)
	{
		 printk(KERN_ERR "baud same don't to change\n");
		 return;
	}
    //bitmain_asic_set_frequency(cmd_buf, 1, freq);
    cmd_buf[0] = 6;
    cmd_buf[1] = 0x10; //16-23
    cmd_buf[2] = bauddiv & 0x1f; //8-13
    cmd_buf[0] |= 0x80;
    cmd_buf[3] = CRC5(cmd_buf, 4*8 - 5);
    printk(KERN_ERR "set_baud cmd_buf[0]{%#x}cmd_buf[1]{%#x}cmd_buf[2]{%#x}cmd_buf[3]{%#x}\n",
		cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3]);
    for(i = 0; (i < sizeof(dev->chain_exist) * 8); i++)
    {
		if((dev->chain_exist) & (0x01 << i))
			send_BC_to_fpga(i, cmd_buf);
		interruptible_sleep_on_timeout(&timeout_wq, 5 * HZ/1000);//5ms
    }
	dev->asic_configure.bauddiv = bauddiv;
}

unsigned char get_baud(uint16_t frequency)
{
	uint32_t toctl;
	unsigned char i;
	toctl = 1000000;
	toctl /= (0xffffffff/64/64/frequency);
	for (i=0;i<32;i++)
	{
		if(toctl < (25000000/((i+1)*8)/1000) && toctl > (25000000/((i+2)*8)/1000) )
			return i;
	}
	return 4;
}

void bitmain_sw_addr(BT_AS_INFO dev)
{
	unsigned char cmd_buf[4] = {0};
	unsigned char i, j;
	wait_queue_head_t timeout_wq;
	init_waitqueue_head(&timeout_wq);
	unsigned char chip_addr = 0;
	unsigned char chip_interval = 2;
	unsigned char chain_nu;
    for(i = 0; i < dev->asic_status_data.chain_num; i++)
    {
		chip_addr = 0;
		chain_nu = dev->chain_map[i];
		printk(KERN_ERR "sw addr start\n");
		bitmain_asic_inactive(cmd_buf, chain_nu);
		interruptible_sleep_on_timeout(&timeout_wq, 5 * HZ/1000);//5ms
		if(gChain_Asic_Interval[chain_nu] !=0 )
			chip_interval = gChain_Asic_Interval[chain_nu];
		for(j = 0; j < 0x100/chip_interval; j++)
		{
			bitmain_asic_set_addr(cmd_buf, chain_nu, 0, chip_addr);
			chip_addr += chip_interval;
			interruptible_sleep_on_timeout(&timeout_wq, 5 * HZ/1000);//5ms
		}
    }
}

extern void rst_hash_asic(BT_AS_INFO dev)
{
	uint16_t rx_size;
	FPGA_QUERY	fpga_query;
	fpga_query.cmd_type = QEURY_FPGA;
	fpga_query.reserved1[0] = 0x55;
	fpga_query.reserved1[1] = 0xaa;
	#if defined S5 || defined S2 || defined S4_PLUS
	fpga_query.rst_time = 50;
	fpga_query.rst_valid = 1;
	printk(KERN_ERR "soft ctrl rst time\n");
	#else
	fpga_query.rst_time = 0x55;
	#endif

	//fpga_query.pwm_h = htons(12500); //25M=40ns  1Khz = 10^6ns  25000
	//fpga_query.pwm_l = htons(12500);
	fpga_query.pwm_h = htons(0x0b3f); //25M=40ns  1Khz = 10^6ns  25000
	fpga_query.pwm_l = htons(0x0fff);
	rx_size = sizeof(g_rx_data);
	spi_tranfer(0x01, (uint8_t*)&fpga_query, sizeof(fpga_query), g_rx_data, &rx_size);
	/*
	printk("query send data:");
	dump_hex((uint8_t*)&fpga_query,sizeof(fpga_query));
	*/
	//printk(KERN_ERR "query\n");
	printk(KERN_ERR "reset hash asic\n");
	return;

}

#define TEST_ASIC	0
int parse_return_nonce(BT_AS_INFO dev, uint8_t *rx_data, uint16_t rx_len)
{
	uint16_t i, j;
	int ret = 0;
	int nonce_num = 0;
	unsigned int fifa_have_work_num = 0;
	uint32_t task_buffer_match_adj = 0;
	FPGA_RETURN *fpga_ret_q = (FPGA_RETURN*)rx_data;
	FPGA_RET_NONCE *fpga_ret_nonce_q = (FPGA_RET_NONCE *)&rx_data[12];
	static FPGA_RET_NONCE last_nonce_full, llast_nonce_full;
	static uint32_t last_nonce = 0, llast_nonce = 0;
	static uint32_t last_nonce1_num = 0;

	static uint32_t chain_hw[16]={0};
	uint16_t chain_loop = 0;
	/*
	printk("return nonce data\n");
	dump_hex(rx_data, rx_len);
	*/
	if(rx_data[0] != 0x55)//\CE\DE\C1\AC\BD\D3
	{
		printk(KERN_ERR "FPGA return data error\n");
		return -1;
	}
	dev->fpga_version = fpga_ret_q->fpga_version;
	dev->pcb_version = fpga_ret_q->pc_version;
	dev->chain_exist = ntohs(fpga_ret_q->chain_exist);
	#if 0
	dev->fan_exist = fpga_ret_q->fan_exist;
	memcpy(dev->fan_speed, fpga_ret_q->fan_speed, sizeof(fpga_ret_q->fan_speed));
	#else
	if(fpga_ret_q->fan_index > CHAIN_SIZE)
	{
		printk(KERN_ERR "fan_index%d err\n", fpga_ret_q->fan_index);
	}
	else
	{
		dev->fan_exist |= 0x01<<fpga_ret_q->fan_index;
		//dev->fan_speed[fpga_ret_q->fan_index] = fpga_ret_q->fan_speed;
		//adjust fpga fan_speed err
		dev->fan_speed[fpga_ret_q->fan_index] = fpga_ret_q->fan_speed << 1;
	}
	//dev->fpga_fifo_st = (rx_data[8]<<8) | rx_data[9];
	#endif
	//dev->fpga_nonce1_num = ntohl(fpga_ret_q->nonce1_num);
	g_FPGA_FIFO_SPACE = (ntohs(fpga_ret_q->fifo_total) - ntohs(fpga_ret_q->have_bytes)) * 4/ 48;
	g_TOTAL_FPGA_FIFO = ntohs(fpga_ret_q->fifo_total);
	fifa_have_work_num = ntohs(fpga_ret_q->have_bytes)*4/48;
	//printk("have_bytes: %d\n", fpga_ret_q->have_bytes);
	//printk("chain exist: %x, g_FPGA_FIFO_SPACE %#x\n",dev->chain_exist, g_FPGA_FIFO_SPACE);
	//printk("total nonce num = %d, snd_to_fpga_work{%d}\n", ret_nonce_num, snd_to_fpga_work);

	if(fpga_ret_prnt)
	{
		printk("return_nonce:");
		dump_hex(rx_data,rx_len);
	}

	task_buffer_match_adj = dev->save_send_work;
	for(i = 0; i < fifa_have_work_num; i++)
		decrease_variable_rehead_U32(&task_buffer_match_adj, dev->task_buffer_size);
	for(j = 0; j < sizeof(fpga_ret_q->nonce)/sizeof(*fpga_ret_nonce_q); j++) //\B1\A3\B4\E6nonce \CA\FD??
	{
		uint8_t data;
		uint16_t work_id;
		uint32_t task_buffer_match = 0;
        uint8_t which_asic_nonce = 0;
		uint8_t which_array = fpga_ret_nonce_q->chain_num;

		//\B3\F6\B4\ED\B4\A6\C0\ED
		if(which_array > CHAIN_SIZE)
		{
			printk(KERN_ERR "Chain ret err\n");
			continue;
		}
		if(fpga_ret_nonce_q->temp_valid)
		{
			if((fpga_ret_nonce_q->temp <= 0xa0) && (fpga_ret_nonce_q->temp >= 00))
				dev->temp[fpga_ret_nonce_q->chain_num] = fpga_ret_nonce_q->temp;
		}
		if(fpga_ret_nonce_q->nonce_valid == true)
		{
			nonce_num++;
			if(dev->clear_fpga_fifo)
				continue;
			work_id = htons(fpga_ret_nonce_q->work_id);
			data = (work_id>>8) & 0xff;
			if (((work_id & 0x8000) == 0x8000) && (fpga_ret_nonce_q->nonce != last_nonce) && (fpga_ret_nonce_q->nonce != llast_nonce))//nonce && 非相同nonce
			{
				ret_nonce_num++;
				gNonce_num ++;
				//nonce_num++;

				if(asic_result_full == 1)
				{
					printk(KERN_ERR "No sp for ret nonce!!wr{%d}rd{%d}\n",asic_result_wr, asic_result_rd);
					break;
				}
				//printk(KERN_ERR "llast_nonce{%x}last_nonce{%x}\n", llast_nonce, last_nonce);
				llast_nonce = last_nonce;
				last_nonce = fpga_ret_nonce_q->nonce;
				rev((uint8_t*)&fpga_ret_nonce_q->nonce, 4);
				//asic_result[asic_result_wr].nonce = fpga_ret_nonce_q->nonce;
				task_buffer_match = work_id & 0x7fff;
				/*
				printk(KERN_ERR "task_buffer_match{%d}work_id{%d}task_buffer_rd{%d}wr{%d}\n", task_buffer_match, dev->task_buffer[task_buffer_match].work_id&0x1f,
				dev->task_buffer_rd, dev->task_buffer_wr);
				*/
				which_asic_nonce = (last_nonce & 0xff) >> (3 + 5 - gChain_Asic_Check_bit[which_array])& 0xff;
				if(gChain_Asic_Interval[which_array] !=0 )
					which_asic_nonce = (last_nonce & 0xff)/gChain_Asic_Interval[which_array] ;
				//which_asic_nonce = ((last_nonce & 0xff)>> (3)) & 0x1f;
				/*
				data = last_nonce & 0xff;
				if( data >= 0xe4)
					printk(KERN_ERR "nonce high byte %#x\n", data);
				*/
				//printk(KERN_ERR "which_array{%d}which_asic_nonce{%d}nonce[%#x]\n", which_array, which_asic_nonce, last_nonce & 0xff);
				gAsic_cnt[which_array][which_asic_nonce]++;
				Chain_nonce_nu[which_array]++;
				if ((dev->hw_error_eft == false) || /**/((ret = hashtest(&dev->task_buffer[task_buffer_match], fpga_ret_nonce_q->nonce)) != 0))
                		{
					if((dev->hw_error_eft == false) || ((dev->asic_configure.diff_sh_bit != 0 ) && (ret == 2)) || (dev->asic_configure.diff_sh_bit == 0))
					{
						asic_result[asic_result_wr].work_id= dev->task_buffer[task_buffer_match].work_id;
						asic_result[asic_result_wr].nonce = fpga_ret_nonce_q->nonce;
						increase_variable_rehead_U16(&asic_result_wr, ASIC_RESULT_NUM);
                        			if (asic_result_wr == asic_result_rd)
                        			{
                            				asic_result_full = 1;
                        			}
					}
					else
					{
						;//diff ==1
					}
				}
				else //if(is_started)
				{
					//if((gNonce_Err++ % 100) == 0)
					gNonce_Err++;
						printk(KERN_ERR "ch%d-as%d: dev->task_buffer_wr{0x%08x}rd{0x%08x}ret work_id{0x%04x} don't match task_buffer_match{0x%04x} \n",
						which_array, which_asic_nonce, dev->task_buffer_wr,dev->task_buffer_rd, work_id, task_buffer_match);
						if(which_array-1 < 16)
							chain_hw[which_array-1] ++;
				}
			}
            else if ((work_id & 0x8000) == 0x0000)//status
            {
                uint8_t crc_reslut;
				printk(KERN_ERR "asic cmd return %08x\n", fpga_ret_nonce_q->nonce);
                crc_reslut = CRC5((uint8_t*)&fpga_ret_nonce_q->nonce, 5 * 8 - 5);
                if (crc_reslut == (data & 0x1f))
                {
					rev((uint8_t*)&fpga_ret_nonce_q->nonce, 4);
                    asic_result_status[asic_result_status_wr] = fpga_ret_nonce_q->nonce;
                    if (dev->asic_configure.reg_address == 4) //PLL parameter
                    {
                        printk("Chain%d PLL: {%#x}\n", which_array, asic_result_status[asic_result_status_wr]);
                    }
                    if (dev->get_status == true)
                    {
                        increase_variable_rehead_U16(&asic_result_status_wr, ASIC_RESULT_STATUS_NUM);
                        if (asic_result_status_wr == asic_result_status_rd)
                            asic_result_status_full = 1;
                    }
                }
                else
                {
					uint8_t *pdata;
					printk(KERN_ERR "chain%d reg crc_r{%#x}crc{%#x} Err ret{0x%08x}\n", which_array, crc_reslut, data, fpga_ret_nonce_q->nonce);
					gNonce_Err++;
					#if 1
					if(fpga_ret_nonce_q->chain_num == 14)
					{
						pdata = (uint8_t*)&last_nonce_full;
						printk(KERN_ERR "last nonce full\n");
						for(i = 0; i < sizeof(last_nonce_full); i++)
						{
							printk(KERN_ERR "0x%02x ", pdata[i]);
						}
						printk(KERN_ERR "\n");
						pdata = (uint8_t*)&llast_nonce_full;
						printk(KERN_ERR "llast nonce full\n");
						for(i = 0; i < sizeof(last_nonce_full); i++)
						{
							printk(KERN_ERR "0x%02x ", pdata[i]);
						}
						printk(KERN_ERR "\n");
					}
					#endif
                }
            }
			#if 1
			if(fpga_ret_nonce_q->chain_num == 14)
			{
				rev((uint8_t*)&fpga_ret_nonce_q->nonce, 4);
				memcpy(&llast_nonce_full, &last_nonce_full, sizeof(last_nonce_full));
				memcpy(&last_nonce_full, fpga_ret_nonce_q, sizeof(last_nonce_full));
			}
			#endif
		}
		else
		{
			for( i = 0; i< sizeof(*fpga_ret_nonce_q); i++)
			{
				if( *((uint8_t*)fpga_ret_nonce_q + i) != 0)
				{
					printk(KERN_ERR "Nonce invalid but all not zero\n");
					dump_hex((uint8_t*)fpga_ret_q,sizeof(*fpga_ret_q));
					break;
				}
			}
		}
		fpga_ret_nonce_q++;
	}
	/*
	if( nonce_num != (dev->fpga_nonce1_num - last_nonce1_num))
	{
		printk("nonce_num{%d}last{%#x}dev->fpga_nonce1_num{%#x}\n", nonce_num, last_nonce1_num, dev->fpga_nonce1_num);
		printk("return_nonce:");
		dump_hex(rx_data,rx_len);
	}
	last_nonce1_num = dev->fpga_nonce1_num;
	*/
	/*
	if((dev->fpga_nonce1_num != 0) && ((dev->fpga_nonce1_num - (uint32_t)(dev->total_nonce_num&0xffffffff)) > 150))
	{
		printk(KERN_ERR "fpga-nc{%d}drv-nc{%ld} = {%ld}\n",dev->fpga_nonce1_num, dev->total_nonce_num, dev->fpga_nonce1_num - (uint32_t)(dev->total_nonce_num &0xffffffff));
	}
	*/
	return nonce_num;
}
extern int nonce_query(BT_AS_INFO dev)
{
	uint16_t rx_size;
	uint32_t toctl = 0;
	static FPGA_QUERY	fpga_query;
	fpga_query.cmd_type = QEURY_FPGA;
	fpga_query.reserved1[0] = 0x55;
	fpga_query.reserved1[1] = 0xaa;

	//fpga_query.pwm_h = htons(12500); //25M=40ns  1Khz = 10^6ns  25000
	//fpga_query.pwm_l = htons(12500);
	//fpga_query.pwm_h = htons(0x0b3f); //25M=40ns  1Khz = 10^6ns  25000
	//fpga_query.pwm_l = htons(0x0fff);
	fpga_query.pwm_h = htons(dev->pwm_high_value);
	fpga_query.pwm_l = htons(dev->pwm_low_value);
	if(dev->timeout_valid)
	{
		//toctl = (dev->asic_configure.timeout_data * 1000000/ 40) - 1;
		toctl = (dev->asic_configure.timeout_data*1000*9/10)  - 1;//1us
		toctl += (0xffffffff/64/64/dev->asic_configure.frequency % 1000 *9 / 10);
		printk(KERN_ERR "timeout {%#x}\n", toctl);
		toctl |= (0x01<<31);
		fpga_query.toctl = htonl(toctl);
		printk(KERN_ERR "rev timeout {%#x}\n", fpga_query.toctl);
		/*
		uint16_t i;
		uint8_t *data = (uint8_t *)&fpga_query;
		for(i = 0; i < sizeof(fpga_query); i++)
		{
			if(0 == (i%16))
				printk(KERN_ERR "\n0x%04x: ", i);
			printk(KERN_ERR "0x%02x ", data[i]);
		}
		printk(KERN_ERR "\n");
		*/
	}
	else
		fpga_query.toctl = 0;
	fpga_query.tm = htonl((0x01 << dev->nonce_diff)-1);
	//printk(KERN_ERR "fpga_query.tm0x%08x ", fpga_query.tm);
	if(gChain_Asic_num[dev->chain_map[0]] != 0)
	{
		//fpga_query.hcn = htonl(0xffffffff/256 * (256/gChain_Asic_num[dev->chain_map[0]]));
		fpga_query.hcn = 0;
	}
	rx_size = sizeof(g_rx_data);
	spi_tranfer(0x01, (uint8_t*)&fpga_query, sizeof(fpga_query), g_rx_data, &rx_size);
	/*
	printk("query send data:");
	dump_hex((uint8_t*)&fpga_query,sizeof(fpga_query));
	*/
	//printk(KERN_ERR "query\n");
	return parse_return_nonce(dev, g_rx_data, rx_size);
}

extern const char g_midstate[], g_data[];
extern int send_work_to_fpga(bool new_block, unsigned char chain_id, BT_AS_INFO dev, ASIC_TASK_P asic_work)
{
	FPGA_WORK	fpga_work;
	static uint32_t TO_FPGA_ID = 0;
	static bool new_block_flg = false;
	uint16_t rx_size;
	int ret = -1;
	fpga_work.block_type = NOR_BLOCK;
	fpga_work.chain_id = chain_id;
	if((time_after(jiffies, open_core_time + dev->asic_configure.timeout_data * 32 * 64 * HZ/1000))
		&& ( dev->wait_timeout ))//40s
	{
		//printk("jiffies{%ld}, open{%ld},dev->wait_timeout{%d}\n",jiffies, open_core_time, dev->wait_timeout);
		dev->timeout_valid = true;
		nonce_query(dev);
		dev->timeout_valid = false;
		dev->wait_timeout = false;
	}
	if(new_block)
	{
		//iowrite32(0x01<<8, gpio2_vaddr + GPIO_SETDATAOUT);
		if(time_after(jiffies, open_core_time + dev->asic_configure.timeout_data * 32 * 64 * HZ/1000))//40s
			fpga_work.block_type = NEW_BLOCK;
		else
			printk("Sending open core work\n");
		printk("Send new block cmd\n");
		//iowrite32(0x01<<8, gpio2_vaddr + GPIO_CLEARDATAOUT);
		new_block_flg = true;
		TO_FPGA_ID = 0;
	}
	if(new_block_flg)
	{
		if(TO_FPGA_ID++ > 500)
		{
			new_block_flg = false;
			//iowrite32(0x01<<8, gpio2_vaddr + GPIO_CLEARDATAOUT);
		}
	}
	#if TEST_ASIC
	hex2bin(asic_work->midstate, g_midstate, sizeof(asic_work->midstate));
	hex2bin(asic_work->data, g_data, sizeof(asic_work->data));
	rev(asic_work->midstate, sizeof(fpga_work.midstate));
	rev(asic_work->data, sizeof(fpga_work.data));
	#endif
	memcpy(fpga_work.midstate, asic_work->midstate, sizeof(fpga_work.midstate));
	memcpy(fpga_work.data, asic_work->data, sizeof(fpga_work.data));
	rev(fpga_work.midstate, sizeof(fpga_work.midstate));
	rev(fpga_work.data, sizeof(fpga_work.data));
	#if TEST_ASIC
	asic_work->work_id = TO_FPGA_ID;
	TO_FPGA_ID +=2;
	#endif
	/*
	printk(KERN_ERR "rd{%d}work_id 0x%08x\n", dev->task_buffer_rd, asic_work->work_id);
	*/
	//printk("asic_work->midstate:");
	//dump_hex((uint8_t*)asic_work->midstate,sizeof(fpga_work.midstate));

	//fpga_work.work_id = htonl(asic_work->work_id & 0x1f);
	//fpga_work.work_id = htonl(dev->task_buffer_rd & 0x1f);
	fpga_work.work_id = htonl(dev->task_buffer_rd & 0x7fff);
	//fpga_work.work_id = 0x15 | (0x15 << 24);
	rx_size = sizeof(g_rx_data);
	/*
	printk("send data:");
	dump_hex((uint8_t*)&fpga_work,sizeof(fpga_work));
	*/


	//printk("send work\n");
	if(-1 != spi_tranfer(0x02, (uint8_t*)&fpga_work, sizeof(fpga_work), g_rx_data, &rx_size))
		ret = parse_return_nonce(dev, g_rx_data, rx_size);
	else
		ret = -1;
	return ret;
}
extern int send_BC_to_fpga(uint8_t chain_num, uint8_t *cmd)
{
	FPGA_ASIC_CMD asic_cmd;
	BT_AS_INFO dev = &bitmain_asic_dev;
	uint16_t rx_size = sizeof(g_rx_data);
	memset(&asic_cmd, 0x00, sizeof(asic_cmd));
	asic_cmd.cmd_type = ASIC_CMD;
	asic_cmd.chain_num = chain_num;
	asic_cmd.bauddiv = dev->asic_configure.bauddiv;
	memcpy(&asic_cmd.cmd, cmd, sizeof(asic_cmd.cmd));

	printk("send BC data:");
	dump_hex((uint8_t*)&asic_cmd,sizeof(asic_cmd));
	iowrite32(0x01<<6, gpio2_vaddr + GPIO_SETDATAOUT);
	spi_tranfer(0x00, (uint8_t*)&asic_cmd, sizeof(asic_cmd), g_rx_data, &rx_size);
	iowrite32(0x01<<6, gpio2_vaddr + GPIO_CLEARDATAOUT);
	return 0;
}

extern void clear_fpga_nonce_buffer(BT_AS_INFO dev)
{
	uint32_t cnt = 0;
	int ret;
	printk(KERN_ERR "clear FPGA nonce buffer\n");
	dev->clear_fpga_fifo = true;
	while(1)
	{
		if((ret = nonce_query(dev)) == 0)
		{
			nonce_query(dev);
			dev->fpga_ok = true;
			break;
		}
		else if(ret < 0)
		{
			printk(KERN_ERR "FPGA don't code\n");
			dev->fpga_ok = false;
			break;
		}
		if(cnt++ > 100)
		{
			printk(KERN_ERR "clear FPGA nonce buffer err\n");
			break;
		}
	}
	dev->clear_fpga_fifo = false;
	//dev->total_nonce_num = dev->fpga_nonce1_num;
	ret_nonce_num = 0;
	return;
}
void sort_array(unsigned char *a, unsigned int count)
{
	unsigned int i,j;
	unsigned char temp;
	for(i=1;i<count;i++)
	{
		temp=a[i];
		j=i-1;
		while(a[j]>temp && j>=0)
		{
			a[j+1]=a[j];
			j--;
		}
		if(j!=(i-1))
			a[j+1]=temp;
	}
}
extern void detect_chain_num(BT_AS_INFO dev)
{
    unsigned long last_jiffies;
	wait_queue_head_t timeout_wq;
    unsigned char asic_addr[256];
	unsigned char addr_pos = 0;
    uint32_t i = 0, j, n;
    uint8_t Asic_num;
	uint8_t chain_remap = 0;
    uint8_t one_cnt, detect_cnt = 0;
    uint8_t addr_interval;
	uint8_t actual_chip_num[CHAIN_SIZE] = {0};
	struct ASIC_TASK asic_work = {0};
	#if defined S2
	printk("S2 reset hash board\n");
	iowrite32(0x01<<7, gpio2_vaddr + GPIO_CLEARDATAOUT);
	init_waitqueue_head(&timeout_wq);
	interruptible_sleep_on_timeout(&timeout_wq, 1000 * HZ/1000);//300ms
	iowrite32(0x01<<7, gpio2_vaddr + GPIO_SETDATAOUT);
	iowrite32(0x01<<7, gpio2_vaddr + GPIO_CLEARDATAOUT);
	init_waitqueue_head(&timeout_wq);
	interruptible_sleep_on_timeout(&timeout_wq, 1000 * HZ/1000);//300ms
	iowrite32(0x01<<7, gpio2_vaddr + GPIO_SETDATAOUT);
	clear_fpga_nonce_buffer(dev);
	#else
	//clear fpga work fifo
	rst_hash_asic(dev);
	//send_work_to_fpga(true, dev, &asic_task);
	clear_fpga_nonce_buffer(dev);
	init_waitqueue_head(&timeout_wq);
	interruptible_sleep_on_timeout(&timeout_wq, 1000 * HZ/1000);//300ms
	#if 1
	rst_hash_asic(dev);
	//send_work_to_fpga(true, dev, &asic_work);
	clear_fpga_nonce_buffer(dev);
	init_waitqueue_head(&timeout_wq);
	interruptible_sleep_on_timeout(&timeout_wq, 1100 * HZ/1000);//300ms
	#endif
	#endif
	//init_waitqueue_head(&timeout_wq);
	//clear_fpga_nonce_buffer(dev);
	//interruptible_sleep_on_timeout(&timeout_wq, 10000 * HZ/1000);//3000ms
start_dect:
	chain_remap = 0;
	for (i = 0; i < CHAIN_SIZE; i++)
	{
		for(j = 0; j < sizeof(asic_addr); j++)
			asic_addr[j] = 0;
		if((dev->chain_exist & (0x01 << i)) == 0)
			continue;
		//clear_fpga_nonce_buffer(dev);
	    bitmain_asic_get_status(NULL, i, 1, 0, 0); //CHIP_ADDR_REG 0
	    last_jiffies = jiffies + 100*HZ/1000;//100ms detect
	    asic_result_status_wr = asic_result_status_rd = 0;
		dev->get_status = true;
		/*
	    while(1)
		{
			if(time_after(jiffies, last_jiffies))
				break;
			if(nonce_query(dev) != 0)
				break;
	    }*/
		interruptible_sleep_on_timeout(&timeout_wq, 100 * HZ/1000);//500ms
		nonce_query(dev);
		addr_pos = 0;
		/*
		printk(KERN_ERR "wait rev\n");
		while(1)
		{
			while(asic_result_status_wr != asic_result_status_rd)//此链有芯片
			{
				while(asic_result_status_wr != asic_result_status_rd)
					asic_addr[addr_pos++] = asic_result_status[asic_result_status_rd++] & 0xff;
				asic_result_status_wr = asic_result_status_rd = 0;
				nonce_query(dev);
			}
			if ( 0 != interruptible_sleep_on_timeout(&timeout_wq, 100 * HZ/1000))//500ms
				break;
			nonce_query(dev);
		}
		*/
		while(asic_result_status_wr != asic_result_status_rd)//此链有芯片
		{
			while(asic_result_status_wr != asic_result_status_rd)
				asic_addr[addr_pos++] = asic_result_status[asic_result_status_rd++] & 0xff;
			asic_result_status_wr = asic_result_status_rd = 0;
			nonce_query(dev);
		}
		printk(KERN_ERR "chain%d total asic:%d\n", i, addr_pos);
		gChain_Asic_num[i] = addr_pos;
		actual_chip_num[i] = addr_pos;
		//remap chain from chain_exist
        if(dev->chain_exist&(0x01 << i))
			dev->chain_map[chain_remap++] = i;
		//调整2^n个芯片
calculate_check_bit:
		j = 0;
        one_cnt = 0;
        Asic_num = gChain_Asic_num[i];
        while (1)
        {
            if (Asic_num != 0)
            {
                j++;
                if (Asic_num & 0x01)
                    one_cnt++;
                Asic_num >>= 1;
            }
            else
                break;
        }
        gChain_Asic_Check_bit[i] = j;
        if (one_cnt == 1)
            gChain_Asic_Check_bit[i] -= 1;
        //printk(KERN_ERR "gChain%d_Asic_Check_bit = %d\n", i, gChain_Asic_Check_bit[i]);
        for(j = 0; j< 8; j++)
		{
			gChain_Asic_status[i][j] = 0;
			//dev->asic_status_data.chain_asic_exist[dev->chain_map[chain_remap-1]][j] = 0;
			dev->chain_asic_exist[i][j] = 0;
		}
		if(gChain_Asic_num[i] != 0)
        	gChain_Asic_num[i] = 0x01 << gChain_Asic_Check_bit[i];
		else
			continue;
        addr_interval = 0x100 / gChain_Asic_num[i];
		gChain_Asic_Interval[i] = addr_interval;
		sort_array(asic_addr, addr_pos);
		n = 0;
        for (j = 0; j < addr_pos; j++)
        {
			if((j >= 1) && ((asic_addr[j] - asic_addr[j-1]) < addr_interval) && (asic_addr[j] != asic_addr[j-1]))
			{
				addr_interval = asic_addr[j] - asic_addr[j-1];
				gChain_Asic_num[i] = 0x100/addr_interval;
				printk("Chain %d modify exist Asic_num[%d]\n", i, gChain_Asic_num[i]);
				goto calculate_check_bit;
			}
			if(actual_chip_num[i] != 0)
			{
				if((j>=1) && ((asic_addr[j] - asic_addr[j-1]) == 0x100/actual_chip_num[i]))
					addr_interval = 0x100/actual_chip_num[i];
			}
			if(asic_addr[j] != asic_addr[0])
			{
				while ((n * addr_interval) != asic_addr[j])
	            {
	                gChain_Asic_status[i][n/32] &= ~(0x1 << n%32);
					//dev->asic_status_data.chain_asic_exist[i][j/32] &= ~(0x1 << n%32);
					dev->chain_asic_exist[i][n/32] &= ~(0x1 << n%32);
	                if (++n >= gChain_Asic_num[i])
	                    break;
	            }
			}
            gChain_Asic_status[i][n/32] |= (0x1 << n%32);
			dev->chain_asic_exist[i][n/32] |= (0x1 << n%32);
            printk(KERN_ERR "pos%d--addr:%02x\n", j, asic_addr[j]);
            n++;
        }
		printk(KERN_ERR "chain%d_asic_exist{0x%08x}\n", i, dev->chain_asic_exist[i][0]);
	}

	dev->asic_configure.chain_num = chain_remap;
	dev->asic_status_data.chain_num = dev->asic_configure.chain_num;
	gTotal_asic_num = 0;
	for (i = 0; i < CHAIN_SIZE; i++)
    {
        gTotal_asic_num += gChain_Asic_num[i];
    }
	printk(KERN_ERR "total chain_num{%d}\n", dev->asic_status_data.chain_num);
	g_FPGA_RESERVE_FIFO_SPACE = dev->asic_status_data.chain_num * 2;
	if(gTotal_asic_num == 0)
	{
		printk(KERN_ERR "FPGA detect asic addr err\n\n");
		if(detect_cnt ++ < 3)
		{
			printk(KERN_ERR "\n\n!!!!Restart%d detect asic addr!!!\n\n", detect_cnt);
			goto start_dect;
		}

		for(i = 0; i < dev->asic_status_data.chain_num; i++)
		{
			//dev->chain_map[i] = i;
			#ifdef S4_Board
			if(gChain_Asic_num[dev->chain_map[i]] == 0)
				gChain_Asic_num[dev->chain_map[i]] = 40;
			#else
				#ifdef C1_Board
				if(gChain_Asic_num[dev->chain_map[i]] == 0)
					gChain_Asic_num[dev->chain_map[i]] = 16;
				#else
					#ifdef S5
					printk(KERN_ERR "S5 board\n");
					if(gChain_Asic_num[dev->chain_map[i]] == 0)
						gChain_Asic_num[dev->chain_map[i]] = 48;
					#endif
				#endif
			#endif
			dev->chain_asic_exist[dev->chain_map[i]][0] = 0xffffffff;
			dev->chain_asic_exist[dev->chain_map[i]][1] = 0xffffffff;
			gChain_Asic_status[dev->chain_map[i]][0] = 0xffffffff;
			gChain_Asic_status[dev->chain_map[i]][1] = 0xffffffff;
			gChain_Asic_Interval[dev->chain_map[i]] = 0x100/gChain_Asic_num[dev->chain_map[i]];
		}
		printk(KERN_ERR "\n\n!!!!soft set defult asic num %d!!!\n\n", gChain_Asic_num[0]);
		gTotal_asic_num = gChain_Asic_num[0] * dev->asic_status_data.chain_num;
		dev->temp_num = dev->asic_status_data.chain_num;
		dev->fan_num = 6;//CHAIN_SIZE;
		for(i = 0; i < dev->fan_num; i++)
		{
			dev->fan_map[i] = i;
		}
	}
	else
	{
		for(i = 0; i < dev->asic_status_data.chain_num; i++)
		{
			printk(KERN_ERR "chain%d chain_map %d\n", i, dev->chain_map[i]);
			printk(KERN_ERR "chain%d asic_num--%d actual--%d\n", i, gChain_Asic_num[dev->chain_map[i]], actual_chip_num[dev->chain_map[i]]);
			gChain_Asic_num[dev->chain_map[i]] = actual_chip_num[dev->chain_map[i]];
			#ifdef S4_Board
			if(gChain_Asic_num[dev->chain_map[i]] == 0)
				gChain_Asic_num[dev->chain_map[i]] = 40;
			#else
				#ifdef C1_Board
				if(gChain_Asic_num[dev->chain_map[i]] == 0)
					gChain_Asic_num[dev->chain_map[i]] = 16;
				#else
					#ifdef S5
					if(gChain_Asic_num[dev->chain_map[i]] == 0)
						gChain_Asic_num[dev->chain_map[i]] = 30;
					#endif
				#endif
			#endif
			if(gChain_Asic_num[dev->chain_map[i]] !=0 )
				gChain_Asic_Interval[dev->chain_map[i]] = 0x100/gChain_Asic_num[dev->chain_map[i]];
			printk(KERN_ERR "chain%d sw addr interval %d\n", i, gChain_Asic_Interval[dev->chain_map[i]]);
			if(gChain_Asic_num[dev->chain_map[i]] == 0)
			{
				printk(KERN_ERR "chain%d actual 0 addr Adjust\n", i);
				if(i != 0)
				{
					gChain_Asic_num[dev->chain_map[i]] = gChain_Asic_num[dev->chain_map[i-1]];
					gChain_Asic_Check_bit[dev->chain_map[i]] = gChain_Asic_Check_bit[dev->chain_map[i-1]];
				}
				else
				{
					gChain_Asic_num[dev->chain_map[i]] = gChain_Asic_num[dev->chain_map[i+1]];
					gChain_Asic_Check_bit[dev->chain_map[i]] = gChain_Asic_Check_bit[dev->chain_map[i+1]];
				}
			}
		}
		dev->temp_num = dev->asic_status_data.chain_num;

		#if 0
		dev->fan_num = 0;
		for(i = 0; i < sizeof(dev->fan_exist); i++)
		{
			if(dev->fan_exist & (0x01 << i))
			{
				dev->fan_map[dev->fan_num] = i;
				dev->fan_num++;
			}
		}
		#endif
		dev->fan_num = 6;//CHAIN_SIZE;
		for(i = 0; i < dev->fan_num; i++)
		{
			dev->fan_map[i] = i;
		}
		//dev->fan_num = 0;
	}
	#if 0
	bitmain_sw_addr(dev);
	iowrite32(0x01<<8, gpio2_vaddr + GPIO_SETDATAOUT);
	clear_fpga_nonce_buffer(dev);
	bitmain_asic_get_status(NULL, 0, 1, 0, 0); //CHIP_ADDR_REG 0
	interruptible_sleep_on_timeout(&timeout_wq, 100 * HZ/1000);//500ms
	asic_result_status_wr = asic_result_status_rd = 0;
	nonce_query(dev);
	while(asic_result_status_wr != asic_result_status_rd)//此链有芯片
	{
		asic_result_status_wr = asic_result_status_rd = 0;
		nonce_query(dev);
	}
	iowrite32(0x01<<8, gpio2_vaddr + GPIO_CLEARDATAOUT);
	#endif
	dev->get_status = false;
}

void sw_addr(BT_AS_INFO dev)
{
	uint32_t i;
	wait_queue_head_t timeout_wq;
	init_waitqueue_head(&timeout_wq);
	struct ASIC_TASK asic_work = {0};
	#ifndef S5_S_VL
	bitmain_sw_addr(dev);
	#endif
	iowrite32(0x01<<8, gpio2_vaddr + GPIO_SETDATAOUT);
	clear_fpga_nonce_buffer(dev);
	dev->get_status = true;
	bitmain_asic_get_status(NULL, dev->chain_map[0], 1, 0, 0); //CHIP_ADDR_REG 0
	interruptible_sleep_on_timeout(&timeout_wq, 100 * HZ/1000);//500ms
	asic_result_status_wr = asic_result_status_rd = 0;
	nonce_query(dev);
	while(asic_result_status_wr != asic_result_status_rd)//此链有芯片
	{
		asic_result_status_wr = asic_result_status_rd = 0;
		nonce_query(dev);
	}
	iowrite32(0x01<<8, gpio2_vaddr + GPIO_CLEARDATAOUT);
	dev->get_status = false;
	#ifndef S5_S_VL
	for(i = 0; i < dev->asic_status_data.chain_num; i++)
	{
		memset(asic_work.midstate, 0x00, sizeof(asic_work.midstate));
		memset(asic_work.data, 0x00, sizeof(asic_work.data));
		send_work_to_fpga(false, 0xc0|dev->chain_map[i], dev, &asic_work);
	}
	#endif
	return;
}
