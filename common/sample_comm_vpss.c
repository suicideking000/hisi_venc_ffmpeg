
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>

#include "sample_comm.h"


/*****************************************************************************
* function : start vpss grp.
*****************************************************************************/
HI_S32 SAMPLE_COMM_VPSS_Start(VPSS_GRP VpssGrp, HI_BOOL* pabChnEnable, VPSS_GRP_ATTR_S* pstVpssGrpAttr, VPSS_CHN_ATTR_S* pastVpssChnAttr)
{
    VPSS_CHN VpssChn;
    HI_S32 s32Ret;
    HI_S32 j;

    s32Ret = HI_MPI_VPSS_CreateGrp(VpssGrp, pstVpssGrpAttr);

    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("HI_MPI_VPSS_CreateGrp(grp:%d) failed with %#x!\n", VpssGrp, s32Ret);
        return HI_FAILURE;
    }

    s32Ret = HI_MPI_VPSS_StartGrp(VpssGrp);

    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("HI_MPI_VPSS_StartGrp failed with %#x\n", s32Ret);
        return HI_FAILURE;
    }

    for (j = 0; j < VPSS_MAX_PHY_CHN_NUM; j++)
    {
        if(HI_TRUE == pabChnEnable[j])
        {
            VpssChn = j;
            s32Ret = HI_MPI_VPSS_SetChnAttr(VpssGrp, VpssChn, &pastVpssChnAttr[VpssChn]);

            if (s32Ret != HI_SUCCESS)
            {
                SAMPLE_PRT("HI_MPI_VPSS_SetChnAttr failed with %#x\n", s32Ret);
                return HI_FAILURE;
            }

            s32Ret = HI_MPI_VPSS_EnableChn(VpssGrp, VpssChn);

            if (s32Ret != HI_SUCCESS)
            {
                SAMPLE_PRT("HI_MPI_VPSS_EnableChn failed with %#x\n", s32Ret);
                return HI_FAILURE;
            }
        }
    }

    return HI_SUCCESS;
}

/*****************************************************************************
* function : stop vpss grp
*****************************************************************************/
HI_S32 SAMPLE_COMM_VPSS_Stop(VPSS_GRP VpssGrp, HI_BOOL* pabChnEnable)
{
    HI_S32 j;
    HI_S32 s32Ret = HI_SUCCESS;
    VPSS_CHN VpssChn;

    for (j = 0; j < VPSS_MAX_PHY_CHN_NUM; j++)
    {
        if(HI_TRUE == pabChnEnable[j])
        {
            VpssChn = j;
            s32Ret = HI_MPI_VPSS_DisableChn(VpssGrp, VpssChn);

            if (s32Ret != HI_SUCCESS)
            {
                SAMPLE_PRT("failed with %#x!\n", s32Ret);
                return HI_FAILURE;
            }
        }
    }

    s32Ret = HI_MPI_VPSS_StopGrp(VpssGrp);

    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }

    s32Ret = HI_MPI_VPSS_DestroyGrp(VpssGrp);

    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }

    return HI_SUCCESS;
}

/*****************************************************************************
* function : getchnframe and process pthread
*****************************************************************************/
HI_S32 SAMPLE_COMM_VPSS_GETCHNFRAME_START(VPSS_GRP VpssGr, VPSS_CHN VpssChn)
{

    getchnframe_process_s getchnframe_process_test;
    HI_S32 s32Ret= HI_SUCCESS;
    pthread_t getchnframe_process_id;
    getchnframe_process_test.VpssGr=VpssGr;
    getchnframe_process_test.VpssChn=VpssChn;

    s32Ret=pthread_create(&getchnframe_process_id,NULL,&SAMPLE_COMM_VPSS_GETCHNFRAME,(HI_VOID*)&getchnframe_process_test);
    if(s32Ret!=HI_SUCCESS)
    {
        SAMPLE_PRT("pthread create failed!\n");
        return HI_FAILURE;
    }
    pthread_detach(getchnframe_process_id);
    return HI_SUCCESS;
}

/*****************************************************************************
* function : getchnframe and process
*****************************************************************************/
HI_VOID* SAMPLE_COMM_VPSS_GETCHNFRAME(HI_VOID*arg)
{
    getchnframe_process_s* getchnframe_process_test;
    getchnframe_process_test=(getchnframe_process_s*)arg;
    VIDEO_FRAME_INFO_S getchnframe_process_picinfo;
    HI_S32 s32Ret=HI_SUCCESS;

    while(1)
    {
        s32Ret=HI_MPI_VPSS_GetChnFrame(getchnframe_process_test->VpssGr,getchnframe_process_test->VpssChn,&getchnframe_process_picinfo,-1);
        if(s32Ret!=HI_SUCCESS)
        {
            SAMPLE_PRT("pthread get chnframe failed!\n");
           
        }
        else
        {
            /***图像处理***/
            SAMPLE_COMM_VPSS_EXTRACT_Y(&getchnframe_process_picinfo);


            s32Ret=HI_MPI_VO_SendFrame(getchnframe_process_test->VpssGr,0,&getchnframe_process_picinfo,-1);
            if(s32Ret!=HI_SUCCESS)
            {
                SAMPLE_PRT("pthread send chnframe failed!\n");
                
            }
            s32Ret=HI_MPI_VPSS_ReleaseChnFrame(getchnframe_process_test->VpssGr,getchnframe_process_test->VpssChn,&getchnframe_process_picinfo);
            if(s32Ret!=HI_SUCCESS)
            {
                SAMPLE_PRT("pthread release chnframe failed!\n");
                
            }
        }

    }

}
/*****************************************************************************
* function :  process function
*****************************************************************************/
HI_S32 SAMPLE_COMM_VPSS_EXTRACT_Y(VIDEO_FRAME_INFO_S* getchnframe_process_picinfo)
{
    VIDEO_FRAME_INFO_S st_process_picinfo;
    VIDEO_FRAME_INFO_S* pst_process_picinfo=&st_process_picinfo;
    HI_VOID * pVirAddr;
    HI_U32 u32height = getchnframe_process_picinfo->stVFrame.u32Height;
    HI_U32 u32stride=getchnframe_process_picinfo->stVFrame.u32Stride[0];
    HI_U32 i;
    pVirAddr=(HI_U8*)HI_MPI_SYS_Mmap(getchnframe_process_picinfo->stVFrame.u64PhyAddr[0],u32height*u32stride*3/2);

    HI_U8* YUVdata=pVirAddr;
   for(i=0;i<u32height*u32stride;i++)
    {
        YUVdata[i]=255-YUVdata[i];
    }
    for(i=u32height*u32stride;i<u32height*u32stride*3/2;i++)
    {
        YUVdata[i]=255-YUVdata[i];
    }
    HI_MPI_SYS_Munmap((HI_VOID*)pVirAddr,u32height*u32stride*3/2);
    return HI_SUCCESS;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
