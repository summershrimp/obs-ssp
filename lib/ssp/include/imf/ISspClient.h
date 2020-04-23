#pragma once
#include <stdint.h>
#include <functional>

namespace imf
{
#ifdef _WIN32
#define LIBSSP_API __declspec(dllexport)
#else
#define LIBSSP_API
#endif
#define ERROR_SSP_PROTOCOL_VERSION_GT_SERVER        (-1000)
#define ERROR_SSP_PROTOCOL_VERSION_LT_SERVER        (-1001)
#define ERROR_SSP_CONNECTION_FAILED                 (-1002)
#define ERROR_SSP_CONNECTION_EXIST                  (-1003)
#define VIDEO_ENCODER_UNKNOWN	(0)
#define VIDEO_ENCODER_H264	(96)
#define VIDEO_ENCODER_H265	(265)



#define AUDIO_ENCODER_UNKNOWN	(0)
#define AUDIO_ENCODER_AAC	(37)
#define AUDIO_ENCODER_PCM	(23)
    enum {
        STREAM_DEFAULT = 0,
        STREAM_MAIN = 1,
        STREAM_SEC = 2
    };
    struct LIBSSP_API SspVideoMeta {
        uint32_t width;
        uint32_t height;
        uint32_t timescale;
        uint32_t unit;
        uint32_t gop;
        uint32_t encoder;
    };

    struct LIBSSP_API SspAudioMeta {
        uint32_t timescale;
        uint32_t unit;
        uint32_t sample_rate;
        uint32_t sample_size;
        uint32_t channel;
        uint32_t bitrate;
        uint32_t encoder;
    };

    struct LIBSSP_API SspMeta {
        bool pts_is_wall_clock;
        bool tc_drop_frame;
        uint32_t timecode;
    };

    struct LIBSSP_API SspH264Data {
        uint8_t * data;
        size_t len;
        uint64_t pts;
        uint64_t ntp_timestamp;
        uint32_t frm_no;
        uint32_t type;			// I or P
    };

    struct LIBSSP_API SspAudioData {
        uint8_t * data;
        size_t len;
        uint64_t pts;
        uint64_t ntp_timestamp;
    };

    typedef std::function <void(void)> OnRecvBufferFullCallback;					// called when the recv buffer is full
    typedef std::function <void(void)> OnDisconnectedCallback;						// called when the session is closed
    typedef std::function <void(void)> OnConnectionConnectedCallback;				// called when the session is est
    typedef std::function <void(struct SspH264Data * h264)> OnH264DataCallback;		// called every video frame is ready. Actually, it's a video callback, no matter it's compression format
    typedef std::function <void(struct SspAudioData * audio)> OnAudioDataCallback;	// called every audio frame is ready
    typedef std::function <void(struct SspVideoMeta*, struct SspAudioMeta*, struct SspMeta *)> OnMetaCallback;	// meta data callback
    typedef std::function <void(int code, const char* description)> OnExceptionCallback;	// exception

    class Loop;
    class ISspClient_class {
    public:
        virtual void destroy() = 0;
        virtual int init(void) = 0;
        virtual int start(void) = 0;
        virtual int stop(void) = 0;

        virtual void setOnRecvBufferFullCallback(const OnRecvBufferFullCallback & cb) = 0;
        virtual void setOnH264DataCallback(const OnH264DataCallback & cb) = 0;
        virtual void setOnAudioDataCallback(const OnAudioDataCallback & cb) = 0;
        virtual void setOnMetaCallback(const OnMetaCallback & cb) = 0;
        virtual void setOnDisconnectedCallback(const OnDisconnectedCallback & cb) = 0;
        virtual void setOnConnectionConnectedCallback(const OnConnectionConnectedCallback & cb) = 0;
        virtual void setOnExceptionCallback(const OnExceptionCallback & cb) = 0;
    };

    class ILoop_class{
    public:
        virtual void destroy() = 0;
        virtual int init(void) = 0;
        virtual int loop(void) = 0;
        virtual int quit(void) = 0;
        virtual void *getLoop(void) = 0;
    };
}

typedef imf::ISspClient_class* (*create_ssp_class_ptr)(const std::string & ip,
                                                          imf::Loop *loop, size_t bufSize, unsigned short port, uint32_t streamStyle);
typedef imf::ILoop_class *(*create_loop_class_ptr)();