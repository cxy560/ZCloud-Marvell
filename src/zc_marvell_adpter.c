/**
******************************************************************************
* @file     zc_hf_adpter.c
* @authors  cxy
* @version  V1.0.0
* @date     10-Sep-2014
* @brief    Event
******************************************************************************
*/
#include <zc_protocol_controller.h>
#include <zc_timer.h>
#include <zc_module_interface.h>
#include <stdlib.h>
#include <wm_net.h>
#include <zc_marvell_adpter.h>
#include <mdev.h>
#include <psm.h>
#include <psm-v2.h>
#include <rfget.h>
#include <app_framework.h>



extern mdev_t *g_uartdev;

extern PTC_ProtocolCon  g_struProtocolController;
PTC_ModuleAdapter g_struHfAdapter;

MSG_Buffer g_struRecvBuffer;
MSG_Buffer g_struRetxBuffer;
MSG_Buffer g_struClientBuffer;


MSG_Queue  g_struRecvQueue;
MSG_Buffer g_struSendBuffer[MSG_BUFFER_SEND_MAX_NUM];
MSG_Queue  g_struSendQueue;

u8 g_u8MsgBuildBuffer[MSG_BULID_BUFFER_MAXLEN];
u8 g_u8ClientSendLen = 0;


u16 g_u16TcpMss;
u16 g_u16LocalPort;


u8 g_u8recvbuffer[HF_MAX_SOCKET_LEN];
ZC_UartBuffer g_struUartBuffer;
HF_TimerInfo g_struHfTimer[ZC_TIMER_MAX_NUM];

u8  g_u8BcSendBuffer[100];
struct sockaddr_in struRemoteAddr;

update_desc_t g_ud;
struct partition_entry *g_passive;
extern psm_hnd_t psm_get_handle(void);
extern void psm_erase(int argc, char **argv);
extern int app_network_set_nw_state(int state);

/*************************************************
* Function: HF_ReadDataFormFlash
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_ReadDataFormFlash(void) 
{
    int ret;
    psm_object_handle_t ohandle;
    psm_hnd_t hnd;

    hnd = psm_get_handle();

    ret = psm_object_open(hnd, ABLECLOUD_MOD_NAME, PSM_MODE_READ, sizeof(ZC_ConfigDB),
        NULL, &ohandle);

    if (ret != WM_SUCCESS) 
    {
        ZC_Printf("fail read, step1 %d\n", ret);
        return;
    }

    ret = psm_object_read(ohandle, &g_struZcConfigDb, sizeof(ZC_ConfigDB));
    if (ZC_MAGIC_FLAG == g_struZcConfigDb.u32MagicFlag)
    {
    }
    else
    {
        ZC_Printf("no para, use default\n");
        ZC_ConfigInitPara();
    }
    
    if (ret == WM_SUCCESS) 
    {
        ZC_Printf("fail read, step2\n");
        return;
    }

    ret = psm_object_close(&ohandle);
    if (ret != WM_SUCCESS) 
    {
        ZC_Printf("fail read, step3\n");
        return;
    }

}

/*************************************************
* Function: HF_WriteDataToFlash
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_WriteDataToFlash(u8 *pu8Data, u16 u16Len)
{
    int ret;
    psm_object_handle_t ohandle;
    psm_hnd_t hnd;

    hnd = psm_get_handle();

    ret = psm_object_open(hnd, ABLECLOUD_MOD_NAME, PSM_MODE_WRITE, sizeof(ZC_ConfigDB),
        NULL, &ohandle);

    if (ret != WM_SUCCESS) 
    {
        ZC_Printf("fail write, step1 %d\n", ret);
        return;
    }
    
    ret = psm_object_write(ohandle, pu8Data, u16Len);
    if (ret != WM_SUCCESS) 
    {
        ZC_Printf("fail write, step2\n");
        return;
    }

    ret = psm_object_close(&ohandle);
    if (ret != WM_SUCCESS) 
    {
        ZC_Printf("fail write, step3\n");    
        return;
    }
    
}

/*************************************************
* Function: HF_timer_callback
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_timer_callback(os_timer_arg_t arg) 
{
    u8 u8TimeId;

    u8TimeId = (int)os_timer_get_context(&arg);

    TIMER_TimeoutAction(u8TimeId);
    TIMER_StopTimer(u8TimeId);
}


/*************************************************
* Function: HF_StopTimer
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_StopTimer(u8 u8TimerIndex)
{
    os_timer_deactivate(&g_struHfTimer[u8TimerIndex].struHandle);
    os_timer_delete(&g_struHfTimer[u8TimerIndex].struHandle);
}

/*************************************************
* Function: HF_SetTimer
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_SetTimer(u8 u8Type, u32 u32Interval, u8 *pu8TimeIndex)
{
    u8 u8TimerIndex;
    u32 u32Tmp;
    u32 u32Retval;

    u32Retval = TIMER_FindIdleTimer(&u8TimerIndex);
    if (ZC_RET_OK == u32Retval)
    {
        TIMER_AllocateTimer(u8Type, u8TimerIndex, (u8*)&g_struHfTimer[u8TimerIndex]);
        u32Tmp = (u32)u8TimerIndex;
        os_timer_create(&g_struHfTimer[u8TimerIndex].struHandle,
        				  NULL,
        				  os_msec_to_ticks(u32Interval),
        				  &HF_timer_callback,
        				  (void *)u32Tmp,
        				  OS_TIMER_ONE_SHOT,
        				  OS_TIMER_NO_ACTIVATE); 
        				  
        g_struHfTimer[u8TimerIndex].u32FirstFlag = 1;
        os_timer_activate(&g_struHfTimer[u8TimerIndex].struHandle);  
        
        *pu8TimeIndex = u8TimerIndex;
    }
    
    return u32Retval;
}
/*************************************************
* Function: HF_FirmwareUpdateFinish
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_FirmwareUpdateFinish(u32 u32TotalLen)
{
	rfget_update_complete(&g_ud);

    return ZC_RET_OK;
}


/*************************************************
* Function: HF_FirmwareUpdate
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_FirmwareUpdate(u8 *pu8FileData, u32 u32Offset, u32 u32DataLen)
{
    int retval;

    if (0 == u32Offset)
    {
        g_passive = rfget_get_passive_firmware();
        retval = rfget_update_begin(&g_ud, g_passive);
        rfget_init();
    }
    
    retval = rfget_update_data(&g_ud, (char *)pu8FileData, u32DataLen);
    if (retval < 0)
    {
        return ZC_RET_ERROR;
    }

    return ZC_RET_OK;
}
/*************************************************
* Function: HF_SendDataToMoudle
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_SendDataToMoudle(u8 *pu8Data, u16 u16DataLen)
{
    u8 u8MagicFlag[4] = {0x02,0x03,0x04,0x05};
    
    uart_drv_write(g_uartdev,u8MagicFlag,4); 
    uart_drv_write(g_uartdev,pu8Data,u16DataLen); 
    
    return ZC_RET_OK;
}


/*************************************************
* Function: HF_Rest
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_Rest(void)
{
    (void)app_network_set_nw_state(APP_NETWORK_NOT_PROVISIONED);
    app_reboot(REASON_USER_REBOOT);
}
/*************************************************
* Function: HF_SendTcpData
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_SendTcpData(u32 u32Fd, u8 *pu8Data, u16 u16DataLen, ZC_SendParam *pstruParam)
{
    send(u32Fd, pu8Data, u16DataLen, 0);
}
/*************************************************
* Function: HF_SendUdpData
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_SendUdpData(u32 u32Fd, u8 *pu8Data, u16 u16DataLen, ZC_SendParam *pstruParam)
{
    sendto(u32Fd,(char*)pu8Data,u16DataLen,0,
        (struct sockaddr *)pstruParam->pu8AddrPara,
        sizeof(struct sockaddr_in)); 
}

/*************************************************
* Function: HF_CloudRecvfunc
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_CloudRecvfunc() 
{
    s32 s32RecvLen=0; 
    fd_set fdread;
    u32 u32Index;
    u32 u32Len=0; 
    u32 u32ActiveFlag = 0;
    struct sockaddr_in cliaddr;
    int connfd;
    u32 u32MaxFd = 0;
    struct timeval timeout; 
    struct sockaddr_in addr;
    int tmp=1;    

    
    ZC_StartClientListen();
    
    u32ActiveFlag = 0;
    
    timeout.tv_sec= 0; 
    timeout.tv_usec= 1000; 
    
    FD_ZERO(&fdread);
    
    FD_SET(g_Bcfd, &fdread);
    u32MaxFd = u32MaxFd > g_Bcfd ? u32MaxFd : g_Bcfd;
    
    if (PCT_INVAILD_SOCKET != g_struProtocolController.struClientConnection.u32Socket)
    {
        FD_SET(g_struProtocolController.struClientConnection.u32Socket, &fdread);
        u32MaxFd = u32MaxFd > g_struProtocolController.struClientConnection.u32Socket ? u32MaxFd : g_struProtocolController.struClientConnection.u32Socket;
        u32ActiveFlag = 1;
    }
    
    if ((g_struProtocolController.u8MainState >= PCT_STATE_WAIT_ACCESSRSP) 
    && (g_struProtocolController.u8MainState < PCT_STATE_DISCONNECT_CLOUD))
    {
        FD_SET(g_struProtocolController.struCloudConnection.u32Socket, &fdread);
        u32MaxFd = u32MaxFd > g_struProtocolController.struCloudConnection.u32Socket ? u32MaxFd : g_struProtocolController.struCloudConnection.u32Socket;
        u32ActiveFlag = 1;
    }
    
    
    for (u32Index = 0; u32Index < ZC_MAX_CLIENT_NUM; u32Index++)
    {
        if (0 == g_struClientInfo.u32ClientVaildFlag[u32Index])
        {
            FD_SET(g_struClientInfo.u32ClientFd[u32Index], &fdread);
            u32MaxFd = u32MaxFd > g_struClientInfo.u32ClientFd[u32Index] ? u32MaxFd : g_struClientInfo.u32ClientFd[u32Index];
            u32ActiveFlag = 1;            
        }
    }
    
    
    if (0 == u32ActiveFlag)
    {
        return;
    }
    
    select(u32MaxFd + 1, &fdread, NULL, NULL, &timeout);
    
    if ((g_struProtocolController.u8MainState >= PCT_STATE_WAIT_ACCESSRSP) 
    && (g_struProtocolController.u8MainState < PCT_STATE_DISCONNECT_CLOUD))
    {
        if (FD_ISSET(g_struProtocolController.struCloudConnection.u32Socket, &fdread))
        {
            s32RecvLen = recv(g_struProtocolController.struCloudConnection.u32Socket, g_u8recvbuffer, HF_MAX_SOCKET_LEN, 0); 
            
            if(s32RecvLen > 0) 
            {
                ZC_Printf("recv data len = %d\n", s32RecvLen);
                MSG_RecvDataFromCloud(g_u8recvbuffer, s32RecvLen);
            }
            else
            {
                ZC_Printf("recv error, len = %d\n",s32RecvLen);
                PCT_DisConnectCloud(&g_struProtocolController);
                
                g_struUartBuffer.u32Status = MSG_BUFFER_IDLE;
                g_struUartBuffer.u32RecvLen = 0;
            }
        }
        
    }
    
    
    for (u32Index = 0; u32Index < ZC_MAX_CLIENT_NUM; u32Index++)
    {
        if (0 == g_struClientInfo.u32ClientVaildFlag[u32Index])
        {
            if (FD_ISSET(g_struClientInfo.u32ClientFd[u32Index], &fdread))
            {
                s32RecvLen = recv(g_struClientInfo.u32ClientFd[u32Index], g_u8recvbuffer, HF_MAX_SOCKET_LEN, 0); 
                if (s32RecvLen > 0)
                {
                    ZC_RecvDataFromClient(g_struClientInfo.u32ClientFd[u32Index], g_u8recvbuffer, s32RecvLen);
                }
                else
                {   
                    ZC_ClientDisconnect(g_struClientInfo.u32ClientFd[u32Index]);
                    close(g_struClientInfo.u32ClientFd[u32Index]);
                }
                
            }
        }
        
    }
    
    if (PCT_INVAILD_SOCKET != g_struProtocolController.struClientConnection.u32Socket)
    {
        if (FD_ISSET(g_struProtocolController.struClientConnection.u32Socket, &fdread))
        {
            connfd = accept(g_struProtocolController.struClientConnection.u32Socket,(struct sockaddr *)&cliaddr,(socklen_t*)&u32Len);
    
            if (ZC_RET_ERROR == ZC_ClientConnect((u32)connfd))
            {
                close(connfd);
            }
            else
            {
                ZC_Printf("accept client = %d\n", connfd);
            }
        }
    }
    
    if (FD_ISSET(g_Bcfd, &fdread))
    {
        tmp = sizeof(addr); 
        s32RecvLen = recvfrom(g_Bcfd, g_u8BcSendBuffer, 100, 0, (struct sockaddr *)&addr, (socklen_t*)&tmp); 
        if(s32RecvLen > 0) 
        {
            ZC_SendClientQueryReq(g_u8BcSendBuffer, (u16)s32RecvLen);
        } 
    }
}
/*************************************************
* Function: HF_Rand
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_Rand(u8 *pu8Rand)
{
    u32 u32Rand;
    u32 u32Index; 
    for (u32Index = 0; u32Index < 10; u32Index++)
    {
        u32Rand = rand();
        pu8Rand[u32Index * 4] = ((u8)u32Rand % 26) + 65;
        pu8Rand[u32Index * 4 + 1] = ((u8)(u32Rand >> 8) % 26) + 65;
        pu8Rand[u32Index * 4 + 2] = ((u8)(u32Rand >> 16) % 26) + 65;
        pu8Rand[u32Index * 4 + 3] = ((u8)(u32Rand >> 24) % 26) + 65;        
    }
}

/*************************************************
* Function: HF_ConnectToCloud
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_ConnectToCloud(PTC_Connection *pstruConnection)
{
    int fd; 
    struct sockaddr_in addr;
    struct hostent *addr_tmp = NULL;
    int retval;
    
    memset((char*)&addr,0,sizeof(addr));
    if (1 == g_struZcConfigDb.struSwitchInfo.u32TestAddrConfig)
    {
        ZC_Printf("test cloud\n");
        retval = net_gethostbyname((const char *)"test.ablecloud.cn", &addr_tmp);
    }
    else if (2 == g_struZcConfigDb.struSwitchInfo.u32TestAddrConfig)
    {
        ZC_Printf("test cloud 2\n");
        retval = WM_SUCCESS;
    }
    else
    {
        retval = net_gethostbyname((const char *)g_struZcConfigDb.struCloudInfo.u8CloudAddr, &addr_tmp);
    }
    
    if (WM_SUCCESS != retval)
    {
        return ZC_RET_ERROR;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ZC_CLOUD_PORT);
    if (2 == g_struZcConfigDb.struSwitchInfo.u32TestAddrConfig)
    {
        addr.sin_addr.s_addr = htonl(g_struZcConfigDb.struSwitchInfo.u32ServerIp);
    }
    else
    {
        memcpy(&addr.sin_addr.s_addr, addr_tmp->h_addr_list[0], addr_tmp->h_length);
    }
    fd = net_socket(AF_INET, SOCK_STREAM, 0);

    if(fd<0)
        return ZC_RET_ERROR;
    
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr))< 0)
    {
        close(fd);
        return ZC_RET_ERROR;
    }

    ZC_Printf("connect ok!\n");
    g_struProtocolController.struCloudConnection.u32Socket = fd;

    
    ZC_Rand(g_struProtocolController.RandMsg);
    return ZC_RET_OK;
}
/*************************************************
* Function: HF_ConnectToCloud
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_ListenClient(PTC_Connection *pstruConnection)
{
    int fd; 
    struct sockaddr_in servaddr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd<0)
        return ZC_RET_ERROR;

    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    servaddr.sin_port = htons(pstruConnection->u16Port);
    if(bind(fd,(struct sockaddr *)&servaddr,sizeof(servaddr))<0)
    {
        close(fd);
        return ZC_RET_ERROR;
    }
    
    if (listen(fd, TCP_DEFAULT_LISTEN_BACKLOG)< 0)
    {
        close(fd);
        return ZC_RET_ERROR;
    }

    ZC_Printf("Tcp Listen Port = %d\n", pstruConnection->u16Port);
    g_struProtocolController.struClientConnection.u32Socket = fd;
    return ZC_RET_OK;
}

/*************************************************
* Function: HF_BcInit
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_BcInit()
{
    int tmp=1;
    struct sockaddr_in addr; 

    addr.sin_family = AF_INET; 
    addr.sin_port = htons(ZC_MOUDLE_PORT); 
    addr.sin_addr.s_addr=htonl(INADDR_ANY);

    g_Bcfd = socket(AF_INET, SOCK_DGRAM, 0); 

    tmp=1; 
    setsockopt(g_Bcfd, SOL_SOCKET,SO_BROADCAST,&tmp,sizeof(tmp)); 

    bind(g_Bcfd, (struct sockaddr*)&addr, sizeof(addr)); 

    memset((char*)&struRemoteAddr,0,sizeof(struRemoteAddr));
    struRemoteAddr.sin_family = AF_INET; 
    struRemoteAddr.sin_port = htons(ZC_MOUDLE_BROADCAST_PORT); 
    struRemoteAddr.sin_addr.s_addr=inet_addr("255.255.255.255"); 
    g_pu8RemoteAddr = (u8*)&struRemoteAddr;
    g_u32BcSleepCount = 1000;
    return;
}

/*************************************************
* Function: HF_Init
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_Init()
{
    int ret;
    
    ZC_Printf("MT Init\n");
    g_struHfAdapter.pfunConnectToCloud = HF_ConnectToCloud;
    g_struHfAdapter.pfunListenClient = HF_ListenClient;
    g_struHfAdapter.pfunSendTcpData = HF_SendTcpData;  
    g_struHfAdapter.pfunSendUdpData = HF_SendUdpData;   
    g_struHfAdapter.pfunUpdate = HF_FirmwareUpdate;     
    g_struHfAdapter.pfunUpdateFinish = HF_FirmwareUpdateFinish;
    g_struHfAdapter.pfunSendToMoudle = HF_SendDataToMoudle;  
    g_struHfAdapter.pfunSetTimer = HF_SetTimer;   
    g_struHfAdapter.pfunStopTimer = HF_StopTimer;
    g_struHfAdapter.pfunWriteFlash = HF_WriteDataToFlash;
    g_struHfAdapter.pfunRest = HF_Rest;
    g_u16TcpMss = 1000;
    PCT_Init(&g_struHfAdapter);

    g_struUartBuffer.u32Status = MSG_BUFFER_IDLE;
    g_struUartBuffer.u32RecvLen = 0;

    ret = psm_register_module(ABLECLOUD_MOD_NAME, "common_part", PSM_CREAT);
    if (ret != WM_SUCCESS && ret != -WM_E_EXIST) 
    {
        ZC_Printf("Failed to register ablecloud module with psm\n");
        return;
    }

}

/*************************************************
* Function: HF_WakeUp
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_WakeUp()
{
    PCT_WakeUp();
}
/*************************************************
* Function: HF_Sleep
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_Sleep()
{
    u32 u32Index;

    close(g_Bcfd);

    if (PCT_INVAILD_SOCKET != g_struProtocolController.struClientConnection.u32Socket)
    {
        close(g_struProtocolController.struClientConnection.u32Socket);
        g_struProtocolController.struClientConnection.u32Socket = PCT_INVAILD_SOCKET;
    }

    if (PCT_INVAILD_SOCKET != g_struProtocolController.struCloudConnection.u32Socket)
    {
        close(g_struProtocolController.struCloudConnection.u32Socket);
        g_struProtocolController.struCloudConnection.u32Socket = PCT_INVAILD_SOCKET;
    }
    
    for (u32Index = 0; u32Index < ZC_MAX_CLIENT_NUM; u32Index++)
    {
        if (0 == g_struClientInfo.u32ClientVaildFlag[u32Index])
        {
            close(g_struClientInfo.u32ClientFd[u32Index]);
            g_struClientInfo.u32ClientFd[u32Index] = PCT_INVAILD_SOCKET;
        }
    }

    PCT_Sleep();
}

/******************************* FILE END ***********************************/



