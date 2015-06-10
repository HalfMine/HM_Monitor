/**
 * 杭州慧牧科技有限公司
 * 0571-88513715
 *
 * 作者：郑腾飞
 * 1195800652＠qq.com
 *
 * 本程序用于实现将大华监控视频实时转换成HLS可用的ts流
 * 基于dhnetsdk, avformat, avcodec, avutil等库
 * 2015年6月8日
 */

#define __STDC_CONSTANT_MACROS
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <mysql/mysql.h>
#include "dhnetsdk.h"
#include "Profile.h"
extern "C"
{
    /* ffmpeg库为标准C程序开发 */
    #include <libavformat/avformat.h>
}

typedef struct
{
    char devName[32];         // 设备名称
    char devIp[32];               // 设备IP
    unsigned short devPort;  // 设备端口
    char devUser[32];           // 登录用户
    char devPwd[32];            // 登录密码
    long loginHandle;            // 登录句柄
    long channelHandle[16]; // 通道句柄
    bool onlineFlag;              // 在线标识
    char *errorStr = NULL;    // 出错原因
    long channelFlag[16];     // 通道状态
} Hm_Device_Info;

pthread_t pthread[16];       // 16个进程，存放进程句柄
pthread_mutex_t mut;        // 进程互斥锁
Hm_Device_Info *hm_Device_Info = new Hm_Device_Info;
int channel = 15;               // 初始化当前通道
int connectNum = 0;          // connect number, should less than 4? or 8? that according to the server ability and net speed.
char videoPathStr[100] = {0};    // 视频缓存路径，如："/home/master/dev/getRealVideo/";
char m3u8UrlStr[100] = {0};     // m3u8配置，如："http://192.168.1.186:8080/";
char *videoPath = videoPathStr;
char *m3u8Url = m3u8UrlStr;

/* 监控设备结构体初始化 */
void InitDecviceInfo(Hm_Device_Info &stDeviceInfo)
{
    stDeviceInfo.devPort = 0;
    stDeviceInfo.loginHandle = 0;
    stDeviceInfo.onlineFlag = false;
    stDeviceInfo.errorStr = NULL;
    memset(&stDeviceInfo.devName, 0, 32);
    memset(&stDeviceInfo.devIp, 0, 32);
    memset(&stDeviceInfo.devUser, 0, 32);
    memset(&stDeviceInfo.devPwd , 0, 32);
    memset(&stDeviceInfo.channelHandle, 0, 16);
    memset(&stDeviceInfo.channelFlag, 0, 16);
}

/* 错误翻译 */
void ChangeLoginError(int nErrorCode , char **strErrorCode)
{
    switch(nErrorCode)
    {
    case 0:
        *strErrorCode = "Login Success";
        break;
    case 1:
        *strErrorCode = "Login Password Error";
        break;
    case 2:
        *strErrorCode = "User Is Not Exist";
        break;
    case 3:
        *strErrorCode = "Login Timeout";
        break;
    case 4:
        *strErrorCode = "Repeat Login";
        break;
    case 5:
        *strErrorCode = "User Account is Locked";
        break;
    case 6:
        *strErrorCode = "User In Blacklist";
        break;
    case 7:
        *strErrorCode = "Device Busy";
        break;
    case 8:
        *strErrorCode = "Sub Connect Failed";
        break;
    case 9:
        *strErrorCode = "Host Connect Failed";
        break;
    case 10 :
        *strErrorCode = "Max Connect";
        break;
    case 11:
        *strErrorCode = "Support Protocol3 Only";
        break;
    case 12:
        *strErrorCode = "UKey Info Error";
        break;
    case 13:
        *strErrorCode = "No Authorized";
        break;
    default:
        break;
    }
}

/* 参见大华SDK */
void CALLBACK AutoConnectFunc(LLONG lLoginID,char *pchDVRIP,LONG nDVRPort, LDWORD dwUser)
{
    // Recognize which device has just disconnect according to the pchDVRIP. Change this device's loginHandle then.
    printf("Reconnect success.\n");
}

void CALLBACK DisConnectFunc(LLONG lLoginID, char *pchDVRIP, LONG nDVRPort, LDWORD dwUser)
{
}

/* 读取配置文件 */
int LoadConfig(Hm_Device_Info &deviceInfo, char *filePath, char *videoPath, char *m3u8Url)
{
    char szSection[128];
    char ip[32];
    char port[8];
    char thread_num[8] = {0};
    char videopath[100] = {0};
    char m3u8url[100] = {0};
    int flag = 0;

    getcwd(videopath, sizeof(videopath));
    getcwd(m3u8url, sizeof(m3u8url));

    CProfile::GetPrivateProfileString("thread", "threadnum", "", thread_num, 8, filePath);
    int nThreadNum = atoi(thread_num);
    for(int i = 0; i < nThreadNum; i++)
    {
        for(int j = 0; ; j++)
        {
            sprintf(szSection, "thread%d.device%d", i, j);
            int nSize = CProfile::GetPrivateProfileString(szSection, "IP", "", ip, 32, filePath);
            if(strcmp(ip, "") == 0)
            {
                break;
            }
            else
            {
                strcpy(deviceInfo.devIp, ip);
            }
            nSize = CProfile::GetPrivateProfileString(szSection, "Port", "", port, 8, filePath);
            nSize = CProfile::GetPrivateProfileString(szSection, "Username", "", deviceInfo.devUser, 32, filePath);
            nSize = CProfile::GetPrivateProfileString(szSection, "Password", "", deviceInfo.devPwd, 32, filePath);
            nSize = CProfile::GetPrivateProfileString(szSection, "videoPath", "",videopath, 100, filePath);
            nSize = CProfile::GetPrivateProfileString(szSection, "m3u8Url", "", m3u8url, 100, filePath);
            nSize = CProfile::GetPrivateProfileString(szSection, "Name", "", deviceInfo.devName, 32, filePath);
            deviceInfo.devPort = atoi(port);
            strcpy(videoPath, videopath);
            strcpy(m3u8Url, m3u8url);
            flag ++;
        }
    }
    if (flag < 1)
    {
        return 0;
    }
    return 1;
}

/* 本地流处理、转封装 */
float TsStreamRemuxer(char *in_filename, char *out_filename)
{
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVPacket pkt;
    int ret, i;

    char transition_file[100] = {0};
    sprintf(transition_file, "%s%s", in_filename, ".h264");

    char h264_head[] = {0x00, 0x00, 0x00, 0x01};
    char dh_head[] = {0x44, 0x48, 0x41, 0x56};
    char dh_end[] = {0x64, 0x68, 0x61, 0x76};

    FILE *p_in_file, *p_trans_file;
    char *buffer;
    int file_size;
    int test_head_size = 48;
    int read_size, write_size;
    int dh_frame_size, dh_head_size;
    int frame_size;
    long seek_size = 0;
    float ts_Duration = 0.00;

    if((p_in_file = fopen(in_filename, "rb")) == NULL)
    {
        printf("Open in_file error\n");
        return 0;
    }
    p_trans_file = fopen(transition_file, "ab");
    if(p_trans_file == NULL)
    {
        printf("New transition file error\n");
        return -1;
    }
    fseek (p_in_file , 0 , SEEK_END);
    file_size = ftell (p_in_file);
    rewind (p_in_file);
    while(seek_size < file_size)
    {
        fseek(p_in_file, seek_size, SEEK_SET);
        buffer = (char*)malloc(sizeof(char) * 65535);
        if(buffer == NULL)
        {
            printf("Memory error\n");
            fclose(p_trans_file);
            fclose(p_in_file);
            return -1;
        }
        read_size = fread(buffer, 1, test_head_size, p_in_file);
        printf("The head is %X %X %X %X\n", buffer[0], buffer[1], buffer[2], buffer[3]);
        if(read_size != 48)
        {
            printf ("Read head error\n");
            free(buffer);
            fclose(p_trans_file);
            fclose(p_in_file);
            return -1;
        }
        if((buffer[0] == dh_head[0]) && (buffer[1] == dh_head[1]) && (buffer[2] == dh_head[2]) && (buffer[3] == dh_head[3]))
        {
            printf("Find dh head\n");
        }
        else
        {
            printf("Head context error\n");
            free(buffer);
            fclose(p_trans_file);
            fclose(p_in_file);
            return -1;
        }

        /* 计算帧长度，注意补码问题 */
        printf("The length is %X %X %X %X\n", buffer[15], buffer[14], buffer[13], buffer[12]);
        if(buffer[13] >= 0 && buffer[12] >= 0)
        {
            dh_frame_size = buffer[13] * 0x0100 + buffer[12];
        }
        else if(buffer[13] >= 0 && buffer[12] < 0)
        {
            dh_frame_size = buffer[13] * 0x0100 + buffer[12] + 0x0100;
        }
        else if(buffer[13] < 0 && buffer[12] >= 0)
        {
            dh_frame_size = (buffer[13] + 0x0100) * 0x0100 + buffer[12];
        }
        else
        {
            dh_frame_size = (buffer[13] + 0x0100)  * 0x0100 + buffer[12] + 0x0100;
        }

        /* 查找264头 */
        if((buffer[28] == h264_head[0]) && (buffer[29] == h264_head[1]) && (buffer[30] == h264_head[2]) && (buffer[31] == h264_head[3]))
        {
            frame_size = dh_frame_size - 36;
            dh_head_size = 28;
            fseek(p_in_file, dh_head_size - test_head_size, SEEK_CUR);
        }
        else if((buffer[32] == h264_head[0]) && (buffer[33] == h264_head[1]) && (buffer[34] == h264_head[2]) && (buffer[35] == h264_head[3]))
        {
            frame_size = dh_frame_size - 40;
            dh_head_size = 32;
            fseek(p_in_file, dh_head_size - test_head_size, SEEK_CUR);
        }
        else if((buffer[36] == h264_head[0]) && (buffer[37] == h264_head[1]) && (buffer[38] == h264_head[2]) && (buffer[39] == h264_head[3]))
        {
            frame_size = dh_frame_size - 44;
            dh_head_size = 36;
            fseek(p_in_file, dh_head_size - test_head_size, SEEK_CUR);
        }
        else if((buffer[40] == h264_head[0]) && (buffer[41] == h264_head[1]) && (buffer[42] == h264_head[2]) && (buffer[43] == h264_head[3]))
        {
            frame_size = dh_frame_size - 48;
            dh_head_size = 40;
            fseek(p_in_file, dh_head_size - test_head_size, SEEK_CUR);
        }
        else if(buffer[4] == 0xFFFFFFF1)
        {
            /* 对大华辅助帧的处理：丢弃 */
            printf("Found dahua fuzhu frame........................\n");
            fseek(p_in_file, dh_frame_size - test_head_size, SEEK_CUR);
            seek_size += dh_frame_size;
            free(buffer);
            continue;
        }

        read_size = fread(buffer, 1, frame_size + 8, p_in_file);
        if(read_size != frame_size + 8)
        {
            printf ("Read h264 error\n");
            free(buffer);
            fclose(p_trans_file);
            fclose(p_in_file);
            return -1;
        }
        printf("The 264head is %X %X %X %X\n", buffer[0], buffer[1], buffer[2], buffer[3]);
        printf("The end is %X %X %X %X\n", buffer[frame_size], buffer[frame_size + 1], buffer[frame_size + 2], buffer[frame_size + 3]);

        write_size = fwrite(buffer, 1, frame_size, p_trans_file);
        if(write_size != frame_size)
        {
            printf("Write h264 error\n");
            free(buffer);
            fclose(p_trans_file);
            fclose(p_in_file);
            return -1;
        }
        seek_size += dh_frame_size;
        free(buffer);
    }
    fclose(p_trans_file);
    fclose(p_in_file);

    av_register_all();
    AVBitStreamFilterContext* h264bsfc =  av_bitstream_filter_init("h264_mp4toannexb");

    if ((ret = avformat_open_input(&ifmt_ctx, transition_file, 0, 0)) < 0)
    {
        printf("Could not open input file '%s'", transition_file);
        goto end;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0)
    {
        printf("Failed to retrieve input stream information");
        goto end;
    }

    av_dump_format(ifmt_ctx, 0, transition_file, 0);

    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename);
    if (!ofmt_ctx)
    {
        printf("Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    ofmt = ofmt_ctx->oformat;

    for (i = 0; i < ifmt_ctx->nb_streams; i++)
    {
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
        if (!out_stream)
        {
            printf("Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
        if (ret < 0)
        {
            printf("Failed to copy context from input to output stream codec context\n");
            goto end;
        }
        out_stream->codec->codec_tag = 0;

        /* 强制限定帧率为25fps */
        out_stream->codec->time_base.den = 25;
        out_stream->codec->time_base.num = 1;

        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }
    av_dump_format(ofmt_ctx, 0, out_filename, 1);

    if (!(ofmt->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            printf("Could not open output file '%s'", out_filename);
            goto end;
        }
    }

    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0)
    {
        printf("Error occurred when opening output file\n");
        goto end;
    }

    while (1)
    {
        AVStream *in_stream, *out_stream;

        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
            break;

        in_stream  = ifmt_ctx->streams[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];

        av_bitstream_filter_filter(h264bsfc, in_stream->codec, NULL, &pkt.data, &pkt.size, pkt.data, pkt.size, 0);

        /* copy packet */
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;

        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0)
        {
            printf("Error muxing packet\n");
            break;
        }
        av_free_packet(&pkt);
    }

    av_write_trailer(ofmt_ctx);
    ts_Duration = ofmt_ctx->streams[0]->nb_frames / 25.00;
    av_bitstream_filter_close(h264bsfc);

end:
    avformat_close_input(&ifmt_ctx);
    remove(in_filename);
    remove(transition_file);

    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    if (ret < 0 && ret != AVERROR_EOF)
    {
        return -1;
    }

    printf("File remuxing is done\n\n");
    return ts_Duration;
}

void *getVideo(void *)
{
    unsigned int i = 0;
    int reserveChannel = channel;       // 线程较多，channel 的值随时会变，故应及时接管
    char reserveTsPart1[100] = {0};     // 保留3个ts索引
    char reserveTsPart2[100] = {0};
    char reserveTsPart3[100] = {0};
    char m3u8ContextTitle[100] = {0};
    float ts_time = 0.00;
    FILE *fp;
    hm_Device_Info->channelHandle[reserveChannel] = CLIENT_RealPlay(hm_Device_Info->loginHandle, reserveChannel, NULL);   // get the channel from the other program
    printf("The realPlayHandle is %ld.\n", hm_Device_Info->channelHandle[reserveChannel]);
    if (hm_Device_Info->channelHandle[reserveChannel] == 0)
    {
        printf("Failed to open the channel.\n");
        return 0;
    }

    while(hm_Device_Info->onlineFlag)
    {
        i++;
        char fileName[100] = {0};
        char in_file[100] = {0};
        char out_file[100] = {0};

        // 命名规则, videoPath + videoNamePrefix + deviceName + channelIndex + _ + fileIndex;
        // for example; /home/username/videoFolder/realVideo_Jinhua16_1
        sprintf(fileName, "%s%s%s%d%s%d", videoPath, "realVideo_", hm_Device_Info->devName, reserveChannel+1, "_", i);
        printf("Path is %s\n", fileName);
        CLIENT_SaveRealData(hm_Device_Info->channelHandle[reserveChannel],  fileName);     // 开始下载监控原始数据
        sprintf(fileName, "%s%s%d%s", videoPath,  hm_Device_Info->devName, reserveChannel+1, ".m3u8");      // 生成流媒体索引文件       

        if(i > 1)
        {
            sprintf(in_file, "%s%s%s%d%s%d", videoPath, "realVideo_", hm_Device_Info->devName, reserveChannel+1, "_", i-1);
            sprintf(out_file, "%s%s", in_file, ".ts");
            ts_time = TsStreamRemuxer(in_file, out_file); // 返回ts片段的时间
            if(ts_time <= 0.00)
            {
                printf("Transition error\n");
            }
        }

        if (i == 1)
        {
            if ((fp=fopen(fileName, "wb+")) == NULL )       // 打开并清空原文件
            {
                printf("Cannot open m3u8 file\n");
                break;
            }
            else
            {
                char context[100] = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-TARGETDURATION:10\n";
                sprintf(reserveTsPart3, "%s%s%s", "#EXTINF:7,\n", m3u8Url, "videoForWait.ts\n");
                fputs(context, fp);
                fputs(reserveTsPart3, fp);
                fclose(fp);
            }
        }
        else if(i > 1)
        {
            if ((fp=fopen(fileName, "wb+")) == NULL )       // 打开并清空原文件
            {
                printf("Cannot open this file\n");
                break;
            }
            else
            {
                strcpy(reserveTsPart1, reserveTsPart2);
                strcpy(reserveTsPart2, reserveTsPart3);
                sprintf(reserveTsPart3, "%s%.6f%s%s%s%s%d%s%d%s", "#EXTINF:", ts_time,  ",\n", m3u8Url, "realVideo_", hm_Device_Info->devName, reserveChannel+1, "_", i - 1, ".ts\n");
                sprintf(m3u8ContextTitle, "%s%d%s", "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-MEDIA-SEQUENCE:", i - 1, "\n#EXT-X-TARGETDURATION:10\n");
                fputs(m3u8ContextTitle, fp);
                fputs(reserveTsPart1, fp);
                fputs(reserveTsPart2, fp);
                fputs(reserveTsPart3, fp);
                fclose(fp);
            }
            // 删除磁盘上的i-4号文件
            sprintf(fileName, "%s%s%s%d%s%d%s", videoPath, "realVideo_", hm_Device_Info->devName, reserveChannel+1, "_", i - 4, ".ts");
            remove(fileName);
        }

        sleep(7);

        /* 停止下载监控原始数据 */
        CLIENT_StopSaveRealData(hm_Device_Info->channelHandle[reserveChannel]);
        pthread_testcancel();
    }

    return NULL;
}

int threadManage(int index, bool stopFlag)
{
    if (hm_Device_Info->channelHandle[index] != 0)
    {
        if(stopFlag)
        {
            pthread_cancel(pthread[index]);
            pthread_join(pthread[index], NULL);     // whether it cause a block than influence the others?

            CLIENT_StopSaveRealData(hm_Device_Info->channelHandle[index]);  // 额外增加的停止下载功能，解决有时线程结束时下载末停止的问题
            hm_Device_Info->channelHandle[index] = 0;
            pthread[index] = 0;
            channel = 0;
            // 线程结束后清空所选m3u8索引文件
            char fileName[50] = {0};
            FILE *fp;
            sprintf(fileName, "%s%s%d%s", videoPath,  hm_Device_Info->devName, index+1, ".m3u8");      // 生成流媒体索引文件
            if ((fp=fopen(fileName, "wb+")) == NULL )       // 打开并清空原文件
            {
                printf("Cannot open m3u8 file\n");
            }
            else
            {
                char context[100] = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-TARGETDURATION:10\n";
                fputs(context, fp);
                sprintf(context, "%s%s%s", "#EXTINF:7,\n", m3u8Url, "videoForWait.ts\n");
                fputs(context, fp);
                fclose(fp);
            }
        }
    }
    else if(!stopFlag)
    {
        channel = index;
        pthread_create(&pthread[index], NULL, getVideo, NULL);
        printf("已经启动下载线程\n");
        sleep(1);
    }

    return 0;
}

void *databaseManage(void *)
{
    MYSQL mysql;
    MYSQL_RES *res;
    MYSQL_ROW row;
    char query[200] = {0};
    int t = 0;
    mysql_init(&mysql);
    if (!mysql_real_connect(&mysql, "localhost", "ifm", "huimv", "ifm", 0, NULL, 0))
    {
        printf("Error connecting to database: %s\n", mysql_error(&mysql));
        return NULL;
    }
    else
        printf("Connecting...\n");
    sprintf(query, "%s%s%s", "select td1, td2, td3, td4, td5, td6, td7, td8, td9, td10, td11, td12, td13, td14, td15, td16 from xt_ydjk where sbip = '", hm_Device_Info->devIp, "'");

    // check the database every 3 seconds.
    while(1)
    {
        t = mysql_real_query(&mysql, query, (unsigned int) strlen(query));
        if (t)
        {
            printf("Error making query: %s\n", mysql_error(&mysql));
            break;
        }
        else
        {
//            printf("[%s] made...\n", query);
        }
        res = mysql_store_result(&mysql);
        while(row = mysql_fetch_row(res))
        {
            pthread_mutex_lock(&mut);
            for(t = 0; t < mysql_num_fields(res); t++)
            {
//                printf("%s;", row[t]);
                hm_Device_Info->channelFlag[t] = atoi(row[t]);
            }
            pthread_mutex_unlock(&mut);
            printf("\n");
        }
        printf("mysql_free_result...\n");
        mysql_free_result(res);
        pthread_testcancel();
        sleep(3);
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        // 输入配置文件。TODO：从数据库中读取xy_ydjk.sbip = da_zsjk.sbip，获取用户名、密码
        printf("Please input a *.ini file\n");
        return -1;
    }

//    float test = TsStreamRemuxer(argv[1], argv[2]);
//    printf("the ts duration is %.2f\n", test);
//    return 0;

    CLIENT_Init(DisConnectFunc, 0);     // 初始化sdk
    CLIENT_SetAutoReconnect(AutoConnectFunc, 0);

    NET_DEVICEINFO deviceInfo;
    InitDecviceInfo(*hm_Device_Info);
    int loadFlag = LoadConfig(*hm_Device_Info, argv[1], videoPath, m3u8Url);
    if (!loadFlag)
    {
        printf("Not load a valid file\n");
        return -1;
    }

    int error = 0;
    int threadState = -1;

    printf("The IP is %s\n", hm_Device_Info->devIp);
    printf("The Port is %d\n", hm_Device_Info->devPort);
    printf("The UserName is %s\n", hm_Device_Info->devUser);
    printf("The Video Stroge Path is %s\n", videoPath);
    printf("The m3u8 Url Path is %s\n", m3u8Url);
    hm_Device_Info->loginHandle = CLIENT_Login(hm_Device_Info->devIp, hm_Device_Info->devPort, hm_Device_Info->devUser, hm_Device_Info->devPwd, &deviceInfo, &error);

    printf("The Device ID is %ld\n", hm_Device_Info->loginHandle);
    if (0 == hm_Device_Info->loginHandle)
    {
        ChangeLoginError(error , &hm_Device_Info->errorStr);
        printf("Connect Fail! The error id is %d\n", error);
        printf("%s\n", hm_Device_Info->errorStr);
        hm_Device_Info->onlineFlag = false;
        return -1;
    }
    hm_Device_Info->onlineFlag = true;

    pthread_t databasePthread;
    pthread_create(&databasePthread, NULL, databaseManage, NULL);
    printf("已经启动数据库线程\n");
    while(1)
    {
        for(int i = 0; i < 16; i++)
        {
            if (hm_Device_Info->channelFlag[i] != 0)
            {
                threadState = threadManage(i, false);
            }
            else
            {
                threadState = threadManage(i, true);
            }
        }
        printf("The thread operate is %s\n", threadState == 0 ? "success" : "fail");
        // 初次启动，数据库延时3秒，此处延时2秒，每启动一个线程延时1秒，共6秒
        sleep(2);
        if(!hm_Device_Info->onlineFlag)
        {
            for(int index = 0; index < 16; index++)
            {
                if(pthread[index] != 0)
                {
                    pthread_cancel(pthread[index]);
                    pthread_join(pthread[index], NULL);
                    hm_Device_Info->channelHandle[index] = 0;
                    pthread[index] = 0;
                }
                else
                    continue;
            }
        }
    }

    return 0;
}
