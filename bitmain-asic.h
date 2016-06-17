#ifndef __BITMAIN_ASIC_H__
#define __BITMAIN_ASIC_H__

#define HAVE_NUM		0
#define PACKED __attribute__( ( packed, aligned(4) ) )

#define CHAIN_SIZE		16

#define	BM_TX_CONF		0x51
#define	BM_TX_TASK		0x52
#define	BM_GET_STATUS	0x53
#define	BM_STATUS_DATA	0xa1
#define	BM_RX_NONCE		0xa2

#define	CNF_REST	(0x01<<0)

#define	CNF_FAN		(0x01<<1)
#define	CNF_TIMEOUT	(0x01<<2)
#define	CNF_FREQUENCY		(0x01<<3)
#define	CNF_VOLTAGE		(0x01<<4)
#define	CNF_CCHECKT		(0x01<<5)
#define	CNF_CHIP_CNF		(0x01<<6)

struct BITMAIN_CONFIGURE {
    uint8_t token_type;
	uint8_t version;
    uint16_t length;
    /*
    uint8_t	rccvftfr;
     */
    uint8_t reset : 1;
    uint8_t fan_eft : 1;
    uint8_t timeout_eft : 1;
    uint8_t frequency_eft : 1;
    uint8_t voltage_eft : 1;
    uint8_t chain_check_time_eft : 1;
    uint8_t chip_config_eft : 1;
    uint8_t hw_error_eft : 1;
	uint8_t beeper_ctrl : 1;
	uint8_t temp_over_ctrl : 1;
	uint8_t fan_ctrl_type : 1; //0: normal 1: home
	uint8_t reserved1 : 5;

	uint8_t chain_check_time;
	uint8_t reserved2;
    uint8_t chain_num;
    uint8_t asic_num;
    uint8_t fan_pwm_data;
    uint8_t timeout_data;

    uint16_t frequency;
 	uint16_t voltage;

    uint32_t reg_data;

    uint8_t chip_address;
    uint8_t reg_address;
    uint16_t crc;
} PACKED;

typedef struct BITMAIN_CONFIGURE*	BITMAIN_CONFIGURE_P;


struct ASIC_TASK
{
	uint32_t	work_id;
	uint8_t	midstate[32];
	uint8_t	data[12];
}PACKED;
typedef struct ASIC_TASK* ASIC_TASK_P;

#define NEW_BLK	0x01
struct BITMAIN_TASK {
	uint8_t token_type;
	uint8_t version;
	uint16_t length;
	uint8_t new_block            :1;
	uint8_t reserved            :7;
	uint8_t	diff;
	uint16_t net_diff;
	struct ASIC_TASK asic_task[64];
	uint16_t crc;
}PACKED;

#if 0
struct BITMAIN_TASK {
	uint8_t token_type;
	#if HAVE_NUM
	uint8_t length;
	uint8_t new_block            :1;
	uint8_t reserved1            :7;

	/*uint8_t	rnew_block;*/
	uint8_t	work_num;
	#else
	uint16_t length;
	uint8_t new_block            :1;
	uint8_t reserved1            :7;
	#endif
	struct ASIC_TASK asic_task[8];
	uint16_t	crc;
}PACKED;
#endif
typedef struct BITMAIN_TASK*	BITMAIN_TASK_P;

#define	DETECT_GET		0x02
#define 	GET_CHIP_ST	0x01
struct BITMAIN_GET_STATUS {
	uint8_t token_type;
	uint8_t version;
	uint16_t length;

	uint8_t chip_status_eft      :1;
	uint8_t detect_get		:1;
	uint8_t reserved1            :6;

	/*uint8_t rchipd_eft;*/
	uint8_t test_hash; //0xba
	uint8_t reserved[2];
	//uint32_t reg_data;
	uint8_t chip_address;
	uint8_t reg_addr;
	uint16_t crc;
}PACKED;
typedef struct BITMAIN_GET_STATUS*	BITMAIN_GET_STATUS_P;
#if 0
struct BITMAIN_STATUS_DATA_HEADER {
	uint8_t data_type;
	uint8_t length;
	/*
	uint8_t chip_reg_value_eft      :1;
	uint8_t reserved1            :7;
	*/
	uint8_t rchipregval_eft;
	uint8_t reserved;
	uint32_t reg_value;
	uint8_t core_num;
	uint8_t asic_num;
	uint8_t temp_sensor_num;
	uint8_t fan_num;
	uint32_t fifo_space;
	/*
	uint8_t temp[temp_sensor_num];
	uint8_t fan[fan_num];
	uint16_t crc;
	*/
}PACKED;
#endif
struct BITMAIN_STATUS_DATA {
    //struct BITMAIN_STATUS_DATA_HEADER status_header;
    uint8_t data_type;
	uint8_t version;
    uint16_t length;

    uint8_t chip_value_eft : 1;
    uint8_t reserved1 : 3;
	uint8_t get_blk_num : 4;
	uint8_t chain_num;
	uint16_t fifo_space;
    uint32_t hw_version;
	uint8_t fan_num;
    uint8_t temp_num;
	uint16_t fan_exist;
	uint32_t temp_exist;
    /*uint8_t rchipregval_eft;*/
    //uint8_t fifo_space;
	uint32_t nonce_err;
	uint32_t reg_value;
	uint32_t chain_asic_exist[CHAIN_SIZE][8];
    uint32_t chain_asic_status[CHAIN_SIZE][8];
    uint8_t chain_asic_num[CHAIN_SIZE];
    uint8_t temp[16];
    uint8_t fan[16];
    uint16_t crc;
} PACKED;

struct ASIC_RESULT{
	uint32_t	work_id;
	uint32_t	nonce;
};

struct BITMAIN_RESULT {
    uint8_t data_type;
	uint8_t version;
    uint16_t length;
	uint16_t fifo_space;
	uint16_t nonce_diff;
	uint64_t total_nonce_num;
    struct ASIC_RESULT nonce[128];
    uint16_t crc;
} PACKED;

struct ASIC_WORK
{
	uint8_t midstate[32];
	uint8_t pad[64-32-12];
	uint8_t data[12];
	uint32_t	nonce2;
}PACKED;
typedef struct ASIC_WORK*	ASIC_WORK_P;

#define be32toh		be32_to_cpu


typedef struct {
    uint8_t chain_num;
    uint8_t asic_num;
    uint8_t fan_pwm_data;
    uint8_t timeout_data;
	uint8_t diff_sh_bit;
	uint8_t bauddiv;
	uint16_t voltage;
    uint16_t frequency;
	uint16_t freq_vlaue;

    uint8_t chain_check_time;
    uint8_t chip_address;
    uint8_t reg_address;
	bool	beep_on_en;
	bool	snd_work_when_temp_h;
} ASIC_CONFIGURE;

typedef struct __BT_AS_info {
	int64_t diff1_num;
	uint64_t total_nonce_num;
	spinlock_t			lock;
	struct mutex		result_lock;
	struct mutex		to_work_lock;
	void __iomem		*virt_addr;
	unsigned			irq;
	void 				*beep_virtual_addr;
	void				*led_virtual;
	void				*led_virtual1;
	struct workqueue_struct *send_to_fpga_work_wq;
	struct work_struct send_to_fpga_work;
	struct delayed_work usb_rdata_work;
    ASIC_TASK_P task_buffer;
	uint32_t fifo_empt_cnt;
	uint16_t fpga_version;
	uint16_t pcb_version;
	uint16_t fpga_fifo_st;
	uint8_t fan_num;
	uint8_t temp_num;
	uint32_t chain_exist;
	uint32_t chain_asic_exist[CHAIN_SIZE][8];
	uint8_t chain_map[CHAIN_SIZE];

	uint8_t fan_exist;
	uint8_t fan_map[CHAIN_SIZE];
	uint8_t fan_speed[CHAIN_SIZE];

	uint8_t temp[CHAIN_SIZE];
	uint8_t temp_highest;
	uint8_t pwm_percent;
	uint32_t pwm_high_value;
	uint32_t pwm_low_value;
	uint32_t send_to_fpga_interval;
	unsigned long last_nonce_timeout;
	unsigned long cgminer_start_time;
	#if 0
	unsigned int task_lllast_num[CHAIN_SIZE];
    unsigned int task_llast_num[CHAIN_SIZE];
    unsigned int task_last_num[CHAIN_SIZE];
    unsigned int task_current_num[CHAIN_SIZE];
	#endif
	//unsigned int save_send_work[CHAIN_SIZE][SAVE_SEND_SIZE];
	unsigned int save_send_work;
    unsigned int task_buffer_size;
    unsigned int task_buffer_wr;
    unsigned int task_buffer_rd;
    bool task_buffer_full;
    bool fpga_ok;
    bool get_status;
    bool new_block;
	bool snding_work;
    bool cgminer_start;
	bool clear_fpga_fifo;
	bool hw_error_eft;
	bool timeout_valid;
	bool fan_ctrl_type; //0: nomarl 1: home
	bool temp_out_fool;
	bool temp_out_high;
	bool temp_out_ctrl;
	bool all_fan_stop;
	bool any_fan_fail;
	bool beep_ctrl;
	bool beep_status;
	bool wait_timeout;
	bool restarting_hash;
	uint16_t net_diff_sh_bit;
	uint8_t get_blk_num;
	uint16_t nonce_diff;
	uint32_t fpga_nonce1_num;
    ASIC_CONFIGURE asic_configure;
    struct BITMAIN_STATUS_DATA asic_status_data;
} *BT_AS_INFO;
#if SPI_USE_INTERRUPT
#define DRIVER_VER	0x02
#else
#define DRIVER_VER	0x01
#endif

/*****************************************
version2: using spi interrupt
version3: s3 nand boot 兼容S2 sd boot harward_version =001
******************************************/
#undef DRIVER_VER
#define DRIVER_VER 0x03
#define BM_1382

#define S4_Board
#define AISC_RT_DIFF	0x06 //diff = 2^6 = 64
//不定义时，由硬件自动判断
//sd start lcd无调整	0x7
//sd start lcd调整		0x0
//nand flash start 			0x01
//C1 53 54 green led 不兼容nand flash 0x02
//C1.1 兼容nand flash start 			0x01，焊接为0x06
#define FIX_HARDWARE_VER	0x1

#if defined C1_02
	#undef FIX_HARDWARE_VER
	#define FIX_HARDWARE_VER	0x2
	#undef S4_Board
	#define C1_Board
#elif defined S5 || defined S4_PLUS
	#undef S4_Board
	#define S5_S_VL
	#define CTL_ASIC_CORE
#endif

#if defined BM1384
	#define BMSC_CORE 55
#endif

#define BM1385 1

#if defined BM1385
	#define BMSC_CORE 50
#endif


//防止full，预留位置，存储已发送的上几个数据
//#define TASK_BUFFER_NUMBER	(64*CHAIN_SIZE)
#define TASK_BUFFER_NUMBER		(2*8192)

//#define CHECK_WORK_NUM		(32 * 3)
#define CHECK_WORK_NUM		(32 * 2)

//#define TASK_PRE_LEFT		(0x800 * 4 /48 + CHECK_WORK_NUM)
#define TASK_PRE_LEFT		(1024)

extern unsigned int gNonce_num, gNonce_Err, gNonce_miss, gDiff_nonce_num;
extern uint32_t gAsic_cnt[CHAIN_SIZE][256];
extern uint32_t Chain_nonce_nu[CHAIN_SIZE];

extern uint16_t gTotal_asic_num;
extern uint8_t gChain_Asic_Check_bit[CHAIN_SIZE];
extern uint8_t gChain_Asic_Interval[CHAIN_SIZE];

extern uint8_t gChain_Asic_num[CHAIN_SIZE];
extern uint32_t gChain_Asic_status[CHAIN_SIZE][256/32];

extern uint32_t ret_nonce_num;
extern uint32_t snd_to_fpga_work;
extern bool fpga_ret_prnt;

//GPIO2_2(Green) GPIO2_5(Red)
//#define GREEN	(0x01<<2)
//#define RED		(0x01<<5)

extern unsigned int GREEN, RED;
extern struct __BT_AS_info bitmain_asic_dev;
extern void *gpio0_vaddr;

extern int hashtest(ASIC_TASK_P asic_task, uint32_t nonce);
extern void dump_hex(uint8_t *data, uint16_t len);
void send_to_pfga_work(struct work_struct *work);
void init_beep(BT_AS_INFO dev);
void stop_work_to_all_chain(BT_AS_INFO dev);


static inline void decrease_variable_rehead_U32(uint32_t *num, uint32_t all_size)
{
	if(*num == 0)
		*num = all_size;
	*num = *num - 1;
    return;
}
static inline void increase_variable_rehead_U16(uint16_t *num, uint16_t all_size)
{
    *num = *num + 1;
    if (*num >= all_size)
        *num = 0;
    return;
}

static inline void increase_variable_rehead_U8(uint8_t *num, uint8_t all_size)
{
    *num = *num + 1;
    if (*num >= all_size)
        *num = 0;
    return;
}

#endif
