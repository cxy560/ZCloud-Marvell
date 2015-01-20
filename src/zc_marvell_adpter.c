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


HF_StaInfo g_struHfStaInfo = {
    DEFAULT_IOT_CLOUD_KEY,
    DEFAULT_IOT_PRIVATE_KEY,
    DEFAULT_DEVICIID,
    "www.ablecloud.cn"
};
u8 g_u8recvbuffer[HF_MAX_SOCKET_LEN];
ZC_UartBuffer g_struUartBuffer;
HF_TimerInfo g_struHfTimer[ZC_TIMER_MAX_NUM];
int g_Bcfd;
u8  g_u8BcSendBuffer[100];

extern u32 g_u32GloablIp;
update_desc_t g_ud;
struct partition_entry *g_passive;
extern psm_hnd_t psm_get_handle(void);
extern void psm_erase(int argc, char **argv);
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

    ret = psm_object_open(hnd, ABLECLOUD_MOD_NAME, PSM_MODE_READ, sizeof(HF_StaInfo),
        NULL, &ohandle);

    if (ret != WM_SUCCESS) 
    {
        ZC_Printf("fail read, step1 %d\n", ret);
        return;
    }
    
    ret = psm_object_read(ohandle, &g_struHfStaInfo, sizeof(HF_StaInfo));
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
void HF_WriteDataToFlash()
{
    int ret;
    psm_object_handle_t ohandle;
    psm_hnd_t hnd;

    hnd = psm_get_handle();

    ret = psm_object_open(hnd, ABLECLOUD_MOD_NAME, PSM_MODE_WRITE, sizeof(HF_StaInfo),
        NULL, &ohandle);

    if (ret != WM_SUCCESS) 
    {
        ZC_Printf("fail write, step1 %d\n", ret);
        return;
    }
    
    ret = psm_object_write(ohandle, &g_struHfStaInfo, sizeof(HF_StaInfo));
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
* Function: HF_SendDataToCloud
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_SendDataToCloud(PTC_Connection *pstruConnection)
{
    MSG_Buffer *pstruBuf = NULL;

    u16 u16DataLen; 
    pstruBuf = (MSG_Buffer *)MSG_PopMsg(&g_struSendQueue); 
    
    if (NULL == pstruBuf)
    {
        return;
    }
    
    u16DataLen = pstruBuf->u32Len; 

    send(pstruConnection->u32Socket, pstruBuf->u8MsgBuffer, u16DataLen, 0);
    ZC_Printf("send data len = %d\n", u16DataLen);
    pstruBuf->u8Status = MSG_BUFFER_IDLE;
    pstruBuf->u32Len = 0;
    return;
}
/*************************************************
* Function: HF_RecvDataFromCloud
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_RecvDataFromCloud(u8 *pu8Data, u32 u32DataLen)
{
    u32 u32RetVal;
    u16 u16PlainLen;
    u32RetVal = MSG_RecvData(&g_struRecvBuffer, pu8Data, u32DataLen);

    if (ZC_RET_OK == u32RetVal)
    {
        if (MSG_BUFFER_FULL == g_struRecvBuffer.u8Status)
        {
            u32RetVal = SEC_Decrypt((ZC_SecHead*)g_struRecvBuffer.u8MsgBuffer, 
                g_struRecvBuffer.u8MsgBuffer + sizeof(ZC_SecHead), g_u8MsgBuildBuffer, &u16PlainLen);

            /*copy data*/
            memcpy(g_struRecvBuffer.u8MsgBuffer, g_u8MsgBuildBuffer, u16PlainLen);

            g_struRecvBuffer.u32Len = u16PlainLen;
            if (ZC_RET_OK == u32RetVal)
            {
                u32RetVal = MSG_PushMsg(&g_struRecvQueue, (u8*)&g_struRecvBuffer);
            }
        }
    }
    
    return;
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
* Function: HF_GetStoreInfor
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_GetStoreInfor(u8 u8Type, u8 **pu8Data)
{
    switch(u8Type)
    {
        case ZC_GET_TYPE_CLOUDKEY:
            *pu8Data = g_struHfStaInfo.u8CloudKey;
            break;
        case ZC_GET_TYPE_DEVICEID:
            *pu8Data = g_struHfStaInfo.u8DeviciId;
            break;
        case ZC_GET_TYPE_PRIVATEKEY:
            *pu8Data = g_struHfStaInfo.u8PrivateKey;
            break;
        case ZC_GET_TYPE_VESION:
            *pu8Data = g_struHfStaInfo.u8EqVersion;        
            break;
        case ZC_GET_TYPE_TOKENKEY:
            *pu8Data = g_struHfStaInfo.u8TokenKey;        
            break;
    }
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
    psm_erase(0, NULL);
}
/*************************************************
* Function: HF_SendDataToNet
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_SendDataToNet(u32 u32Fd, u8 *pu8Data, u16 u16DataLen, ZC_SendParam *pstruParam)
{
    send(u32Fd, pu8Data, u16DataLen, 0);
}

/*************************************************
* Function: HF_StoreRegisterInfor
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_StoreRegisterInfor(u8 u8Type, u8 *pu8Data, u16 u16DataLen)
{
    switch(u8Type)
    {
        case 0:
        {
            ZC_RegisterReq *pstruRegister;
            
            pstruRegister = (ZC_RegisterReq *)(pu8Data);
            
            memcpy(g_struHfStaInfo.u8PrivateKey, pstruRegister->u8ModuleKey, ZC_MODULE_KEY_LEN);
            memcpy(g_struHfStaInfo.u8DeviciId, pstruRegister->u8DeviceId, ZC_HS_DEVICE_ID_LEN);
            memcpy(g_struHfStaInfo.u8DeviciId + ZC_HS_DEVICE_ID_LEN, pstruRegister->u8Domain, ZC_DOMAIN_LEN);
            memcpy(g_struHfStaInfo.u8EqVersion, pstruRegister->u8EqVersion, ZC_EQVERSION_LEN);
        
            break;
        }
        case 1:
        {
            memcpy(g_struHfStaInfo.u8TokenKey, pu8Data, u16DataLen);
            HF_WriteDataToFlash();
            break;        
        }
        default:
            break;
    }
    
    return ZC_RET_OK;
}
/*************************************************
* Function: HF_SendClientQueryReq
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_SendClientQueryReq(u8 *pu8Msg, u16 u16RecvLen)
{
    ZC_MessageHead *pstruMsg;
    struct sockaddr_in addr;
    ZC_ClientQueryRsp struRsp;
    u16 u16Len;
    u8 *pu8DeviceId;
    u8 *pu8Domain;    
    u32 u32Index;
    ZC_ClientQueryReq *pstruQuery;

    if (g_struProtocolController.u8MainState < PCT_STATE_ACCESS_NET)
    {
        return;
    }
    
    if (u16RecvLen != sizeof(ZC_MessageHead) + sizeof(ZC_ClientQueryReq))
    {
        return;
    }
    
    pstruMsg = (ZC_MessageHead *)pu8Msg;
    pstruQuery = (ZC_ClientQueryReq *)(pstruMsg + 1);

    if (ZC_CODE_CLIENT_QUERY_REQ != pstruMsg->MsgCode)
    {
        return;
    }
    g_struProtocolController.pstruMoudleFun->pfunGetStoreInfo(ZC_GET_TYPE_DEVICEID, &pu8DeviceId);
    pu8Domain = pu8DeviceId + ZC_HS_DEVICE_ID_LEN;

    /*Only first 6 bytes is vaild*/
    for (u32Index = 0; u32Index < 6; u32Index++)
    {
        if (pstruQuery->u8Domain[u32Index] != pu8Domain[u32Index])
        {
            return;
        }
        
    }


    memset((char*)&addr,0,sizeof(addr));
    addr.sin_family = AF_INET; 
    addr.sin_port = htons(ZC_MOUDLE_BROADCAST_PORT); 
    addr.sin_addr.s_addr=inet_addr("255.255.255.255"); 
    
    struRsp.addr[0] = g_u32GloablIp & 0xff;
    struRsp.addr[1] = (g_u32GloablIp >> 8) & 0xff;        
    struRsp.addr[2] = (g_u32GloablIp >> 16) & 0xff;
    struRsp.addr[3] = (g_u32GloablIp >> 24)  & 0xff;    
    
    memcpy(struRsp.DeviceId, pu8DeviceId, ZC_HS_DEVICE_ID_LEN);
    EVENT_BuildMsg(ZC_CODE_CLIENT_QUERY_RSP, 0, g_u8MsgBuildBuffer, &u16Len, (u8*)&struRsp, sizeof(ZC_ClientQueryRsp));
    sendto(g_Bcfd,g_u8MsgBuildBuffer,u16Len,0,(struct sockaddr *)&addr,sizeof(addr));             

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
                HF_RecvDataFromCloud(g_u8recvbuffer, s32RecvLen);
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
                net_close(connfd);
            }
            else
            {
                ZC_Printf("accept client = %d", connfd);
            }
        }
    }
    
    if (FD_ISSET(g_Bcfd, &fdread))
    {
        tmp = sizeof(addr); 
        s32RecvLen = recvfrom(g_Bcfd, g_u8BcSendBuffer, 100, 0, (struct sockaddr *)&addr, (socklen_t*)&tmp); 
        if(s32RecvLen > 0) 
        {
            HF_SendClientQueryReq(g_u8BcSendBuffer, (u16)s32RecvLen);
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
    ZC_Printf("connect to cloud %s\n",g_struHfStaInfo.u8CloudAddr);
    retval = net_gethostbyname((const char*)g_struHfStaInfo.u8CloudAddr, &addr_tmp);
    if (WM_SUCCESS != retval)
    {
        return ZC_RET_ERROR;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ZC_CLOUD_PORT);
    memcpy(&addr.sin_addr.s_addr, addr_tmp->h_addr_list[0], addr_tmp->h_length);
    fd = net_socket(AF_INET, SOCK_STREAM, 0);

    if(fd<0)
        return ZC_RET_ERROR;
    
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr))< 0)
    {
        net_close(fd);
        return ZC_RET_ERROR;
    }

    ZC_Printf("connect ok!\n");
    g_struProtocolController.struCloudConnection.u32Socket = fd;

    
    HF_Rand(g_struProtocolController.RandMsg);
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
    return;
}
/*************************************************
* Function: HF_SendBc
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_SendBc()
{
    struct sockaddr_in addr;
    u16 u16Len;
    static int sleepcount = 0;
    
    if (PCT_STATE_CONNECT_CLOUD != g_struProtocolController.u8MainState)
    {
        sleepcount = 0;
        return;
    }
    sleepcount++;
    if (sleepcount > 500)
    {
        memset((char*)&addr,0,sizeof(addr));
        addr.sin_family = AF_INET; 
        addr.sin_port = htons(ZC_MOUDLE_BROADCAST_PORT); 
        addr.sin_addr.s_addr=inet_addr("255.255.255.255"); 
        

        EVENT_BuildBcMsg(g_u8MsgBuildBuffer, &u16Len);

        if (g_struProtocolController.u16SendBcNum < (PCT_SEND_BC_MAX_NUM))
        {
           sendto(g_Bcfd,g_u8MsgBuildBuffer,u16Len,0,(struct sockaddr *)&addr,sizeof(addr)); 
           g_struProtocolController.u16SendBcNum++;
        }
        sleepcount = 0;
    }
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
    g_struHfAdapter.pfunSendToNet = HF_SendDataToNet;   
    g_struHfAdapter.pfunUpdate = HF_FirmwareUpdate;     
    g_struHfAdapter.pfunUpdateFinish = HF_FirmwareUpdateFinish;
    g_struHfAdapter.pfunSendToMoudle = HF_SendDataToMoudle;  
    g_struHfAdapter.pfunStoreInfo = HF_StoreRegisterInfor;
    g_struHfAdapter.pfunGetStoreInfo = HF_GetStoreInfor;
    g_struHfAdapter.pfunSetTimer = HF_SetTimer;   
    g_struHfAdapter.pfunStopTimer = HF_StopTimer;
    
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
    PCT_Sleep();
}

/******************************* FILE END ***********************************/



