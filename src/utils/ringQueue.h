#pragma once

#include <condition_variable>
#include <iostream>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

class RingQueue final {
public:
    RingQueue(const int size) : m_size { size } {
        if (m_size <= 1) {
            throw std::runtime_error("Invalid queue size! Should be bigger than 1");
        }
        std::cout << "Initializing queue with " << m_size << " elements" << std::endl;
        m_queue = new AVPacket*[size];
    }

    ~RingQueue() {
        std::cout << "Cleaning the queue" << std::endl;
        delete[] m_queue;
    }

    void insert(AVPacket** packet) {
        std::unique_lock<std::mutex> lock(
            m_access_mutex); // From this point on only this method can have the lock

        this->m_queue[this->m_head] =
            this->extractAndRelease(packet); // The user will lose the reference to the pointer

        std::clog << "queue now has packet with pts " << this->m_queue[this->m_head]->pts << "\r";
        this->m_head = (this->m_head + 1) % m_size;

        if (this->m_head == this->m_tail) {
            auto element = this->m_queue[this->m_tail];

            if (element != nullptr) {
                av_packet_unref(element);
                element = nullptr;
            }

            this->m_tail = (this->m_tail + 1) % m_size; // Push the tail forward
        }
    }

    int dump(AVFormatContext* output_format_context) {
        std::unique_lock<std::mutex> lock(
            m_access_mutex); // From this point on only this method can have the lock

        m_pts = 0; // Always zeros PTS before dumping to file
        return recursiveDump(output_format_context);
    }

private:
    AVPacket** m_queue;

    int m_head { 0 };
    int m_tail { 0 };
    int m_size { 0 };
    int m_pts { 0 };

    std::mutex m_access_mutex;
    const int kGrowthValue = 3000;

    AVPacket* extractAndRelease(AVPacket** packet) {
        AVPacket* incoming_packet = *packet;
        packet                    = nullptr;
        return incoming_packet;
    }

    int recursiveDump(AVFormatContext* output_format_context) {
        if ((this->m_tail + 1) % this->m_size ==
            this->m_head) { // If we are gonna pass the head we stop
            return 0;
        }

        auto packet_in_tail = this->m_queue[this->m_tail]; // Get the packet at the tail

        if (packet_in_tail != nullptr) {
            packet_in_tail->pts = packet_in_tail->dts = m_pts;
            m_pts += kGrowthValue;

            std::clog << "Writing packet " << packet_in_tail->pts << "\r";

            int ret = av_interleaved_write_frame(output_format_context,
                                                 packet_in_tail); // Dumps it to the file

            av_packet_unref(packet_in_tail); // Release the packet as ffmpeg already has its data
            packet_in_tail = nullptr;        // Add nullptr for sanity checks

            this->m_tail = (this->m_tail + 1) % this->m_size; // Moves the tail forward
            if (ret == 0) {
                return this->recursiveDump(
                    output_format_context); // Invokes it again until we reach the head
            }
        }

        std::clog << "No more packets in tail" << std::endl;

        return 0;
    }
};
