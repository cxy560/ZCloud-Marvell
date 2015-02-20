/*
 *  Copyright (C) 2008-2014, Marvell International Ltd.
 *  All Rights Reserved.
 */

/* Sample Application demonstrating the use of Arrayent Cloud
 * This application communicates with Arrayent cloud once the device is
 * provisioned. Device can be provisioned using the psm CLIs as mentioned below.
 * After that, it periodically gets/updates (toggles) the state of board_led_1()
 * and board_led_2() from/to the Arrayent cloud.
 */
#include <wm_os.h>
#include <app_framework.h>
#include <wmtime.h>
#include <cli.h>
#include <wmstdio.h>
#include <board.h>
#include <wmtime.h>
#include <psm.h>
#include <mdev.h>

#include <zc_protocol_controller.h>
#include <zc_marvell_adpter.h>
#include <wm_net.h>
#include <wlan.h>


/* Thread handle */
static os_thread_t app_thread;
static os_thread_t uart_thread;

/* Buffer to be used as stack */
static os_thread_stack_define(app_stack, 8 * 1024);
static os_thread_stack_define(uart_thread_stack, 5 * 2048);

extern PTC_ProtocolCon  g_struProtocolController;

mdev_t *g_uartdev;
extern ZC_UartBuffer g_struUartBuffer;

extern int app_network_get_nw(struct wlan_network *network);

void uart_rx_cmd(os_thread_arg_t arg)
{
    int uart_rx_len = 0;
    int len = 0;
    int msg_len = 0;
    u32 u32FixHeaderLen; 
    ZC_MessageHead *pstruHead;
    u8  u8UartBuffer[ZC_MAGIC_LEN + 1024];
    u8  u8MagicHead[ZC_MAGIC_LEN] = {0x02,0x03,0x04,0x05};

    u32FixHeaderLen = ZC_MAGIC_LEN + sizeof(ZC_MessageHead);
    
    while (1) 
    {
        len = 0;
        msg_len = 0;
        uart_rx_len = 0;
        memset(u8UartBuffer, 0, 1024);
        

        while (len != u32FixHeaderLen) 
        {
            uart_rx_len = uart_drv_read(g_uartdev, u8UartBuffer
                            + len,
                            u32FixHeaderLen - len);
            len += uart_rx_len;
        }

        if (0 != memcmp(u8UartBuffer, u8MagicHead, 4))
        {
            continue;
        }

        pstruHead = (ZC_MessageHead *)(u8UartBuffer + ZC_MAGIC_LEN);
        msg_len = ZC_HTONS(pstruHead->Payloadlen);

        if (msg_len + sizeof(ZC_MessageHead) > 1024)
        {
            continue;
        }
        
        len = 0;
        uart_rx_len = 0;
        while (len != msg_len) 
        {
            uart_rx_len = uart_drv_read(g_uartdev, u8UartBuffer +
                            u32FixHeaderLen +
                            len, msg_len - len);
                            
            len += uart_rx_len;
        }
        
        ZC_RecvDataFromMoudle(u8UartBuffer + ZC_MAGIC_LEN, msg_len + sizeof(ZC_MessageHead));

    }
    os_thread_self_complete(NULL);

}


static void ablecloud_main(os_thread_arg_t data)
{
    int fd;
    u32 u32Timer = 0;

    HF_BcInit();

    while(1) 
    {
        fd = g_struProtocolController.struCloudConnection.u32Socket;

        HF_CloudRecvfunc();
        
        PCT_Run();
        
        if (PCT_STATE_DISCONNECT_CLOUD == g_struProtocolController.u8MainState)
        {
            close(fd);
            u32Timer = rand();
            u32Timer = (PCT_TIMER_INTERVAL_RECONNECT) * (u32Timer % 10 + 1);
            PCT_ReconnectCloud(&g_struProtocolController, u32Timer);
            g_struUartBuffer.u32Status = MSG_BUFFER_IDLE;
            g_struUartBuffer.u32RecvLen = 0;
        }
        else
        {
            MSG_SendDataToCloud((u8*)&g_struProtocolController.struCloudConnection);
        }
        ZC_SendBc();
    } 

	return;
}


/* This is the main event handler for this project. The application framework
 * calls this function in response to the various events in the system.
 */
int common_event_handler(int event, void *data)
{
	int ret;
	static bool is_cloud_started;
    struct wlan_ip_config struIpAddr;
	switch (event) {
	case AF_EVT_WLAN_INIT_DONE:
		ret = psm_cli_init();
		if (ret != WM_SUCCESS)
			wmprintf("Error: psm_cli_init failed\r\n");
		int i = (int) data;

		if (i != APP_NETWORK_PROVISIONED) {
		    wmprintf("start ezconfig\r\n");
            app_ezconnect_provisioning_start(NULL, 0);
		} 
		else
		{
        	app_sta_start();		    
		}
        HF_ReadDataFormFlash();
		break;
	case AF_EVT_NORMAL_CONNECTED:
        wlan_get_address(&struIpAddr);
        g_u32GloablIp = struIpAddr.ipv4.address;
        g_u32GloablIp = ZC_HTONL(g_u32GloablIp);
		if (!is_cloud_started) {
			ret = os_thread_create(&app_thread,  /* thread handle */
				"ablecloud",/* thread name */
				ablecloud_main,  /* entry function */
				0,          /* argument */
				&app_stack,     /* stack */
				OS_PRIO_3);     /* priority - medium low */
			is_cloud_started = true;
		}
		HF_WakeUp();
		break;
    case AF_EVT_PROV_DONE:
        app_ezconnect_provisioning_stop();
    case AF_EVT_NORMAL_LINK_LOST:
        HF_Sleep();
        HF_BcInit();
	default:
		break;
	}

	return 0;
}

static void modules_init()
{
	int ret;

	ret = wmstdio_init(UART0_ID, 0);
	if (ret != WM_SUCCESS) {
		wmprintf("Error: wmstdio_init failed\r\n");
	}

	ret = cli_init();
	if (ret != WM_SUCCESS) {
		wmprintf("Error: cli_init failed\r\n");
	}

	ret = pm_cli_init();
	if (ret != WM_SUCCESS) {
		wmprintf("Error: pm_cli_init failed\r\n");
	}
	/* Initialize time subsystem.
	 *
	 * Initializes time to 1/1/1970 epoch 0.
	 */
	ret = wmtime_init();
	if (ret != WM_SUCCESS) {
		wmprintf("Error: wmtime_init failed\r\n");
	}

	uart_drv_init(UART1_ID, UART_8BIT);
#if 0
	/* Enable DMA on UART1 */
	uart_drv_xfer_mode(UART1_ID, UART_DMA_ENABLE);

	/* Set DMA block size */
	uart_drv_dma_rd_blk_size(UART1_ID, 512);

	/* Set internal rx ringbuffer size to 3K */
	uart_drv_rxbuf_size(UART1_ID, 1024 * 3);
#endif
    uart_drv_blocking_read(UART1_ID, true);

	/* Open UART1 with 115200 baud rate. This will return mdev UART1
	 * handle */
	g_uartdev = uart_drv_open(UART1_ID, 115200);


	HF_Init();

	
	return;
}

int main()
{
	modules_init();

	wmprintf("Build Time: " __DATE__ " " __TIME__ "\r\n");
	/* Start the application framework */
	if (app_framework_start(common_event_handler) != WM_SUCCESS) {
		wmprintf("Failed to start application framework\r\n");
	}


    os_thread_create(&uart_thread,
			       "uart_thread",
			       (void *)uart_rx_cmd, 0,
			       &uart_thread_stack, OS_PRIO_3);	
	return 0;
}

