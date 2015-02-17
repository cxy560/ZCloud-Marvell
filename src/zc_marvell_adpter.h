/**
******************************************************************************
* @file     zc_hf_adpter.h
* @authors  cxy
* @version  V1.0.0
* @date     10-Sep-2014
* @brief    HANDSHAKE
******************************************************************************
*/

#ifndef  __ZC_HF_ADPTER_H__ 
#define  __ZC_HF_ADPTER_H__

#include <zc_common.h>
#include <zc_protocol_controller.h>
#include <zc_module_interface.h>
#include <wm_os.h>

typedef struct 
{
    u32 u32FirstFlag;
    os_timer_t struHandle;
}HF_TimerInfo;


#define HF_MAX_SOCKET_LEN    (1000)




#define ABLECLOUD_MOD_NAME		"ablecloud"

#define VAR_ABLECLOUD_PRODUCT_INFO		"ablecloudinfo"


#ifdef __cplusplus
extern "C" {
#endif
void HF_Init(void);
void HF_WakeUp(void);
void HF_Sleep(void);
void HF_ReadDataFormFlash(void);
void HF_BcInit();
void HF_CloudRecvfunc();
#ifdef __cplusplus
}
#endif
#endif
/******************************* FILE END ***********************************/


