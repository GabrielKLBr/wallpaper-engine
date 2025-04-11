#include <windows.h>
#include <thread>
#include <chrono>
#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

HWND hwnd;
int window_width = GetSystemMetrics(SM_CXSCREEN);
int window_height = GetSystemMetrics(SM_CYSCREEN);
bool running = true;

AVFormatContext* fmt_ctx = nullptr;
AVCodecContext* video_dec_ctx = nullptr;
AVStream* video_stream = nullptr;
int video_stream_idx = -1;
struct SwsContext* sws_ctx = nullptr;

void RenderFrame(AVFrame* pFrameRGB) {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = window_width;
    bmi.bmiHeader.biHeight = -window_height; // negativo = top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(hwnd);
    StretchDIBits(hdc, 0, 0, window_width, window_height, 0, 0, window_width, window_height,
        pFrameRGB->data[0], &bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(hwnd, hdc);
}

void VideoThread() {
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* frameRGB = av_frame_alloc();

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB32, window_width, window_height, 1);
    uint8_t* buffer = (uint8_t*)av_malloc(numBytes);
    av_image_fill_arrays(frameRGB->data, frameRGB->linesize, buffer, AV_PIX_FMT_RGB32,
                         window_width, window_height, 1);

    while (running) {
    if (av_read_frame(fmt_ctx, pkt) < 0) {
        // Recomeça o vídeo do início
        av_seek_frame(fmt_ctx, video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(video_dec_ctx);
        continue;
    }

    if (pkt->stream_index == video_stream_idx) {
        avcodec_send_packet(video_dec_ctx, pkt);
        while (avcodec_receive_frame(video_dec_ctx, frame) == 0) {
            sws_scale(sws_ctx, frame->data, frame->linesize, 0, video_dec_ctx->height,
                        frameRGB->data, frameRGB->linesize);

            double fps = av_q2d(video_stream->r_frame_rate); // taxa de quadros real
            int delay = static_cast<int>(1000.0 / fps);      // em ms
                        
            RenderFrame(frameRGB);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay - 10));
        }
    }

    av_packet_unref(pkt);
}

    av_free(buffer);
    av_frame_free(&frameRGB);
    av_frame_free(&frame);
    av_packet_free(&pkt);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) {
        running = false;
        PostQuitMessage(0);
    }

    if (msg == WM_PAINT) {

    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

HWND GetDesktopWorkerW() {
    HWND progman = FindWindow("Progman", nullptr);
    SendMessageTimeout(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);

    HWND workerw = nullptr;
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        HWND shellView = FindWindowEx(hwnd, nullptr, "SHELLDLL_DefView", nullptr);
        if (shellView != nullptr) {
            HWND* out = reinterpret_cast<HWND*>(lParam);
            *out = FindWindowEx(nullptr, hwnd, "WorkerW", nullptr);
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&workerw));
    return workerw;
}


int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    avformat_network_init();

    const char* filename = lpCmdLine;
    avformat_open_input(&fmt_ctx, filename, nullptr, nullptr);
    avformat_find_stream_info(fmt_ctx, nullptr);

    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        AVCodecParameters* codecpar = fmt_ctx->streams[i]->codecpar;
        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
            video_dec_ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(video_dec_ctx, codecpar);
            avcodec_open2(video_dec_ctx, codec, nullptr);

            video_stream = fmt_ctx->streams[i];
            video_stream_idx = i;
            break;
        }
    }

    sws_ctx = sws_getContext(
        video_dec_ctx->width, video_dec_ctx->height, video_dec_ctx->pix_fmt,
        window_width, window_height, AV_PIX_FMT_RGB32,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = TEXT("FFmpegVideoPlayer");
    RegisterClass(&wc);

    HWND desktopWnd = GetDesktopWorkerW();

    hwnd = CreateWindowEx(0, wc.lpszClassName, "Player de Vídeo com FFmpeg + Win32",
        WS_OVERLAPPED | WS_CHILD, 0, 0,
        window_width, window_height, desktopWnd, NULL, hInstance, NULL);
    ShowWindow(hwnd, nCmdShow);

    std::thread videoThread(VideoThread);

    MSG msg = {};
    while (running && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    videoThread.join();

    avcodec_free_context(&video_dec_ctx);
    avformat_close_input(&fmt_ctx);
    sws_freeContext(sws_ctx);

    return 0;
}