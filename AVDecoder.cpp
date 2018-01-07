#include <QFile>
#include <QDateTime>
#include <QDebug>


#include "AVDecoder.h"

extern "C"
{
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>


    #include <libavfilter/avfiltergraph.h>
    #include <libavfilter/buffersink.h>
    #include <libavfilter/buffersrc.h>
    #include <libavutil/opt.h>

    #include <libswresample/swresample.h>
}

class  AVDecoder_p{

public :
    AVDecoder_p()
        : mVideoCodec(NULL)
        , mVideoCodecCtx(NULL)
        , mFrame(NULL)
        , mVideoCodecParserCtx(NULL)
        , mPkt(NULL)
        , mIsUseHardDecode(false)
        , mEnableHardDecode(false)
    {
    }

    static bool isSupportH265HW(){
        return false;
    }


    static bool isSupportH264HW(){
        return false;
    }

    /** 是否启用硬解 */
    void setEnableHW(bool isEnable){
        mEnableHardDecode = isEnable;
    }

    /** 初使化 */
    bool init(VideoCodecFormat vFormat,int ThreadCount_n4){
        av_register_all(); //注册ffmpeg所有组件
        av_log_set_callback(NULL);//不打印日志


        if(vFormat == VideoCodecFormat_H264){
            mVideoCodecId = AV_CODEC_ID_H264;
        }else if(vFormat == VideoCodecFormat_H265){
            mVideoCodecId = AV_CODEC_ID_HEVC;
        }else{
            return false;
        }

        //寻找对应的解码器
        if(mVideoCodec == NULL){
            mVideoCodec = avcodec_find_decoder(mVideoCodecId);
        }

        if(mVideoCodec == NULL) {
            return false;
        }
        //end 寻找对应的解码器

        //创建解码器上下文
        if(mVideoCodecCtx == NULL)
            mVideoCodecCtx = avcodec_alloc_context3(mVideoCodec);

        if (!mVideoCodecCtx){
            return false;
        }
        //end 创建解码器上下文

        if(ThreadCount_n4 > 5){
            ThreadCount_n4 = 5;
        }

        if(ThreadCount_n4 > 0){
            mVideoCodecCtx->thread_count = ThreadCount_n4;
        }


        //初使化视频包解析器
        if(mVideoCodecParserCtx == NULL){
            mVideoCodecParserCtx = av_parser_init(mVideoCodecId);
        }
        if(mVideoCodecParserCtx == NULL){
            if(mVideoCodecCtx != NULL)
                avcodec_free_context(&mVideoCodecCtx);
            mVideoCodecCtx = NULL;
            return false;
        }
        //end 初使化视频包解析器




        //打开解码器
        if(avcodec_open2(mVideoCodecCtx, mVideoCodec, NULL) < 0) {
            if(mVideoCodecParserCtx != NULL)
                av_parser_close(mVideoCodecParserCtx);
            if(mVideoCodecCtx != NULL)
                avcodec_free_context(&mVideoCodecCtx);
            mVideoCodecCtx = NULL;
            mVideoCodecParserCtx = NULL;
            return false;
        }
        //end 打开解码器

        mFrame = av_frame_alloc();
        mPkt = av_packet_alloc();
        return true;
    }

    /** 解码 */
    bool decodec(const char *buffer,int size,Decoded_YV12 &YV12_ro){
        if(buffer == NULL || size <= 0){
            YV12_ro.DecodedSize_n4 = -1;//表示码流非法
            return false;
        }
        YV12_ro.DecodedSize_n4 = 0;//表示数据不足
        int pos = 0;
        bool isDecoded = false;
        while(pos < size){
            int len = av_parser_parse2(mVideoCodecParserCtx,
                                       mVideoCodecCtx,
                                       &mPkt->data, &mPkt->size,
                                       (uint8_t *)buffer + pos, size - pos,
                                       AV_NOPTS_VALUE,
                                       AV_NOPTS_VALUE,
                                       0);
            pos += len;
            if(mPkt->size > 0){
                int ret = avcodec_send_packet(mVideoCodecCtx, mPkt);
                if(ret != 0){
                    qDebug() << "decoder error code : " << ret;
                    YV12_ro.DecodedSize_n4 = -1;
                    av_packet_unref(mPkt);
                    continue;
                }

                while(avcodec_receive_frame(mVideoCodecCtx, mFrame) == 0){
                    YV12_ro.Width_n4 = mFrame->width;
                    YV12_ro.Height_n4 = mFrame->height;
                    YV12_ro.YData_pu1 = mFrame->data[0];
                    YV12_ro.UData_pu1 = mFrame->data[1];
                    YV12_ro.VData_pu1 = mFrame->data[2];
                    YV12_ro.YLineSize_n4 = mFrame->linesize[0];
                    YV12_ro.ULineSize_n4 = mFrame->linesize[1];
                    YV12_ro.VLineSize_n4 = mFrame->linesize[2];
                    YV12_ro.DecodedSize_n4 = mPkt->size;//表示解码成功
                    isDecoded = true;
                }
            }
        }
        return isDecoded;
    }

    ~AVDecoder_p(){
        if(mFrame != NULL){
            av_frame_unref(mFrame);
            av_frame_free(&mFrame);
        }

        if(mVideoCodecParserCtx != NULL){
            av_parser_close(mVideoCodecParserCtx);
        }

        if(mVideoCodec != NULL)
            av_free(mVideoCodec);

        if(mVideoCodecCtx != NULL){
            avcodec_close(mVideoCodecCtx);
            avcodec_free_context(&mVideoCodecCtx);
        }

        if(mPkt != NULL){
            av_packet_unref(mPkt);
            av_packet_free(&mPkt);
        }
    }

public:
    AVCodecParserContext *mVideoCodecParserCtx;
    AVCodecID mVideoCodecId;
    AVFrame *mFrame;
    AVPacket *mPkt;
    AVCodec *mVideoCodec;
    AVCodecContext *mVideoCodecCtx;
    bool mIsUseHardDecode;//是否支持硬解
    DecodeContext mDecode;
    bool mEnableHardDecode;
};



/** 打开解码器，返回解码器的操作句柄 */
extern "C" __declspec(dllexport)
unsigned int Open_Decoder_H265(int ThreadCount_n4 = 1){
    AVDecoder_p *decoder = new AVDecoder_p();
    decoder->setEnableHW(false);
    if(decoder->init(VideoCodecFormat_H265,ThreadCount_n4)){
        return (unsigned int)decoder;
    }
    delete decoder;
    return 0;
}


/** 打开解码器，返回解码器的操作句柄 */
extern "C" __declspec(dllexport)
unsigned int Open_Decoder_H264(int ThreadCount_n4 = 1){
    AVDecoder_p *decoder = new AVDecoder_p();
    decoder->setEnableHW(false);
    if(decoder->init(VideoCodecFormat_H264,ThreadCount_n4)){
        return (unsigned int)decoder;
    }
    delete decoder;
    return false;
}

/** 关闭解码器 */
extern "C" __declspec(dllexport)
void Close_Decoder(unsigned int *Decoder_u4i){
    if(*Decoder_u4i != NULL){
        AVDecoder_p *decoder = (AVDecoder_p *)*Decoder_u4i;
        delete decoder;
        *Decoder_u4i = NULL;
    }
}

/**
 * @brief Decode_Frame
 * 解码h265
 * @param Decoder_u4i解码器句柄
 * @param Stream_pu1i 需要解码的数据
 * @param StreamSize_n4i 需要解码数据的长度
 * @param YV12_ro 解码后的yuv数据
 */
extern "C" __declspec(dllexport)
bool Decode_Frame(unsigned int Decoder_u4i,unsigned char *Stream_pu1i,int StreamSize_n4i,Decoded_YV12 &YV12_ro){
    if(Decoder_u4i != NULL){
        AVDecoder_p *decoder = (AVDecoder_p *)Decoder_u4i;
        return decoder->decodec((char *)Stream_pu1i,StreamSize_n4i,YV12_ro);
    }
    return false;
}



int main(){
//    av_register_all(); //注册ffmpeg所有组件
//    AVHWAccel* p_AVHWAccel = NULL;
//    do
//    {
//        p_AVHWAccel = av_hwaccel_next(p_AVHWAccel); //列举出所有的硬解类型
//        if(NULL != p_AVHWAccel)
//        {
//            qDebug() << "name : " << p_AVHWAccel->name;
//        }
//    } while(NULL != p_AVHWAccel);
//    qDebug()<< avcodec_find_decoder_by_name("h264");
//    qDebug() << avcodec_find_decoder(AV_CODEC_ID_H264);
//    return 0;
    QFile file;
    file.setFileName("F:/rtsp/1.265");
    file.open(QFile::ReadOnly);
    char buffer[1024] = {0};
    int h265id = Open_Decoder_H265();

    while(true){
        file.read(buffer,1024);
        Decoded_YV12 yv12;
        memset(&yv12,0,sizeof(Decoded_YV12));
        qint64 begin = QDateTime::currentMSecsSinceEpoch();
        Decode_Frame(h265id,(unsigned char *)buffer,1024,yv12);
        if(yv12.DecodedSize_n4 > 0)
            qDebug() << "haoshi : " << (QDateTime::currentMSecsSinceEpoch() - begin);
//            qDebug() << yv12.DecodedSize_n4 << ":" << yv12.Width_n4 << ":" << yv12.Height_n4;
    }
    qDebug() << "----------------------------";
    return 0;
}
