#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "hw_memmap.h"
#include "debug.h"
#include "gpio.h"
#include "hw_i2c.h"
#include "hw_types.h"
#include "i2c.h"
#include "pin_map.h"
#include "sysctl.h"
#include "systick.h"
#include "interrupt.h"
#include "uart.h"
#include "hw_ints.h"

#define SYSTICK_FREQUENCY		1000			//1000hz

//*****************************************************************************
// I2C GPIO chip address and resigster define
//*****************************************************************************
#define TCA6424_I2CADDR 					0x22
#define PCA9557_I2CADDR						0x18

#define PCA9557_INPUT							0x00
#define	PCA9557_OUTPUT						0x01
#define PCA9557_POLINVERT					0x02
#define PCA9557_CONFIG						0x03

#define TCA6424_CONFIG_PORT0			0x0c
#define TCA6424_CONFIG_PORT1			0x0d
#define TCA6424_CONFIG_PORT2			0x0e

#define TCA6424_INPUT_PORT0				0x00
#define TCA6424_INPUT_PORT1				0x01
#define TCA6424_INPUT_PORT2				0x02

#define TCA6424_OUTPUT_PORT0			0x04
#define TCA6424_OUTPUT_PORT1			0x05
#define TCA6424_OUTPUT_PORT2			0x06

// 串口与指令配置
#define UART_RX_BUF_SIZE 64
#define MAX_CMD_LEN 32
#define HIST_SIZE 10

void 		Delay(uint32_t value);
void 		S800_GPIO_Init(void);
uint8_t 	I2C0_WriteByte(uint8_t DevAddr, uint8_t RegAddr, uint8_t WriteData);
uint8_t 	I2C0_ReadByte(uint8_t DevAddr, uint8_t RegAddr);
void		S800_I2C0_Init(void);
void 		S800_UART_Init(void);
uint8_t     GetSegCode(char c);
void 		ParseAndExecuteCmd(void);
void 		UARTSendString(const char *str);
uint16_t 	CRC16_Calc(uint8_t *buf, uint16_t len);
void 		AddToHistory(char *cmd);
void 		UpdateMaxPos(void);

// systick software counter define
volatile uint16_t systick_10ms_couter, systick_1ms_couter;
volatile uint8_t	systick_10ms_status, systick_1ms_status;

uint32_t ui32SysClock;

// 显示相关
const char boot_disp[] = "S524031910727CuiJuntong";
char display_buf[32] = "S524031910727CuiJuntong"; // 可被SET DISP修改的缓冲区
char original_disp[32] = "S524031910727CuiJuntong"; // MODE 1下恢复显示用
int str_len;
int max_pos = 0;

// 流水控制变量
int window_pos = 0;
int direction = 1;        
int speed_idx = 0;        
const int speed_delays_mode0[4] = {512, 128, 48, 16}; // MODE 0 四档速度(10ms倍数)
uint16_t current_flow_delay = 512; // 当前速度(10ms倍数)，受串口SET SPEED影响
uint32_t current_speed_ms = 5120;  // 当前速度(毫秒)，用于GET SPEED查询              /************************ */

// 系统模式
uint8_t system_mode = 0; // 0: 本地流水模式, 1: 串口控制模式

// 串口接收
volatile uint8_t uart_rx_buf[UART_RX_BUF_SIZE];
volatile uint8_t uart_rx_idx = 0;
volatile uint8_t cmd_ready = 0;
volatile uint8_t overflow_flag = 0;

// PF0 状态机 (0:空闲, 1:正常双闪-亮1, 2:双闪-灭1, 3:双闪-亮2, 4:双闪-灭2, 5:错误慢闪-亮, 6:错误慢闪-灭)
uint8_t pf0_state = 0;
uint8_t pf0_timer = 0;

// 历史记录 (选做2)
char cmd_history[HIST_SIZE][MAX_CMD_LEN];
int hist_head = 0;
int hist_tail = 0;
int hist_count = 0;
uint8_t is_viewing_history = 0;
int hist_view_idx = 0;
uint8_t sw1_pressed_last = 1;

// 0-9 段码
const uint8_t seg7_numeric[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
// A-Z 段码
const uint8_t seg7_alpha[] = {
    0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71, 
    0x3D, 0x76, 0x30, 0x1E, 0x75, 0x38, 
    0x37, 0x54, 0x3F, 0x73, 0x67, 0x50, 
    0x6D, 0x78, 0x3E, 0x3E, 0x3E, 0x76, 
    0x6E, 0x5B                          
};

uint8_t GetSegCode(char c) {
    if (c >= '0' && c <= '9') return seg7_numeric[c - '0'];
    if (c >= 'A' && c <= 'Z') return seg7_alpha[c - 'A'];
    if (c >= 'a' && c <= 'z') return seg7_alpha[c - 'a']; 
    if (c == '-') return 0x40;
    if (c == ' ') return 0x00;
    return 0x00;
}

void UARTSendString(const char *str) {
    while(*str) {
        UARTCharPut(UART0_BASE, *str++);
    }
}

int main(void)
{
	// C89规范：所有局部变量必须在函数最开头声明
	uint8_t scan_idx;
	uint8_t key1_last, key2_last;
	uint16_t flow_cnt;
	char ch;
	uint8_t seg;
	uint8_t bit_sel;
	uint8_t key1_curr, key2_curr;
	uint8_t led_pos;
	uint8_t result;
	int char_pos;
	int idx; // 提前声明历史记录索引变量

	// 变量初始化
	scan_idx = 0;
	key1_last = 1;
	key2_last = 1;
	flow_cnt = 0;

	ui32SysClock = SysCtlClockFreqSet((SYSCTL_XTAL_16MHZ |SYSCTL_OSC_INT | SYSCTL_USE_PLL |SYSCTL_CFG_VCO_480), 20000000);

	SysTickPeriodSet(ui32SysClock/SYSTICK_FREQUENCY);
	SysTickEnable();
	SysTickIntEnable();
	IntMasterEnable();		

	S800_GPIO_Init();
	S800_I2C0_Init();
	S800_UART_Init();
	
	str_len = strlen(display_buf);
	UpdateMaxPos();
	led_pos = 7 - (window_pos % 8);
	result = I2C0_WriteByte(PCA9557_I2CADDR,PCA9557_OUTPUT, ~(1 << led_pos));
	(void)result; 

	while (1)
	{
		// ---------------------------------------------------------
		// 串口指令处理
		// ---------------------------------------------------------
		if (cmd_ready) {
			ParseAndExecuteCmd();
			cmd_ready = 0;
			uart_rx_idx = 0;
		} else if (overflow_flag) {
			UARTSendString("ERROR: BUFFER OVERFLOW\r\n");
			pf0_state = 5; pf0_timer = 0; // 报警
			overflow_flag = 0;
			uart_rx_idx = 0;
		}

		// ---------------------------------------------------------
		// 任务1: 1ms执行一次，数码管动态扫描
		// ---------------------------------------------------------
		if (systick_1ms_status)
		{
			systick_1ms_status = 0;

			char_pos = window_pos + scan_idx;
			if (char_pos < str_len) {
				ch = display_buf[char_pos];
			} else {
				ch = ' ';
			}
			seg = GetSegCode(ch);
			bit_sel = (1 << scan_idx);

			I2C0_WriteByte(TCA6424_I2CADDR,TCA6424_OUTPUT_PORT2, 0x00);
			I2C0_WriteByte(TCA6424_I2CADDR,TCA6424_OUTPUT_PORT1, seg);
			I2C0_WriteByte(TCA6424_I2CADDR,TCA6424_OUTPUT_PORT2, bit_sel);

			scan_idx++;
			if (scan_idx >= 8) scan_idx = 0;
		}

		// ---------------------------------------------------------
		// 任务2: 10ms执行一次，按键、流水、状态灯
		// ---------------------------------------------------------
		if (systick_10ms_status)
		{
			systick_10ms_status = 0;

			// 1. 按键扫描
			key1_curr = GPIOPinRead(GPIO_PORTJ_BASE, GPIO_PIN_0) ? 1 : 0;
			key2_curr = GPIOPinRead(GPIO_PORTJ_BASE, GPIO_PIN_1) ? 1 : 0;

			if (system_mode == 0) {
				// MODE 0: SW1切换方向, SW2切换速度
				if (key1_last == 1 && key1_curr == 0) direction = -direction;
				if (key2_last == 1 && key2_curr == 0) {
					speed_idx++;
					if (speed_idx >= 4) speed_idx = 0;
					current_flow_delay = speed_delays_mode0[speed_idx];
					current_speed_ms = current_flow_delay * 10;
				}
			} else {
				// MODE 1: SW1作为历史记录追溯键 (选做2)
				if (key1_last == 1 && key1_curr == 0) {
					if (hist_count > 0) {
						is_viewing_history = 1;
						hist_view_idx++;
						if (hist_view_idx > hist_count) hist_view_idx = 1;
						idx = (hist_head - hist_view_idx + HIST_SIZE) % HIST_SIZE; // 修复C89错误
						strcpy(display_buf, cmd_history[idx]);
						str_len = strlen(display_buf);
						window_pos = 0;
						UpdateMaxPos();
						UARTSendString("HISTORY:");
						UARTSendString(cmd_history[idx]);
						UARTSendString("\r\n");
					}
				}
				if (key1_curr == 1 && sw1_pressed_last == 0) {
					if (is_viewing_history) {
						is_viewing_history = 0;
						strcpy(display_buf, original_disp);
						str_len = strlen(display_buf);
						window_pos = 0;
						UpdateMaxPos();
					}
				}
			}
			key1_last = key1_curr;
			sw1_pressed_last = key1_curr;
			key2_last = key2_curr;

			// 2. 流水位置推进 (仅在非查看历史时推进)
			if (!is_viewing_history) {
				flow_cnt++;
				if (flow_cnt >= current_flow_delay)
				{
					flow_cnt = 0;
					window_pos += direction;

					if (window_pos > max_pos)
					{
						window_pos = 0;
					}
					else if (window_pos < 0)
					{
						window_pos = max_pos;
					}

					led_pos = 7 - (window_pos % 8);
					I2C0_WriteByte(PCA9557_I2CADDR,PCA9557_OUTPUT, ~(1 << led_pos));
				}
			}

			// 3. PF0 状态机驱动
			pf0_timer++;
			switch(pf0_state) {
				case 1: GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_0, GPIO_PIN_0); if(pf0_timer >= 5) { pf0_state = 2; pf0_timer = 0; } break;
				case 2: GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_0, 0); if(pf0_timer >= 5) { pf0_state = 3; pf0_timer = 0; } break;
				case 3: GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_0, GPIO_PIN_0); if(pf0_timer >= 5) { pf0_state = 4; pf0_timer = 0; } break;
				case 4: GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_0, 0); if(pf0_timer >= 5) { pf0_state = 0; pf0_timer = 0; } break;
				case 5: GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_0, GPIO_PIN_0); if(pf0_timer >= 30) { pf0_state = 6; pf0_timer = 0; } break;
				case 6: GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_0, 0); if(pf0_timer >= 30) { pf0_state = 5; pf0_timer = 0; } break;
				default: GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_0, 0); break;
			}
		}
	}
}

// ================= 指令解析与执行 =================
void ParseAndExecuteCmd(void) {
	// C89规范：所有局部变量必须在函数最开头声明
	char cmd_body[64];
	uint16_t recv_crc;
	uint16_t calc_crc;
	char *crc_ptr;
	char *cmd_ptr;
	char *dot;           // 提前声明
	uint8_t mode;        // 提前声明
	uint32_t ms_val;     // 提前声明
	char buf[64];        // 提前声明
	uint8_t s_int;       // 提前声明
	uint8_t s_dec;       // 提前声明

	recv_crc = 0;
	calc_crc = 0;
	cmd_ptr = cmd_body;
	
	// 复制缓冲区到局部变量处理
	strcpy(cmd_body, (char*)uart_rx_buf);
	
	// 选做1: CRC校验检查
	crc_ptr = strchr(cmd_body, '^');
	if (crc_ptr != NULL) {
		*crc_ptr = '\0'; 
		recv_crc = (uint16_t)strtol(crc_ptr + 1, NULL, 10);
		calc_crc = CRC16_Calc((uint8_t*)cmd_body, strlen(cmd_body));
		if (recv_crc != calc_crc) {
			UARTSendString("ERROR: CHECKSUM\r\n");
			pf0_state = 5; pf0_timer = 0;
			return;
		}
	}

	// 开始解析指令主体
	if (strncmp(cmd_ptr, "SET MODE ", 9) == 0) {
		mode = cmd_ptr[9] - '0';
		if (mode == 0 || mode == 1) {
			system_mode = mode;
			if (system_mode == 0) {
				speed_idx = 0;
				current_flow_delay = speed_delays_mode0[speed_idx];
				current_speed_ms = current_flow_delay * 10;
				is_viewing_history = 0;
				hist_view_idx = 0;
				direction = 1;
				strcpy(display_buf, boot_disp);
				strcpy(original_disp, boot_disp);
				str_len = strlen(display_buf);
				window_pos = 0;
				UpdateMaxPos();
			}
			UARTSendString("OK\r\n");
			pf0_state = 1; pf0_timer = 0; 
			AddToHistory(cmd_body);
		} else {
			UARTSendString("ERROR: INVALID PARAM\r\n");
			pf0_state = 5; pf0_timer = 0;
		}
	} 
	else if (strncmp(cmd_ptr, "SET DISP ", 9) == 0) {
		if (system_mode != 1) {
			UARTSendString("ERROR: NOT IN UART MODE\r\n");
			pf0_state = 5; pf0_timer = 0;
			return;
		}
		if (strlen(cmd_ptr + 9) > 8) {
			UARTSendString("ERROR: INVALID PARAM\r\n");
			pf0_state = 5; pf0_timer = 0;
			return;
		}
		strcpy(display_buf, cmd_ptr + 9);
		strcpy(original_disp, display_buf); 
		str_len = strlen(display_buf);
		window_pos = 0;
		UpdateMaxPos();
		UARTSendString("OK\r\n");
		pf0_state = 1; pf0_timer = 0;
		AddToHistory(cmd_body);
	}
	else if (strncmp(cmd_ptr, "SET SPEED ", 10) == 0) {
		ms_val = 0;
		if (system_mode != 1) {
			UARTSendString("ERROR: NOT IN UART MODE\r\n");
			pf0_state = 5; pf0_timer = 0;
			return;
		}
		dot = strchr(cmd_ptr + 10, '.'); // 修复C89错误
		if (dot != NULL) {
			s_int = atoi(cmd_ptr + 10);
			s_dec = *(dot + 1) - '0';
			ms_val = s_int * 1000 + s_dec * 100;
		} else {
			ms_val = atoi(cmd_ptr + 10) * 1000;
		}
		
		if (ms_val >= 100 && ms_val <= 10000) {
			current_speed_ms = ms_val;
			current_flow_delay = ms_val / 10; 
			UARTSendString("OK\r\n");
			pf0_state = 1; pf0_timer = 0;
			AddToHistory(cmd_body);
		} else {
			UARTSendString("ERROR: INVALID PARAM\r\n");
			pf0_state = 5; pf0_timer = 0;
		}
	}
	else if (strcmp(cmd_ptr, "GET STATUS") == 0) {
		s_int = current_speed_ms / 1000;
		s_dec = (current_speed_ms % 1000) / 100;
		sprintf(buf, "STATUS:MODE=%d,SPEED=%d.%d\r\n", system_mode, s_int, s_dec);
		UARTSendString(buf);
		pf0_state = 1; pf0_timer = 0;
		AddToHistory(cmd_body);
	}
	else if (strcmp(cmd_ptr, "GET SPEED") == 0) {
		s_int = current_speed_ms / 1000;
		s_dec = (current_speed_ms % 1000) / 100;
		sprintf(buf, "SPEED:%d.%d\r\n", s_int, s_dec);
		UARTSendString(buf);
		pf0_state = 1; pf0_timer = 0;
		AddToHistory(cmd_body);
	}
	else {
		UARTSendString("ERROR: INVALID COMMAND\r\n");
		pf0_state = 5; pf0_timer = 0;
	}
}

// ================= 选做1: CRC16 计算 =================
uint16_t CRC16_Calc(uint8_t *buf, uint16_t len) {
	uint16_t crc = 0xFFFF;
	uint16_t i;
	uint8_t j;
	for(i = 0; i < len; i++) {
		crc ^= buf[i];
		for(j = 0; j < 8; j++) {
			if(crc & 1) {
				crc = (crc >> 1) ^ 0xA001;
			} else {
				crc >>= 1;
			}
		}
	}
	return crc;
}

// ================= 选做2: 历史记录 =================
void AddToHistory(char *cmd) {
	if (strlen(cmd) >= MAX_CMD_LEN) return;
	strcpy(cmd_history[hist_head], cmd);
	hist_head = (hist_head + 1) % HIST_SIZE;
	if (hist_count < HIST_SIZE) {
		hist_count++;
	} else {
		hist_tail = (hist_tail + 1) % HIST_SIZE; 
	}
}


void UpdateMaxPos(void)
{
	if (str_len > 8) {
		max_pos = str_len - 8;
	} else {
		max_pos = 0;
	}

	if (window_pos > max_pos) {
		window_pos = max_pos;
	} else if (window_pos < 0) {
		window_pos = 0;
	}
}

// ================= 底层驱动函数 =================
void Delay(uint32_t value)
{
	uint32_t ui32Loop;
	for(ui32Loop = 0; ui32Loop < value; ui32Loop++){};
}

void S800_GPIO_Init(void)
{
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
	while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF));
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOJ);
	while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOJ));
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPION);
	while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPION));
	
	GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_0);
	GPIOPinTypeGPIOOutput(GPIO_PORTN_BASE, GPIO_PIN_0);
	GPIOPinTypeGPIOInput(GPIO_PORTJ_BASE,GPIO_PIN_0 | GPIO_PIN_1);
	GPIOPadConfigSet(GPIO_PORTJ_BASE,GPIO_PIN_0 | GPIO_PIN_1,GPIO_STRENGTH_2MA,GPIO_PIN_TYPE_STD_WPU);
}

void S800_I2C0_Init(void)
{
	uint8_t result;
	SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
	GPIOPinConfigure(GPIO_PB2_I2C0SCL);
	GPIOPinConfigure(GPIO_PB3_I2C0SDA);
	GPIOPinTypeI2CSCL(GPIO_PORTB_BASE, GPIO_PIN_2);
	GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_3);

	I2CMasterInitExpClk(I2C0_BASE,ui32SysClock, true);
	I2CMasterEnable(I2C0_BASE);	

	result = I2C0_WriteByte(TCA6424_I2CADDR,TCA6424_CONFIG_PORT0,0x0ff);
	result = I2C0_WriteByte(TCA6424_I2CADDR,TCA6424_CONFIG_PORT1,0x0);
	result = I2C0_WriteByte(TCA6424_I2CADDR,TCA6424_CONFIG_PORT2,0x0);

	result = I2C0_WriteByte(PCA9557_I2CADDR,PCA9557_CONFIG,0x00);
	result = I2C0_WriteByte(PCA9557_I2CADDR,PCA9557_OUTPUT,0x0ff);
	(void)result; 
}

void S800_UART_Init(void)
{
	SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
	while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA));

	GPIOPinConfigure(GPIO_PA0_U0RX);
	GPIOPinConfigure(GPIO_PA1_U0TX);    			
	GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

	UARTConfigSetExpClk(UART0_BASE, ui32SysClock, 115200, (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE));
	UARTFIFOLevelSet(UART0_BASE, UART_FIFO_TX2_8, UART_FIFO_RX1_8);
	
	IntEnable(INT_UART0);
	UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);
}

uint8_t I2C0_WriteByte(uint8_t DevAddr, uint8_t RegAddr, uint8_t WriteData)
{
	uint8_t rop;
	while(I2CMasterBusy(I2C0_BASE)){};
	I2CMasterSlaveAddrSet(I2C0_BASE, DevAddr, false);
	I2CMasterDataPut(I2C0_BASE, RegAddr);
	I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_START);
	while(I2CMasterBusy(I2C0_BASE)){};
	rop = (uint8_t)I2CMasterErr(I2C0_BASE);

	I2CMasterDataPut(I2C0_BASE, WriteData);
	I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_FINISH);
	while(I2CMasterBusy(I2C0_BASE)){};

	rop = (uint8_t)I2CMasterErr(I2C0_BASE);
	return rop;
}

uint8_t I2C0_ReadByte(uint8_t DevAddr, uint8_t RegAddr)
{
	uint8_t value,rop;
	while(I2CMasterBusy(I2C0_BASE)){};	
	I2CMasterSlaveAddrSet(I2C0_BASE, DevAddr, false);
	I2CMasterDataPut(I2C0_BASE, RegAddr);
	I2CMasterControl(I2C0_BASE,I2C_MASTER_CMD_SINGLE_SEND);
	while(I2CMasterBusBusy(I2C0_BASE));
	rop = (uint8_t)I2CMasterErr(I2C0_BASE);
	Delay(1);
	
	I2CMasterSlaveAddrSet(I2C0_BASE, DevAddr, true);
	I2CMasterControl(I2C0_BASE,I2C_MASTER_CMD_SINGLE_RECEIVE);
	while(I2CMasterBusBusy(I2C0_BASE));
	value=I2CMasterDataGet(I2C0_BASE);
	Delay(1);
	
	(void)rop; 
	return value;
}

// ================= 中断服务函数 =================
void SysTick_Handler(void)
{
	systick_1ms_status = 1;

	if (systick_10ms_couter != 0)
		systick_10ms_couter--;
	else
	{
		systick_10ms_couter = SYSTICK_FREQUENCY/100;
		systick_10ms_status = 1;
	}
}

void UART0_Handler(void)
{
	uint32_t ui32Status;
	int32_t recv_char;

	ui32Status = UARTIntStatus(UART0_BASE, true);
	UARTIntClear(UART0_BASE, ui32Status);

	while(UARTCharsAvail(UART0_BASE)) {
		recv_char = UARTCharGetNonBlocking(UART0_BASE);
		
		if (recv_char == '#') {
			if (!overflow_flag) {
				uart_rx_buf[uart_rx_idx] = '\0';
				cmd_ready = 1;
			}
			uart_rx_idx = 0; 
		} else {
			if (!cmd_ready && !overflow_flag) {
				if (uart_rx_idx < UART_RX_BUF_SIZE - 1) {
					uart_rx_buf[uart_rx_idx++] = (uint8_t)recv_char;
				} else {
					overflow_flag = 1; 
				}
			}
		}
	}
}
