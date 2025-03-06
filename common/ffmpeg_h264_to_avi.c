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

#include "sample_comm.h"
#define USING_SEQ			 // 计算 PTS、DTS 的方法
#define STREAM_FRAME_RATE 60 // 输出帧率

HI_S32 HI_PDT_CreateAVI(VENC_CHN VeChn, char *pfile, FfmpegConf *fc)
{
	int ret = 0;
	char pszFileName[256] = {0};	// 保存文件名
    AVOutputFormat *pOutFmt = NULL; // 输出Format指针
	sprintf(pszFileName, "%s_h264_to_avi.avi", pfile);

	// av_register_all();                      //注册所有编解码器、复用/解复用组件

	// 初始化输出视频码流的AVFormatContext
	avformat_alloc_output_context2(&(fc->g_OutFmt_Ctx[VeChn]), NULL, NULL, pszFileName);
	if (NULL == fc->g_OutFmt_Ctx[VeChn]) // 失败处理
	{
		printf("Could not deduce output format from file extension: using avi. \n");
		avformat_alloc_output_context2(&(fc->g_OutFmt_Ctx[VeChn]), NULL, "avi", pszFileName);
		if (NULL == fc->g_OutFmt_Ctx[VeChn])
		{
			printf("avformat_alloc_output_context2 failed\n");
			return -1;
		}
	}

	pOutFmt = fc->g_OutFmt_Ctx[VeChn]->oformat;	  // 获取输出Format指针
	if (pOutFmt->video_codec == AV_CODEC_ID_NONE) // 检查视频编码器
	{
		printf("add_video_stream ID failed\n");
		goto exit_outFmt_failed;
	}
	else
	{
		printf("\tpOutfmt->video_codec=%d\n\t",pOutFmt->video_codec);
	}
	if (!(pOutFmt->flags & AVFMT_NOFILE)) // 应该是判断文件IO是否打开
	{
		ret = avio_open(&(fc->g_OutFmt_Ctx[VeChn]->pb), pszFileName, AVIO_FLAG_WRITE); // 创建并打开视频文件
		if (ret < 0)
		{
			printf("could not create video file %s.\n", pszFileName);
			goto exit_vio_open_failed;
		}
	}

	// 初始化一些参数
	fc->Video_PTS[VeChn] = 0;
	fc->Vfirst[VeChn] = 0;
	fc->vi[VeChn] = -1;
	fc->b_First_IDR_Find[VeChn] = 0;
	printf("vedio file created success\n");
	
	return HI_SUCCESS;
// 错误处理
exit_vio_open_failed:
	if (fc->g_OutFmt_Ctx[VeChn] && !(fc->g_OutFmt_Ctx[VeChn]->flags & AVFMT_NOFILE))
		avio_close(fc->g_OutFmt_Ctx[VeChn]->pb);
exit_outFmt_failed:
	if (NULL != fc->g_OutFmt_Ctx[VeChn])
		avformat_free_context(fc->g_OutFmt_Ctx[VeChn]);
	return -1;
}

HI_S32 HI_PDT_WriteVideo_AVI(VENC_CHN VeChn, VENC_STREAM_S *pstStream, FfmpegConf *fc)
{
	unsigned int i = 0;
	unsigned char* pPackVirtAddr = NULL;	//码流首地址
	unsigned int u32PackLen = 0;			//码流长度
    int ret = 0;
    AVStream *Vpst = NULL; 					//视频流指针
    AVPacket pkt;			//音视频包结构体，这个包不是海思的包，填充之后，用于最终写入数据
	uint8_t sps_buf[32];
    uint8_t pps_buf[32];
    uint8_t sps_pps_buf[64];
    unsigned int pps_len=0;
    unsigned int sps_len=0;
	AVBSFContext *h264bsfc;
	const AVBitStreamFilter *filter = av_bsf_get_by_name("h264_mp4toannexb");
	ret = av_bsf_alloc(filter, &h264bsfc);
	
	if(NULL == pstStream)	//裸码流有效判断
	{
		return HI_SUCCESS;
	}
	//u32PackCount是海思中记录此码流结构体中码流包数量，一般含I帧的是4个包，P帧1个
    for (i = 0 ; i < pstStream->u32PackCount; i++)	
    {
    	//从海思码流包中获取数据地址，长度
        pPackVirtAddr = pstStream->pstPack[i].pu8Addr + pstStream->pstPack[i].u32Offset;	
        u32PackLen = pstStream->pstPack[i].u32Len - pstStream->pstPack[i].u32Offset;	
		av_init_packet(&pkt);			//初始化AVpack包
		pkt.flags = AV_PKT_FLAG_KEY;	//默认是关键帧，关不关键好像都没问题
		switch(pstStream->pstPack[i].DataType.enH264EType)
		{
			case H264E_NALU_SPS:	//如果这个包是SPS
				pkt.flags =  0;	    //不是关键帧
				if(fc->b_First_IDR_Find[VeChn] == 2)	//如果不是第一个SPS帧
				{
					continue;	//不处理，丢弃
					//我只要新建文件之后的第一个SPS PPS信息，后面都是一样的，只要第一个即可
				}
				else //如果是第一个SPS帧
				{
					sps_len = u32PackLen;
					memcpy(sps_buf, pPackVirtAddr, sps_len);
					if(fc->b_First_IDR_Find[VeChn] == 1)	//如果PPS帧已经收到
					{
						memcpy(sps_pps_buf, sps_buf, sps_len);	//复制sps
						memcpy(sps_pps_buf+sps_len, pps_buf, pps_len);	//加上pps
						//去添加视频流，和SPS PPS信息，这步之后才开始写入视频帧
						ret = HI_ADD_SPS_PPS_AVI(VeChn, sps_pps_buf, sps_len+pps_len, fc);
						if(ret<0)return HI_FAILURE;
					}
					fc->b_First_IDR_Find[VeChn]++;
				}
				continue; //继续
			case H264E_NALU_PPS:
				pkt.flags = 0;	//不是关键帧
				if(fc->b_First_IDR_Find[VeChn] == 2)	//如果不是第一个PPS帧
				{
					continue;
				}
				else //是第一个PPS帧
				{
					pps_len = u32PackLen;
					memcpy(pps_buf, pPackVirtAddr, pps_len);	//复制
					if(fc->b_First_IDR_Find[VeChn] == 1)	//如果SPS帧已经收到
					{
						memcpy(sps_pps_buf, sps_buf, sps_len);
						memcpy(sps_pps_buf+sps_len, pps_buf, pps_len);
						//这里和SPS那里互斥，只有一个会执行，主要是看SPS和PPS包谁排在后面
						ret = HI_ADD_SPS_PPS_AVI(VeChn, sps_pps_buf, sps_len+pps_len, fc); 
						if(ret<0)return HI_FAILURE;
					}
					fc->b_First_IDR_Find[VeChn]++;
				}
				continue;
			case H264E_NALU_SEI:	//增强帧，不含图像数据信息，只是对图像数据信息和视频流的补充
				continue;	//不稀罕这个帧
			case H264E_NALU_PSLICE:		//P帧
			case H264E_NALU_IDRSLICE:	//I帧
				if(fc->b_First_IDR_Find[VeChn] != 2)	//如果这个文件还没有收到过sps和pps帧
				{
					continue;	//跳过，不处理这帧
				}
				break;
			default:
				break;
		}
		
		if(fc->vi[VeChn] < 0)	//流索引号，如果g_OutFmt_Ctx里面还没有新建视频流，也就是说还没收到I帧
		{
			#ifdef DEBUG
			printf("vi less than 0 \n");
			#endif
			return HI_SUCCESS;
		}

		if(fc->Vfirst[VeChn] == 0)	//如果是文件的第一帧视频
		{
			fc->Vfirst[VeChn] = 1;	
			#ifdef USING_SEQ	//使用帧序号计算PTS
			fc->Video_PTS[VeChn] = pstStream->u32Seq; //记录初始序号
			#endif
			#ifdef USING_PTS	//直接使用海思的PTS
			fc->Video_PTS[VeChn] = pstStream->pstPack[i].u64PTS;	//记录开始时间戳
			#endif
		}
		
		Vpst = fc->g_OutFmt_Ctx[VeChn]->streams[fc->vi[VeChn]];	//根据索引号获取视频流地址
	    pkt.stream_index = Vpst->index;	//视频流的索引号 赋给 包里面的流索引号，表示这个包属于视频流
		avcodec_parameters_copy(h264bsfc->par_in, Vpst->codecpar);
		av_bsf_init(h264bsfc);
		
		//以下，时间基转换，PTS很重要，涉及音视频同步问题
		#if 0	
			pkt.pts = av_rescale_q_rnd((fc->VptsInc[VeChn]++), Vpst->codec->time_base,Vpst->time_base,(enum AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
			pkt.dts = av_rescale_q_rnd(pkt.pts, Vpst->time_base,Vpst->time_base,(enum AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
		#endif
		#if 1	
			#ifdef USING_SEQ	
				
				pkt.pts = av_rescale_q_rnd(pstStream->u32Seq - fc->Video_PTS[VeChn], (AVRational){1, STREAM_FRAME_RATE},Vpst->time_base,(enum AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
				pkt.dts = pkt.pts;	//只有I、P帧，相等就行了
			#endif
			#ifdef USING_PTS
				//海思的PTS是us单位，所以将真实世界的1000000us转成90000Hz频率的时间
				pkt.pts = pkt.dts =(int64_t)((pstStream->pstPack[i].u64PTS - fc->Video_PTS[VeChn]) *0.09+0.5);
			#endif
		#endif
		// 这个位置的数值设置是否合理（与输入帧率、输出帧率匹配）将直接影响最终封装出来的 mp4 播放效果，
		pkt.duration = 16;      // 60帧 16ms
		pkt.duration = av_rescale_q(pkt.duration, Vpst->time_base, Vpst->time_base);
		pkt.pos = -1;	//默认
		
		//最重要的数据要给AVpack包
		pkt.data = pPackVirtAddr ;	//接受视频数据NAUL
   		pkt.size = u32PackLen;		//视频数据长度
		

		
			if (av_bsf_send_packet(h264bsfc, &pkt) < 0 || av_bsf_receive_packet(h264bsfc, &pkt) < 0)
			{
				printf("Failed to filter packet\n");
				// Handle error
			}
		ret = av_interleaved_write_frame(fc->g_OutFmt_Ctx[VeChn], &pkt);
		// printf("pkt,stream_index=%d\n",pkt.stream_index);
		if (ret < 0)
		{
		    printf("cannot write video frame\n");
		    return HI_FAILURE;
		}

		
    }
	av_bsf_free(&h264bsfc);
	return HI_SUCCESS;
}
HI_S32 HI_ADD_SPS_PPS_AVI(VENC_CHN VeChn, uint8_t *buf, uint32_t size, FfmpegConf *fc)
{
	HI_S32 ret;
	ret = HI_PDT_Add_Stream_AVI(VeChn, fc); // 创建一个新流并添加到当前AVFormatContext中
	if (ret < 0)
	{
		printf("HI_PDT_Add_Stream faild\n");
		goto Add_Stream_faild;
	}

	fc->g_OutFmt_Ctx[VeChn]->streams[fc->vi[VeChn]]->codecpar->extradata_size = size;
	fc->g_OutFmt_Ctx[VeChn]->streams[fc->vi[VeChn]]->codecpar->extradata = (uint8_t *)av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
	memcpy(fc->g_OutFmt_Ctx[VeChn]->streams[fc->vi[VeChn]]->codecpar->extradata, buf, size); // 写入SPS和PPS
    
	
	ret = avformat_write_header(fc->g_OutFmt_Ctx[VeChn], NULL); // 写文件头
	if (ret < 0)
	{
		printf("avformat_write_header faild\n");
		goto write_header_faild;
	}
	printf("write head\n");
	

	return HI_SUCCESS;

write_header_faild:
	if (fc->g_OutFmt_Ctx[VeChn] && !(fc->g_OutFmt_Ctx[VeChn]->flags & AVFMT_NOFILE))
		avio_close(fc->g_OutFmt_Ctx[VeChn]->pb);

Add_Stream_faild:
	if (NULL != fc->g_OutFmt_Ctx[VeChn])
		avformat_free_context(fc->g_OutFmt_Ctx[VeChn]);
	fc->vi[VeChn] = -1;
	fc->VptsInc[VeChn] = 0;
	fc->b_First_IDR_Find[VeChn] = 0; // sps，pps帧标志清除
	return HI_FAILURE;
}
HI_S32 HI_PDT_Add_Stream_AVI(VENC_CHN VeChn, FfmpegConf *fc)
{
	AVOutputFormat *pOutFmt = NULL;			//用于获取AVFormatContext->Format
    AVCodecParameters *vAVCodecPar = NULL;	//新替代参数AVStream->CodecPar
    AVStream *vAVStream = NULL;				//用于指向新建的视频流
	AVCodec *vcodec = NULL;					//用于指向视频编码器
	//const char *in_filename = "temp.avi";  // Input file URL
	AVFormatContext *ifmt_ctx = NULL;
	int ret, i;

	pOutFmt = fc->g_OutFmt_Ctx[VeChn]->oformat;	//输出Format
	
    //查找视频编码器
    vcodec = avcodec_find_encoder(pOutFmt->video_codec);
    // vcodec = avcodec_find_encoder(AV_CODEC_ID_H264); 
    if (NULL == vcodec)
    {
        printf("could not find video encoder H264\n");
        return -1;
    }
	// ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0);
	// ret = avformat_find_stream_info(ifmt_ctx, 0);
	// printf("ifmt_ctx->nb_streams=%d\n",ifmt_ctx->nb_streams);

	// for (i = 0; i < ifmt_ctx->nb_streams; i++){
	//根据视频编码器信息（H264），在AVFormatContext里新建视频流通道
    vAVStream = avformat_new_stream(fc->g_OutFmt_Ctx[VeChn], vcodec);	
	// vAVStream = avformat_new_stream(fc->g_OutFmt_Ctx[VeChn], NULL);	
    if (NULL == vAVStream)
    {
       printf("could not allocate vcodec stream\n");
       return -1;
    }
    //给新建的视频流一个ID，0
    vAVStream->id = fc->g_OutFmt_Ctx[VeChn]->nb_streams-1;	//nb_streams是当前AVFormatContext里面流的数量
    vAVCodecPar = vAVStream->codecpar;
	fc->vi[VeChn] = vAVStream->index;	//获取视频流的索引号

    // AVStream *in_stream = ifmt_ctx->streams[0];
	// avcodec_parameters_copy(vAVCodecPar, in_stream->codecpar);

	// 对视频流的参数设置
	vAVCodecPar->codec_type = AVMEDIA_TYPE_VIDEO;
	vAVCodecPar->codec_id = AV_CODEC_ID_H264;
	vAVCodecPar->bit_rate = 0; // kbps，好像不需要
	vAVCodecPar->width = 400;  // 像素
	vAVCodecPar->height = 400;
	vAVStream->time_base = (AVRational){1, STREAM_FRAME_RATE}; // 时间基:1/STREAM_FRAME_RATE
	vAVCodecPar->format = AV_PIX_FMT_YUV420P;
	

	


	
	vAVStream->codecpar->codec_tag = 0;
	if (pOutFmt->flags & AVFMT_GLOBALHEADER)
	vAVStream->codecpar->codec_id |= AV_CODEC_FLAG_GLOBAL_HEADER;
	avformat_close_input(&ifmt_ctx);
	// }

	return HI_SUCCESS;
}
HI_VOID HI_PDT_CloseAVI(VENC_CHN VeChn, FfmpegConf *fc)
{
	int ret;
	if (fc->g_OutFmt_Ctx[VeChn])
	{
		ret = av_write_trailer(fc->g_OutFmt_Ctx[VeChn]); // 写文件尾
		if (ret < 0)
		{
#ifdef DEBUG
			printf("av_write_trailer faild\n");
#endif
		}
	}
	if (fc->g_OutFmt_Ctx[VeChn] && !(fc->g_OutFmt_Ctx[VeChn]->oformat->flags & AVFMT_NOFILE)) // 文件状态检测
	{
		ret = avio_close(fc->g_OutFmt_Ctx[VeChn]->pb); // 关闭文件
		if (ret < 0)
		{
#ifdef DEBUG
			printf("avio_close faild\n");
#endif
		}
	}
	if (fc->g_OutFmt_Ctx[VeChn])
	{
		avformat_free_context(fc->g_OutFmt_Ctx[VeChn]); // 释放结构体
		fc->g_OutFmt_Ctx[VeChn] = NULL;
	}
	// 清除相关标志
	fc->vi[VeChn] = -1;
	fc->VptsInc[VeChn] = 0;
	fc->Vfirst[VeChn] = 0;
	fc->b_First_IDR_Find[VeChn] = 0;
}
