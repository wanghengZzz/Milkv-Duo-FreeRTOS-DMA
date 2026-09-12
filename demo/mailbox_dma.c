#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

enum SYSTEM_CMD_TYPE {
	CMDQU_SEND = 1,
	CMDQU_SEND_WAIT,
	CMDQU_SEND_WAKEUP,
};
#define RTOS_CMDQU_DEV_NAME "/dev/cvi-rtos-cmdqu"
#define RTOS_CMDQU_SEND                         _IOW('r', CMDQU_SEND, unsigned long)
#define RTOS_CMDQU_SEND_WAIT                    _IOW('r', CMDQU_SEND_WAIT, unsigned long)
#define RTOS_CMDQU_SEND_WAKEUP                  _IOW('r', CMDQU_SEND_WAKEUP, unsigned long)

enum SYS_CMD_ID {
    CMD_TEST_A  = 0x10,
    CMD_TEST_B,
    CMD_TEST_C,
    CMD_DUO_LED,
    SYS_CMD_INFO_LIMIT,
    CMD_TEST_DMA
};

enum DUO_LED_STATUS {
	DUO_LED_ON	= 0x02,
	DUO_LED_OFF,
    DUO_LED_DONE,
};

struct valid_t {
	unsigned char linux_valid;
	unsigned char rtos_valid;
} __attribute__((packed));

typedef union resv_t {
	struct valid_t valid;
	unsigned short mstime; // 0 : noblock, -1 : block infinite
} resv_t;

typedef struct cmdqu_t cmdqu_t;
/* cmdqu size should be 8 bytes because of mailbox buffer size */
struct cmdqu_t {
	unsigned char ip_id;
	unsigned char cmd_id : 7;
	unsigned char block : 1;
	union resv_t resv;
	unsigned int  param_ptr;
} __attribute__((packed)) __attribute__((aligned(0x8)));

enum DMA_OPTION {

	DMA_MEM_MEM_POLLING_BA,
	DMA_MEM_MEM_INT_BA,
	DMA_DEV_DEV_POLLING_BA,
	DMA_DEV_DEV_INT_BA,
	DMA_MEM_DEV_POLLING_BA,
	DMA_MEM_DEV_INT_BA,
	DMA_DEV_MEM_POLLING_BA,
	DMA_DEV_MEM_INT_BA,

	DMA_MEM_MEM_POLLING_LIST,
	DMA_MEM_MEM_INT_LIST,
	DMA_DEV_DEV_POLLING_LIST,
	DMA_DEV_DEV_INT_LIST,
	DMA_MEM_DEV_POLLING_LIST,
	DMA_MEM_DEV_INT_LIST,
	DMA_DEV_MEM_POLLING_LIST,
	DMA_DEV_MEM_INT_LIST,
};

typedef struct dma_params dma_params;
struct dma_params {
    enum DMA_OPTION opt : 8;
    unsigned char burst;
    unsigned char width;
    unsigned char ch;
} __attribute__((packed));

struct test_str {
    int a;
    int b;
};

int main()
{
    int ret = 0;
    int fd = open(RTOS_CMDQU_DEV_NAME, O_RDWR);

    if(fd <= 0)
    {
        printf("open failed! fd = %d\n", fd);
        return 0;
    }

    struct cmdqu_t cmd = {0};

    dma_params params;
    cmd.ip_id = 0;
    cmd.resv.mstime = 100;
    cmd.cmd_id = CMD_TEST_DMA;
    cmd.param_ptr = *(unsigned int *)&params;
    
    params.ch = 2;
    params.opt = DMA_MEM_MEM_INT_BA;
    params.burst = 0x2;
    params.width = 0x2;

    printf("%x\n", params);
    ret = ioctl(fd , RTOS_CMDQU_SEND_WAIT, &cmd);

    if(ret < 0)
    {
        printf("ioctl error!\n");
        close(fd);
    }
    sleep(1);
    printf("C906B CMD TEST DMA: cmd.param_ptr = 0x%x\n", cmd.param_ptr);

    close(fd);

    return 0;
}