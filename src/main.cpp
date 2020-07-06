#ifndef INT64_C
#define INT64_C(c) (c##LL)
#define UINT64_C(c) (c##ULL)
#endif

extern "C" {
/*Include ffmpeg header file*/
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
}

#include <iostream>
#include "utils/ringQueue.h"
using namespace std;

#define BUFFERSIZE 90

int main(void) {
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    const char *in_filename, *out_filename;
    in_filename  = "rtsp://admin:admin123@192.168.0.23/cam/realmonitor?channel=1&subtype=0";
    out_filename = "output.flv";

    avformat_network_init();

    AVDictionary* avdic = NULL;
    char option_key[]   = "rtsp_transport";
    char option_value[] = "tcp";
    av_dict_set(&avdic, option_key, option_value, 0);
    char option_key2[]   = "max_delay";
    char option_value2[] = "5000000";
    av_dict_set(&avdic, option_key2, option_value2, 0);

    AVPacket* pkt;
    // AVPacket* pkt1       = &pkt; // GAMBS: CORRECT LATER
    AVOutputFormat* ofmt = NULL;
    int video_index      = -1;
    int frame_index      = 0;

    RingQueue circle(BUFFERSIZE);

    int i;
    int v = 0;

    //Open the input stream
    int ret;
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, &avdic)) < 0) {
        cout << "Could not open input file." << endl;
        goto end;
    }
    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        cout << "Failed to retrieve input stream information" << endl;
        goto end;
    }

    //nb_streams represent several streams

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            //Video streaming
            video_index = i;
            cout << "get videostream." << endl;
            break;
        }
    }

    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    //Open the output stream
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename);

    if (!ofmt_ctx) {
        printf("Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }
    ofmt = ofmt_ctx->oformat;

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream* in_stream  = ifmt_ctx->streams[i];
        AVStream* out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);

        if (!out_stream) {
            printf("Failed allocating output stream.\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        //Copy the encoding information of the output stream to the input stream
        ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
        if (ret < 0) {
            printf("Failed to copy context from input to output stream codec context\n");
            goto end;
        }

        out_stream->codec->codec_tag = 0;

        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    av_dump_format(ofmt_ctx, 0, out_filename, 1);

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            printf("Could not open output URL '%s'", out_filename);
            goto end;
        }
    }

    //Write file header to output file
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        printf("Error occured when opening output URL\n");
        goto end;
    }

    //Continuous access to data packets in the while loop, regardless of audio and video, is stored in the file
    while (v < BUFFERSIZE) {
        AVStream *in_stream, *out_stream;

        //Alloc packet memory
        pkt = av_packet_alloc();
        //Get a packet from the input stream
        ret = av_read_frame(ifmt_ctx, pkt);
        if (ret < 0) break;

        in_stream  = ifmt_ctx->streams[pkt->stream_index];
        out_stream = ofmt_ctx->streams[pkt->stream_index];
        //copy packet
        //Conversion of PTS/DTS Timing
        pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base,
                                    (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt->dts = av_rescale_q_rnd(pkt->dts, in_stream->time_base, out_stream->time_base,
                                    (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        //printf("pts %d dts %d base %d\n",pkt.pts,pkt.dts, in_stream->time_base);
        pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
        pkt->pos      = -1;

        //Not all packet s in this while loop are video frames. Record when you receive a video frame
        if (pkt->stream_index == video_index) {
            printf("Receive %8d video frames from input URL\n", frame_index);
            frame_index++;
            circle.insert(&pkt);
        } else {
            av_free_packet(pkt);
        }

        v++;
    }

    std::cout << "OUT OF WHILE\n";
    circle.dump(ofmt_ctx);

    //Write the end of the file
    av_write_trailer(ofmt_ctx);

end:
    av_dict_free(&avdic);
    avformat_close_input(&ifmt_ctx);
    //Close input
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE)) avio_close(ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
    if (ret < 0 && ret != AVERROR_EOF) {
        cout << "Error occured." << endl;
        return -1;
    }

    return 0;
}
