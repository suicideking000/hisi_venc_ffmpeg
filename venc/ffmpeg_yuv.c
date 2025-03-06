
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
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include <sys/prctl.h>
#include "sample_comm.h"
#include "libavformat/avformat.h"
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include "libavcodec/avcodec.h"
#include "libavcodec/bsf.h"
#include <libavutil/timestamp.h>
#include "libavutil/channel_layout.h"  
#include "libavutil/common.h"  
#include "libavutil/imgutils.h"  
#include "libswscale/swscale.h"   
#include "libavutil/imgutils.h"           
#include "libavutil/mathematics.h"      
#include "libavutil/samplefmt.h" 


int main(void)
{int ret;
 ret=HI_MP4toAVI();
 ret=HI_MP4toYUV420P();
    return 0;
}
#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */c