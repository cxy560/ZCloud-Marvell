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


/* Thread handle */
static os_thread_t app_thread;
static os_thread_t uart_thread;

/* Buffer to be used as stack */
static os_thread_stack_define(app_stack, 8 * 1024);
static os_thread_stack_define(uart_thread_stack, 5 * 2048);

extern PTC_ProtocolCon  g_struProtocolController;
u32 g_u32GloablIp;

mdev_t *g_uartdev;
extern ZC_UartBuffer g_struUartBuffer;

void uart_rx_cmd(os_thread_arg_t arg)
{
	int uart_rx_len = 0;
	u8 u8Buffer[1024];

	while(1) 
	{
        uart_rx_len = uart_drv_read(g_uartdev, u8Buffer, 1024);
        
        if (uart_rx_len > 0)
        {
            ZC_Moudlefunc(u8Buffer, uart_rx_len);
        }
        
        //uart_drv_rx_buf_reset(dev);
    }
}


static void arrayent_demo_main(os_thread_arg_t data)
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
            net_close(fd);
            u32Timer = rand();
            u32Timer = (PCT_TIMER_INTERVAL_RECONNECT) * (u32Timer % 10 + 1);
            PCT_ReconnectCloud(&g_struProtocolController, u32Timer);
            g_struUartBuffer.u32Status = MSG_BUFFER_IDLE;
            g_struUartBuffer.u32RecvLen = 0;
        }
        else
        {
            HF_SendDataToCloud(&g_struProtocolController.struCloudConnection);
        }
        HF_SendBc();
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
    struct wlan_network WlanInfo;
	switch (event) {
	case AF_EVT_WLAN_INIT_DONE:
		ret = psm_cli_init();
		if (ret != WM_SUCCESS)
			wmprintf("Error: psm_cli_init failed\r\n");
		int i = (int) data;

		if (i == APP_NETWORK_NOT_PROVISIONED) {
			wmprintf("\r\nPlease provision the device "
				"and then reboot it:\r\n\r\n");
			wmprintf("psm-set network ssid <ssid>\r\n");
			wmprintf("psm-set network security <security_type>"
				"\r\n");
			wmprintf("    where: security_type: 0 if open,"
				" 3 if wpa, 4 if wpa2\r\n");
			wmprintf("psm-set network passphrase <passphrase>"
				" [valid only for WPA and WPA2 security]\r\n");
			wmprintf("psm-set network configured 1\r\n");
			wmprintf("pm-reboot\r\n\r\n");
		} 
		else
		{
        	app_sta_start();		    
		}
		break;
	case AF_EVT_NORMAL_CONNECTED:
        app_network_get_nw(&WlanInfo);
        g_u32GloablIp = WlanInfo.ip.ipv4.address;
		if (!is_cloud_started) {
			ret = os_thread_create(&app_thread,  /* thread handle */
				"arrayent_demo_thread",/* thread name */
				arrayent_demo_main,  /* entry function */
				0,          /* argument */
				&app_stack,     /* stack */
				OS_PRIO_3);     /* priority - medium low */
			is_cloud_started = true;
		}
		HF_WakeUp();
		break;
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

	/* Enable DMA on UART1 */
	uart_drv_xfer_mode(UART1_ID, UART_DMA_ENABLE);

	/* Set DMA block size */
	uart_drv_dma_rd_blk_size(UART1_ID, 512);

	/* Set internal rx ringbuffer size to 3K */
	uart_drv_rxbuf_size(UART1_ID, 1024 * 3);

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
