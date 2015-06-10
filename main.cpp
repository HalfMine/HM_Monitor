/**
 * @file
 * libavformat/libavcodec demuxing and muxing API.
 *
 * Remux streams from one container format to another.
 * @example remuxing.c
 */

#define __STDC_CONSTANT_MACROS
#include <stdio.h>
extern "C"
{
    #include <libavformat/avformat.h>
}

// 主函数
int main(int argc, char **argv)
{
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVPacket pkt;
    const char *in_filename, *out_filename;
    int ret, i;

    if (argc < 3) {
        printf("usage: %s input output\n"
               "API example program to remux a media file with libavformat and libavcodec.\n"
               "The output format is guessed according to the file extension.\n"
               "\n", argv[0]);
        return 1;
    }

    // TODO: 名字要统一好，加序列号区分
    in_filename   = argv[1];
//    transiton_file =
    out_filename = argv[2];

    char h264_head[] = {0x00, 0x00, 0x00, 0x01};
    char dh_head[]     = {0x44, 0x48, 0x41, 0x56};
    char dh_end[]       = {0x64, 0x68, 0x61, 0x76};
    printf("The define is %s %s %s\n", h264_head, dh_head, dh_end);

    FILE *p_in_file, *p_trans_file;
    char *buffer;
    int file_size;
    int test_head_size = 48;
    int read_size, write_size;
    int dh_frame_size, dh_head_size;
    int frame_size;
    long seek_size = 0;

    if((p_in_file = fopen(in_filename, "rb")) == NULL)
    {
        printf("File error\n");
        return 0;
    }
    p_trans_file = fopen("transition", "ab");
    if(p_trans_file == NULL)
    {
        printf("New file error\n");
        return 0;
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
            return 0;
        }
        read_size = fread(buffer, 1, test_head_size, p_in_file);
        printf("the head is %X %X %X %X\n", buffer[0], buffer[1], buffer[2], buffer[3]);
        if(read_size != 48)
        {
            printf ("Reading error\n");
            return 0;
        }
        if((buffer[0] == dh_head[0]) && (buffer[1] == dh_head[1]) && (buffer[2] == dh_head[2]) && (buffer[3] == dh_head[3]))
        {
            printf("Find dh head\n");
        }
        else
        {
            printf("Head error\n");
            return 0;
        }

        // 计算帧长度，注意补码问题
        printf("the length is %X %X %X %X\n", buffer[15], buffer[14], buffer[13], buffer[12]);
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

        // 查找264头
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
        read_size = fread(buffer, 1, frame_size + 8, p_in_file);
        if(read_size != frame_size + 8)
        {
            printf ("Reading error\n");
            return 0;
        }
        printf("The 264head is %X %X %X %X\n", buffer[0], buffer[1], buffer[2], buffer[3]);
        printf("The end is %X %X %X %X\n", buffer[frame_size], buffer[frame_size + 1], buffer[frame_size + 2], buffer[frame_size + 3]);
        write_size = fwrite(buffer, 1, frame_size, p_trans_file);
        if(write_size != frame_size)
        {
            printf("Writing error\n");
            return 0;
        }
        seek_size += dh_frame_size;
        free(buffer);
    }
    fclose(p_trans_file);
    fclose(p_in_file);

    in_filename = "transition";
    av_register_all();
    AVBitStreamFilterContext* h264bsfc =  av_bitstream_filter_init("h264_mp4toannexb");

    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
        printf("Could not open input file '%s'", in_filename);
        goto end;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        printf("Failed to retrieve input stream information");
        goto end;
    }

    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename);
    if (!ofmt_ctx) {
        printf("Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    ofmt = ofmt_ctx->oformat;

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
        if (!out_stream) {
            printf("Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
        if (ret < 0) {
            printf("Failed to copy context from input to output stream codec context\n");
            goto end;
        }
        out_stream->codec->codec_tag = 0;

        // 强制限定帧率为25fps
        out_stream->codec->time_base.den = 25;
        out_stream->codec->time_base.num = 1;

        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }
    av_dump_format(ofmt_ctx, 0, out_filename, 1);

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            printf("Could not open output file '%s'", out_filename);
            goto end;
        }
    }

    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        printf("Error occurred when opening output file\n");
        goto end;
    }

    while (1) {
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
        if (ret < 0) {
            printf("Error muxing packet\n");
            break;
        }
        av_free_packet(&pkt);
    }

    av_write_trailer(ofmt_ctx);
    av_bitstream_filter_close(h264bsfc);

end:
    avformat_close_input(&ifmt_ctx);

    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    if (ret < 0 && ret != AVERROR_EOF) {
        return 1;
    }

    return 0;
}
