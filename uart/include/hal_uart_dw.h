#ifndef __HAL_UART_DW_HEADER__
#define __HAL_UART_DW_HEADER__
#include <stdint.h>
//#include "linux/types.h"

#define thr rbr
#define iir fcr
#define dll rbr
#define dlm ier

#ifdef RISCV_QEMU

struct dw_uart_regs {
	volatile uint8_t rbr;   /* 0x00 RBR / THR / DLL */
	volatile uint8_t ier;   /* 0x04 IER / DLH */
	volatile uint8_t fcr;   /* 0x08 FCR / IIR */
	volatile uint8_t lcr;   /* 0x0C Line Control Register */
	volatile uint8_t mcr;   /* 0x10 Modem Control Register */
	volatile uint8_t lsr;   /* 0x14 Line Status Register */
	volatile uint8_t msr;   /* 0x18 Modem Status Register */
	uint8_t reserved_1[0x20 - 0x1C];
	volatile uint8_t spr;   /* 0x20 Scratch Register */

};

#else

struct dw_uart_regs {
	volatile uint32_t rbr; 		   /* 0x00 RBR / THR / DLL */
	volatile uint32_t ier;   	   /* 0x04 IER / DLH */
	volatile uint32_t fcr;     	   /* 0x08 FCR / IIR */
	volatile uint32_t lcr;         /* 0x0C Line Control Register */
	volatile uint32_t mcr;         /* 0x10 Modem Control Register */
	volatile uint32_t lsr;         /* 0x14 Line Status Register */
	volatile uint32_t msr;         /* 0x18 Modem Status Register */

	uint32_t reserved_1[1];        /* 0x1C, brings next field to 0x20 */

	volatile uint32_t lpdl;        /* 0x20? */
	volatile uint32_t lpdh;        /* 0x24? */

	uint32_t reserved_2[2];

	volatile uint32_t srbr_sthr;   /* 0x30 Shadow RBR / STHR */

	uint32_t reserved_3[15];

	volatile uint32_t far;         /* 0x70 FIFO Access Register */
	volatile uint32_t tfr;         /* 0x74 Transmit FIFO Read */
	volatile uint32_t rfw;         /* 0x78 Receive FIFO Write */
	volatile uint32_t usr;         /* 0x7C UART Status Register */
	volatile uint32_t tfl;         /* 0x80 Transmit FIFO Level */
	volatile uint32_t rfl;         /* 0x84 Receive FIFO Level */
	volatile uint32_t srr;         /* 0x88 Software Reset Register */
	volatile uint32_t srts;        /* 0x8C Shadow Request to Send */
	volatile uint32_t sbcr;        /* 0x90 Shadow Break Control */
	volatile uint32_t sdmam;       /* 0x94 Shadow DMA Mode */
	volatile uint32_t sfe;         /* 0x98 Shadow FIFO Enable */
	volatile uint32_t srt;         /* 0x9C Shadow RCVR Trigger */
	volatile uint32_t stet;        /* 0xA0 Shadow TX Empty Trigger */
	volatile uint32_t htx;         /* 0xA4 Halt TX */
	volatile uint32_t dmasa;       /* 0xA8 DMA Software Acknowledge */
};

#endif

#define UART_LCR_WLS_MSK 0x03 /* character length select mask */
#define UART_LCR_WLS_5 0x00 /* 5 bit character length */
#define UART_LCR_WLS_6 0x01 /* 6 bit character length */
#define UART_LCR_WLS_7 0x02 /* 7 bit character length */
#define UART_LCR_WLS_8 0x03 /* 8 bit character length */
#define UART_LCR_STB 0x04 /* # stop Bits, off=1, on=1.5 or 2) */
#define UART_LCR_PEN 0x08 /* Parity eneble */
#define UART_LCR_EPS 0x10 /* Even Parity Select */
#define UART_LCR_STKP 0x20 /* Stick Parity */
#define UART_LCR_SBRK 0x40 /* Set Break */
#define UART_LCR_BKSE 0x80 /* Bank select enable */
#define UART_LCR_DLAB 0x80 /* Divisor latch access bit */

#define UART_MCR_DTR 0x01 /* DTR   */
#define UART_MCR_RTS 0x02 /* RTS   */

#define UART_LSR_THRE 0x20 /* Transmit-hold-register empty */
#define UART_LSR_DR 0x01 /* Receiver data ready */
#define UART_LSR_TEMT 0x40 /* Xmitter empty */

#define UART_FCR_FIFO_EN       0x01 /* FIFO enable */
#define UART_FCR_RXSR          0x02 /* Receiver FIFO soft reset */
#define UART_FCR_TXSR          0x04 /* Transmitter FIFO soft reset */
#define UART_FCR_DMA_MODE      0x08 /* DMA mode */

/* TX Empty trigger level [5:4] */
#define UART_FCR_TXTRIG_0      0x00 /* FIFO empty */
#define UART_FCR_TXTRIG_2      0x10 /* 2 characters in FIFO */
#define UART_FCR_TXTRIG_1_4    0x20 /* FIFO 1/4 full */
#define UART_FCR_TXTRIG_1_2    0x30 /* FIFO 1/2 full */

/* RX trigger level [7:6] */
#define UART_FCR_RXTRIG_1      0x00 /* 1 character in FIFO */
#define UART_FCR_RXTRIG_1_4    0x40 /* FIFO 1/4 full */
#define UART_FCR_RXTRIG_1_2    0x80 /* FIFO 1/2 full */
#define UART_FCR_RXTRIG_2LESS  0xC0 /* FIFO 2 less than full */

#define UART_MCRVAL (UART_MCR_DTR | UART_MCR_RTS) /* RTS/DTR */
#define UART_FCR_DEFVAL (UART_FCR_FIFO_EN | UART_FCR_RXSR | UART_FCR_TXSR)
#define UART_LCR_8N1 0x03

typedef enum DEV_UART device_uart;

enum DEV_UART{
	UART0,
	UART1,
	UART2,
	UART3,
};

void hal_uart_init(device_uart dev_uart, int baudrate, int uart_clock);
void hal_uart_putc(uint8_t ch);
int hal_uart_getc(void);
int hal_uart_tstc(void);

#endif // end of __HAL_UART_DW_HEADER__