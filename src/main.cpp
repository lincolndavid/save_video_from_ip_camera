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

#define BUFFERSIZE 30 * 30 // 30 seconds with 30 FPS

int main(void) {
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    const char *in_filename, *out_filename;
    in_filename  = "rtsp://admin:admin123@192.168.0.23/cam/realmonitor?channel=1&subtype=0";
    out_filename = "output.mp4";

    avformat_network_init();

    AVDictionary* avdic = NULL;
    char option_key[]   = "rtsp_transport";
    char option_value[] = "tcp";
    av_dict_set(&avdic, option_key, option_value, 0);
    char option_key2[]   = "max_delay";
    char option_value2[] = "5000000";
    av_dict_set(&avdic, option_key2, option_value2, 0);

    AVPacket* pkt;
    AVOutputFormat* ofmt = NULL;
    int video_index      = -1;

    RingQueue circle(BUFFERSIZE);

    int i;
    int v = 0;

    //Open the input stream
    int ret;
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, &avdic)) < 0) {
        cout << "Could not open input file." << endl;
    }
    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        cout << "Failed to retrieve input stream information" << endl;
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

    auto out_format = av_guess_format("mp4", nullptr, nullptr);

    //Open the output stream
    avformat_alloc_output_context2(&ofmt_ctx, out_format, out_format->name, out_filename);

    if (!ofmt_ctx) {
        printf("Could not create output context\n");
        ret = AVERROR_UNKNOWN;
    }

    auto input_stream  = ifmt_ctx->streams[video_index];
    auto output_stream = avformat_new_stream(ofmt_ctx, nullptr);

    if (output_stream == nullptr) {
        std::cerr << "Failed to create output stream" << std::endl;
        return 1;
    }

    ret = avcodec_parameters_copy(output_stream->codecpar, input_stream->codecpar);

    if (ret < 0) {
        std::cerr << "Could not copy codec parameters" << std::endl;
    }

    output_stream->id             = ofmt_ctx->nb_streams - 1;
    output_stream->r_frame_rate   = input_stream->r_frame_rate;
    output_stream->avg_frame_rate = input_stream->avg_frame_rate;
    output_stream->time_base      = input_stream->time_base;

    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            std::cerr << "Could not open avio" << std::endl;
            return 1;
        }
    }

    av_dump_format(ofmt_ctx, 0, out_filename, 1);

    //Write file header to output file
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        printf("Error occured when opening output URL\n");
    }

    //Continuous access to data packets in the while loop
    while (v < (BUFFERSIZE * 2)) {
        AVStream *in_stream, *out_stream;

        //Alloc packet memory
        pkt = av_packet_alloc();
        //Get a packet from the input stream
        ret = av_read_frame(ifmt_ctx, pkt);
        if (ret < 0) break;

        if (pkt->stream_index != video_index) {
            av_packet_unref(pkt);
            continue;
        }

        in_stream  = ifmt_ctx->streams[pkt->stream_index];
        out_stream = ofmt_ctx->streams[pkt->stream_index];

        //Not all packet s in this while loop are video frames.
        //Record when you receive a video frame.
        if (pkt->flags & AV_PKT_FLAG_CORRUPT || pkt->flags & AV_PKT_FLAG_DISCARD) {
            std::cout << "Discarding packet - corrupted" << std::endl;
        } else {
            circle.insert(&pkt);
        }
        v++;
    }

    circle.dump(ofmt_ctx);

    //Write the end of the file
    av_write_trailer(ofmt_ctx);

    if (ofmt_ctx && !(out_format->flags & AVFMT_NOFILE)) avio_close(ofmt_ctx->pb);
    av_dict_free(&avdic);
    avformat_close_input(&ifmt_ctx);
    //Close input
    avformat_free_context(ofmt_ctx);
    if (ret < 0 && ret != AVERROR_EOF) {
        cout << "Error occured." << endl;
        return -1;
    }

    return 0;
}
