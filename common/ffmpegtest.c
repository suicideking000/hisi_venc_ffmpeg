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

// typedef struct FfmpegConf
// {
// 	AVFormatContext* g_OutFmt_Ctx[VENC_MAX_CHN_NUM];	//每个通道的AVFormatContext
// 	int vi[VENC_MAX_CHN_NUM];							//视频流索引号
// 	HI_BOOL b_First_IDR_Find[VENC_MAX_CHN_NUM];			//第一帧是I帧标志
// 	long int VptsInc[VENC_MAX_CHN_NUM];					//用于视频帧递增计数
// 	HI_U64 Video_PTS[VENC_MAX_CHN_NUM];					//视频PTS
// 	HI_U64 Vfirst[VENC_MAX_CHN_NUM];					//视频第一帧标志
// 	char filename[VENC_MAX_CHN_NUM][1024];				//文件名
// }FfmpegConf;

HI_S32 HI_PDT_CreateMp4(VENC_CHN VeChn, char *pfile, FfmpegConf *fc)
{
	int ret = 0;
	char pszFileName[256] = {0};	// 保存文件名
	AVOutputFormat *pOutFmt = NULL; // 输出Format指针
	
	sprintf(pszFileName, "%s1.avi", pfile);

	// av_register_all();                      //注册所有编解码器、复用/解复用组件

	// 初始化输出视频码流的AVFormatContext
	avformat_alloc_output_context2(&(fc->g_OutFmt_Ctx[VeChn]), NULL, "avi", pszFileName);
	if (NULL == fc->g_OutFmt_Ctx[VeChn]) // 失败处理
	{
		printf("Could not deduce output format from file extension: using mp4. \n");
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
		printf("pOutfmt->video_codec=%d\n",pOutFmt->video_codec);
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
HI_S32 HI_PDT_WriteVideo(VENC_CHN VeChn, VENC_STREAM_S *pstStream, FfmpegConf *fc)
{
	unsigned int i = 0;
	int frame_index =1;
	unsigned char* pPackVirtAddr = NULL;	//码流首地址
	unsigned int u32PackLen = 0;			//码流长度
    int ret = 0;
    AVStream *Vpst = NULL; 					//视频流指针
    AVPacket pkt;			//音视频包结构体，这个包不是海思的包，填充之后，用于最终写入数据
    AVPacket pkt1;
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
						ret = HI_ADD_SPS_PPS(VeChn, sps_pps_buf, sps_len+pps_len, fc);
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
						ret = HI_ADD_SPS_PPS(VeChn, sps_pps_buf, sps_len+pps_len, fc); 
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
		//把AVpack包写入mp4

		
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
HI_S32 HI_ADD_SPS_PPS(VENC_CHN VeChn, uint8_t *buf, uint32_t size, FfmpegConf *fc)
{
	HI_S32 ret;
	ret = HI_PDT_Add_Stream(VeChn, fc); // 创建一个新流并添加到当前AVFormatContext中
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
HI_S32 HI_PDT_Add_Stream(VENC_CHN VeChn, FfmpegConf *fc)
{
	AVOutputFormat *pOutFmt = NULL;			//用于获取AVFormatContext->Format
    AVCodecParameters *vAVCodecPar = NULL;	//新替代参数AVStream->CodecPar
    AVStream *vAVStream = NULL;				//用于指向新建的视频流
	AVCodec *vcodec = NULL;					//用于指向视频编码器
	const char *in_filename = "test.mp4";  // Input file URL
	AVFormatContext *ifmt_ctx = NULL;
	int ret, i;

	pOutFmt = fc->g_OutFmt_Ctx[VeChn]->oformat;	//输出Format
	
    //查找视频编码器，MP4格式输出时默认值为 AV_CODEC_ID_MPEG4
    vcodec = avcodec_find_encoder(pOutFmt->video_codec);
    // vcodec = avcodec_find_encoder(AV_CODEC_ID_H264); 
    if (NULL == vcodec)
    {
        printf("could not find video encoder H264\n");
        return -1;
    }
	ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0);
	ret = avformat_find_stream_info(ifmt_ctx, 0);
	printf("ifmt_ctx->nb_streams=%d\n",ifmt_ctx->nb_streams);

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


	

	// 对视频流的参数设置
	vAVCodecPar->codec_type = AVMEDIA_TYPE_VIDEO;
	vAVCodecPar->codec_id = AV_CODEC_ID_H264;
	vAVCodecPar->bit_rate = 0; // kbps，好像不需要
	vAVCodecPar->width = 400;  // 像素
	vAVCodecPar->height = 400;
	vAVStream->time_base = (AVRational){1, STREAM_FRAME_RATE}; // 时间基:1/STREAM_FRAME_RATE
	vAVCodecPar->format = AV_PIX_FMT_YUV420P;
	

	AVStream *in_stream = ifmt_ctx->streams[0];


	avcodec_parameters_copy(vAVCodecPar, in_stream->codecpar);
	vAVStream->codecpar->codec_tag = 0;
	if (pOutFmt->flags & AVFMT_GLOBALHEADER)
	vAVStream->codecpar->codec_id |= AV_CODEC_FLAG_GLOBAL_HEADER;
	avformat_close_input(&ifmt_ctx);
	// }

	return HI_SUCCESS;
}
HI_VOID HI_PDT_CloseMp4(VENC_CHN VeChn, FfmpegConf *fc)
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
HI_S32 HI_MP4toAVI(void)
{
	AVOutputFormat *ofmt = NULL;
	// Input AVFormatContext and Output AVFormatContext
	AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
	AVPacket pkt;
	const char *in_filename, *out_filename;
	int ret, i;
	int frame_index = 0;
	int flag=0;
	in_filename = "test.mp4";  // Input file URL
	out_filename = "test.avi"; // Output file URL
	AVBSFContext *h264bsfc;
	const AVBitStreamFilter *filter = av_bsf_get_by_name("h264_mp4toannexb");
	ret = av_bsf_alloc(filter, &h264bsfc);
	// Input
	if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0)
	{ // 打开媒体文件
		printf("Could not open input file.");
		flag=-1;
		goto end;
	}
	if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0)
	{ // 获取视频信息
		printf("Failed to retrieve input stream information");
		flag=-1;
		goto end;
	}
	av_dump_format(ifmt_ctx, 0, in_filename, 0);
	// 初始化输出视频码流的AVFormatContext。
	avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename);
	if (!ofmt_ctx)
	{
		printf("Could not create output context\n");
		ret = AVERROR_UNKNOWN;
		flag=-1;
		goto end;
	}
	ofmt = ofmt_ctx->oformat;
	for (i = 0; i < ifmt_ctx->nb_streams; i++)
	{
		// Create output AVStream according to input AVStream
		AVStream *in_stream = ifmt_ctx->streams[i];
		AVStream *out_stream = avformat_new_stream(ofmt_ctx, NULL); // 初始化AVStream
		if (!out_stream)
		{
			printf("Failed allocating output stream\n");
			ret = AVERROR_UNKNOWN;
			flag=-1;
			goto end;
		}
		// 关键：Copy the settings of AVCodecContext
		if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0)
		{
			printf("Failed to copy context from input to output stream codec context\n");
			flag=-1;
			goto end;
		}
		else
		{
			avcodec_parameters_copy(h264bsfc->par_in, in_stream->codecpar);
			av_bsf_init(h264bsfc);
		}
		out_stream->codecpar->codec_tag = 0;
		if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
			out_stream->codecpar->codec_id |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
	// Output information------------------
	av_dump_format(ofmt_ctx, 0, out_filename, 1);
	// Open output file
	if (!(ofmt->flags & AVFMT_NOFILE))
	{
		ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE); // 打开输出文件。
		if (ret < 0)
		{
			printf("Could not open output file '%s'", out_filename);
			flag=-1;
			goto end;
		}
	}
	// Write file header
	if (avformat_write_header(ofmt_ctx, NULL) < 0)
	{
		printf("Error occurred when opening output file\n");

		goto end;
	}
	while (1)
	{
		AVStream *in_stream, *out_stream;
		// Get an AVPacket
		ret = av_read_frame(ifmt_ctx, &pkt);
		if (ret < 0)
			break;
		in_stream = ifmt_ctx->streams[pkt.stream_index];
		out_stream = ofmt_ctx->streams[pkt.stream_index];
		// Convert PTS/DTS
		pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
		pkt.pos = -1;
		printf("pkt,stream_index=%d\n",pkt.stream_index);
		if (pkt.stream_index == 0)
		{

			if (av_bsf_send_packet(h264bsfc, &pkt) < 0 || av_bsf_receive_packet(h264bsfc, &pkt) < 0)
			{
				printf("Failed to filter packet\n");
				// Handle error
			}
		}
		// Write
		if (av_interleaved_write_frame(ofmt_ctx, &pkt) < 0)
		{ // 将AVPacket（存储视频压缩码流数据）写入文件
			printf("Error muxing packet\n");
			break;
		}
		printf("Write %8d frames to output file\n", frame_index);
		av_packet_unref(&pkt);
		frame_index++;
	}
	// Write file trailer
	av_write_trailer(ofmt_ctx);
end:
	av_bsf_free(&h264bsfc);
	avformat_close_input(&ifmt_ctx);
	/* close output */
	if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
		avio_close(ofmt_ctx->pb);
	avformat_free_context(ofmt_ctx);
	return flag;
}
HI_S32 HI_MP4toYUV420P(void)
{
    char filepath[] = "test1.avi";       //文件名称
    avformat_network_init();
    AVFormatContext* pFormatCtx = avformat_alloc_context(); //为AVFormatContext开辟内存
    if (avformat_open_input(&pFormatCtx, filepath, NULL, NULL) != 0) {    //打开视频文件
        return -1;
    }
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {   //获取视频流信息
        return -1;
    }
    int videoindex = -1;    //视频的位置索引
    videoindex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);//查找视频流索引
    if (videoindex == -1) { //没有找到视频流
        return -1;
    }
    AVCodecContext* pCodecCtx = avcodec_alloc_context3(NULL);//申请内存
    if (pCodecCtx == NULL) {
        return -1;
    }
    avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoindex]->codecpar); //获取视频流中的编解码上下文
    AVCodec* pCodec = avcodec_find_decoder(pCodecCtx->codec_id);            //根据指定视频流id找解码器
    if (pCodec == NULL) {
        return -1;
    }
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {    //打开解码器
        return -1;
    }
    AVFrame* pFrame = av_frame_alloc();     //解码之前的数据
    AVFrame* pFrameYUV = av_frame_alloc();  //解码之后的数据
    uint8_t* out_buffer = (uint8_t*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
        pCodecCtx->width,pCodecCtx->height,1));//缓冲数组（AVFrame中的data指向的内存）
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer, AV_PIX_FMT_YUV420P,
        pCodecCtx->width, pCodecCtx->height,1);//缓冲数组和pFrameYUV联系起来
    AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));  //压缩的数据
    //用于转码的参数
    struct SwsContext* img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
        pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);//格式转换上下文
    int frame_cnt =0;
    int ret, got_picture,size;
    FILE* fp_yuv = fopen("output.yuv", "wb+");
    while (av_read_frame(pFormatCtx, packet) >= 0) 
	{    //从流中读一帧
        if (packet->stream_index == videoindex) {   //判断是否为视频流
            ret = avcodec_send_packet(pCodecCtx, packet);//解码一帧视频压缩数据
            if (ret < 0) 
			{
                return -1;
            }
            while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) 
			{ //一个packet可能包含多个frame
                    sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0,
                        pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);//图像每行有多余的像素
                    
                    frame_cnt++;
                    size = pCodecCtx->width*pCodecCtx->height;
                    fwrite(pFrameYUV->data[0], 1, size, fp_yuv);
                    fwrite(pFrameYUV->data[1], 1, size / 4, fp_yuv);
                    fwrite(pFrameYUV->data[2], 1, size / 4, fp_yuv);
					printf("Write %8d frames to output file\n", frame_cnt);
            }           
        }
        av_packet_unref(packet);
    }
    sws_freeContext(img_convert_ctx);
    av_frame_free(&pFrame);
    avcodec_close(pCodecCtx);   //关闭解码器
    avformat_close_input(&pFormatCtx);  //关闭输入视频文件
	return 0;
}