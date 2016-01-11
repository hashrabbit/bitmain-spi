#ifndef __FPGA_H__
#define __FPGA_H__
//To fpga type
#define NOR_BLOCK	0x01
#define NEW_BLOCK	0x11
#define ASIC_CMD	0x03
#define QEURY_FPGA	0x02

typedef struct {
    uint8_t block_type;
	uint8_t chain_id;
    uint8_t reserved1[2];
   	uint8_t midstate[32];
	uint32_t work_id;
	uint8_t data[12];
} FPGA_WORK;
typedef struct {
    uint8_t cmd_type;
    uint8_t chain_num;
   	uint8_t reserved1[1];
	/*
	115.200 115.740 26	216
	460.800 446.429 6	56
	921.600 1.041.666	2	24
	1.500.000	1.562.500	1	16
	*/
	uint8_t bauddiv; //5-3: res 4:0
	uint8_t cmd[4];
} FPGA_ASIC_CMD;

typedef struct {
    uint8_t cmd_type;
   	uint8_t reserved1[2];
	uint8_t rst_time : 7;
	uint8_t rst_valid : 1;
	uint16_t pwm_h;
	uint16_t pwm_l;
	uint32_t toctl;
	uint32_t tm; //12-15
	uint32_t hcn; //16-19
	uint32_t sno;//20-23
	uint8_t reserved2[48 - 8 -12];
} FPGA_QUERY;

#if 0
typedef struct {
	uint8_t type;
	uint8_t reserved0;
    uint8_t fpga_version;
   	uint8_t pc_version;
	uint8_t nonce[32]; // 4-35
	//uint32_t fifo_space;
	uint16_t fifo_total;
	uint16_t have_bytes;
	uint8_t  reserved1;
	uint8_t	 fan_exist;
	uint16_t chain_exist;
	uint8_t fan_speed[4];
	uint32_t nonce1_num;
} FPGA_RETURN;
#else
typedef struct {
	uint8_t type;
	uint8_t reserved0;
    uint8_t fpga_version;
   	uint8_t pc_version;
	uint16_t fifo_total;
	uint16_t have_bytes;
	uint8_t fan_index : 3;
	uint8_t reserved1 : 5;
	uint8_t fan_speed;
	uint16_t chain_exist;
	uint8_t nonce[40]; // 12-51
} FPGA_RETURN;

#endif
/*******************************
Byte0~3	Byte 4	Byte 5	Byte 6	Byte 7
nonce	Work count	temperature	Reserved	ChainNumber
ChainNumber的格式如下表所示：
Bit[7] :Nonce indicator:
0: invalid
1: valid
Bit[6]:Temperature indicator:
0: invalid
1: valid
Bit[5:4]	Reserved
Bit[3:0]	number
**********************************/
typedef struct {
	uint32_t nonce;
	uint16_t work_id;
	//uint8_t reserved;
	uint8_t temp;;
	//uint8_t chain_num;
	uint8_t chain_num : 4;
	uint8_t reserved1 : 2;
	uint8_t temp_valid: 1;
	uint8_t nonce_valid:1;
} FPGA_RET_NONCE;

extern uint16_t g_FPGA_FIFO_SPACE, g_TOTAL_FPGA_FIFO;
extern uint16_t g_FPGA_RESERVE_FIFO_SPACE;
extern bool is_started;
extern uint16_t asic_result_wr, asic_result_rd, asic_result_full;
extern uint16_t asic_result_status_wr, asic_result_status_rd, asic_result_status_full;
extern volatile struct ASIC_RESULT asic_result[512];
#define ASIC_RESULT_NUM		(sizeof(asic_result)/sizeof(asic_result[0]))
extern uint32_t asic_result_status[512];
#define ASIC_RESULT_STATUS_NUM		(sizeof(asic_result_status)/sizeof(asic_result_status[0]))

extern int nonce_query(BT_AS_INFO dev);
extern int send_work_to_fpga(bool new_block, unsigned char chain_id, BT_AS_INFO dev, ASIC_TASK_P asic_work);
extern int send_BC_to_fpga(uint8_t chain_num, uint8_t *cmd);
extern void clear_fpga_nonce_buffer(BT_AS_INFO dev);
extern void set_frequency(BT_AS_INFO dev, unsigned int freq);
extern void rev(unsigned char *s, size_t l);
extern void rst_hash_asic(BT_AS_INFO dev);
extern void detect_chain_num(BT_AS_INFO dev);
extern int bitmain_asic_get_status(char* buf, char chain, char mode, char chip_addr, char reg_addr);
extern void bitmain_set_voltage(BT_AS_INFO dev, unsigned short voltage);
extern void set_baud(BT_AS_INFO dev, unsigned char bauddiv);
extern unsigned char get_baud(uint16_t frequency);
extern void sw_addr(BT_AS_INFO dev);

#endif
