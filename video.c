#include "video.h"
#include "packet.h"
#include "frame.h"
#include "player.h"

static int queue_picture(player_stat_t *is, AVFrame *src_frame, double pts, double duration, int64_t pos)
{
    frame_t *vp;

    if (!(vp = frame_queue_peek_writable(&is->video_frm_queue)))
        return -1;

    vp->sar = src_frame->sample_aspect_ratio;
    vp->uploaded = 0;

    vp->width = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;

    vp->pts = pts;
    vp->duration = duration;
    vp->pos = pos;
    //vp->serial = serial;

    //set_default_window_size(vp->width, vp->height, vp->sar);

    // ��AVFrame���������Ӧλ��
    av_frame_move_ref(vp->frame, src_frame);
    // ���¶��м�����д����
    frame_queue_push(&is->video_frm_queue);
    return 0;
}


// ��packet_queue��ȡһ��packet����������frame
static int video_decode_frame(AVCodecContext *p_codec_ctx, packet_queue_t *p_pkt_queue, AVFrame *frame)
{
    int ret;
    
    while (1)
    {
        AVPacket pkt;

        while (1)
        {
            // 3. �ӽ���������frame
            // 3.1 һ����Ƶpacket��һ����Ƶframe
            //     ����������һ��������packet�󣬲��н�����frame���
            //     frame���˳���ǰ�pts��˳����IBBPBBP
            //     frame->pkt_pos�����Ǵ�frame��Ӧ��packet����Ƶ�ļ��е�ƫ�Ƶ�ַ��ֵͬpkt.pos
            ret = avcodec_receive_frame(p_codec_ctx, frame);
            if (ret < 0)
            {
                if (ret == AVERROR_EOF)
                {
                    av_log(NULL, AV_LOG_INFO, "video avcodec_receive_frame(): the decoder has been fully flushed\n");
                    avcodec_flush_buffers(p_codec_ctx);
                    return 0;
                }
                else if (ret == AVERROR(EAGAIN))
                {
                    av_log(NULL, AV_LOG_INFO, "video avcodec_receive_frame(): output is not available in this state - "
                            "user must try to send new input\n");
                    break;
                }
                else
                {
                    av_log(NULL, AV_LOG_ERROR, "video avcodec_receive_frame(): other errors\n");
                    continue;
                }
            }
            else
            {
                frame->pts = frame->best_effort_timestamp;
                //frame->pts = frame->pkt_dts;

                return 1;   // �ɹ�����õ�һ����Ƶ֡��һ����Ƶ֡���򷵻�
            }
        }

        // 1. ȡ��һ��packet��ʹ��pkt��Ӧ��serial��ֵ��d->pkt_serial
        if (packet_queue_get(p_pkt_queue, &pkt, true) < 0)
        {
            return -1;
        }

        if (pkt.data == NULL)
        {
            // ��λ�������ڲ�״̬/ˢ���ڲ���������
            avcodec_flush_buffers(p_codec_ctx);
        }
        else
        {
            // 2. ��packet���͸�������
            //    ����packet��˳���ǰ�dts������˳����IPBBPBB
            //    pkt.pos�������Ա�ʶ��ǰpacket����Ƶ�ļ��еĵ�ַƫ��
            if (avcodec_send_packet(p_codec_ctx, &pkt) == AVERROR(EAGAIN))
            {
                av_log(NULL, AV_LOG_ERROR, "receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
            }

            av_packet_unref(&pkt);
        }
    }
}

// ����Ƶ������õ���Ƶ֡��Ȼ��д��picture����
static int video_decode_thread(void *arg)
{
    player_stat_t *is = (player_stat_t *)arg;
    AVFrame *p_frame = av_frame_alloc();
    double pts;
    double duration;
    int ret;
    int got_picture;
    AVRational tb = is->p_video_stream->time_base;
    AVRational frame_rate = av_guess_frame_rate(is->p_fmt_ctx, is->p_video_stream, NULL);
    
    if (p_frame == NULL)
    {
        av_log(NULL, AV_LOG_ERROR, "av_frame_alloc() for p_frame failed\n");
        return AVERROR(ENOMEM);
    }

    while (1)
    {
        got_picture = video_decode_frame(is->p_vcodec_ctx, &is->video_pkt_queue, p_frame);
        if (got_picture < 0)
        {
            goto exit;
        }
        
        duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational){frame_rate.den, frame_rate.num}) : 0);   // ��ǰ֡����ʱ��
        pts = (p_frame->pts == AV_NOPTS_VALUE) ? NAN : p_frame->pts * av_q2d(tb);   // ��ǰ֡��ʾʱ���
        ret = queue_picture(is, p_frame, pts, duration, p_frame->pkt_pos);   // ����ǰ֡ѹ��frame_queue
        av_frame_unref(p_frame);

        if (ret < 0)
        {
            goto exit;
        }

    }

exit:
    av_frame_free(&p_frame);

    return 0;
}

// ������Ƶʱ����ͬ��ʱ��(����Ƶʱ��)�Ĳ�ֵ��У��delayֵ��ʹ��Ƶʱ��׷�ϻ�ȴ�ͬ��ʱ��
// �������delay����һ֡����ʱ��������һ֡���ź�Ӧ��ʱ�೤ʱ����ٲ��ŵ�ǰ֡��ͨ�����ڴ�ֵ�����ڵ�ǰ֡���ſ���
// ����ֵdelay�ǽ��������delay��У����õ���ֵ
static double compute_target_delay(double delay, player_stat_t *is)
{
    double sync_threshold, diff = 0;

    /* update delay to follow master synchronisation source */

    /* if video is slave, we try to correct big delays by
       duplicating or deleting a frame */
    // ��Ƶʱ����ͬ��ʱ��(����Ƶʱ��)�Ĳ��죬ʱ��ֵ����һ֡ptsֵ(ʵΪ����һ֡pts + ��һ֡�������ŵ�ʱ���)
    diff = get_clock(&is->video_clk) - get_clock(&is->audio_clk);
    // delay����һ֡����ʱ������ǰ֡(�����ŵ�֡)����ʱ������һ֡����ʱ�������ֵ
    // diff����Ƶʱ����ͬ��ʱ�ӵĲ�ֵ

    /* skip or repeat frame. We take into account the
       delay to compute the threshold. I still don't know
       if it is the best guess */
    // ��delay < AV_SYNC_THRESHOLD_MIN����ͬ����ֵΪAV_SYNC_THRESHOLD_MIN
    // ��delay > AV_SYNC_THRESHOLD_MAX����ͬ����ֵΪAV_SYNC_THRESHOLD_MAX
    // ��AV_SYNC_THRESHOLD_MIN < delay < AV_SYNC_THRESHOLD_MAX����ͬ����ֵΪdelay
    sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
    if (!isnan(diff))
    {
        if (diff <= -sync_threshold)        // ��Ƶʱ�������ͬ��ʱ�ӣ��ҳ���ͬ����ֵ
            delay = FFMAX(0, delay + diff); // ��ǰ֡����ʱ�������ͬ��ʱ��(delay+diff<0)��delay=0(��Ƶ׷�ϣ���������)������delay=delay+diff
        else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)  // ��Ƶʱ�ӳ�ǰ��ͬ��ʱ�ӣ��ҳ���ͬ����ֵ������һ֡����ʱ������
            delay = delay + diff;           // ����У��Ϊdelay=delay+diff����Ҫ��AV_SYNC_FRAMEDUP_THRESHOLD����������
        else if (diff >= sync_threshold)    // ��Ƶʱ�ӳ�ǰ��ͬ��ʱ�ӣ��ҳ���ͬ����ֵ
            delay = 2 * delay;              // ��Ƶ����Ҫ�����Ų���delay������2��
    }

    av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n", delay, -diff);

    return delay;
}

static double vp_duration(player_stat_t *is, frame_t *vp, frame_t *nextvp) {
    if (vp->serial == nextvp->serial)
    {
        double duration = nextvp->pts - vp->pts;
        if (isnan(duration) || duration <= 0)
            return vp->duration;
        else
            return duration;
    } else {
        return 0.0;
    }
}

static void update_video_pts(player_stat_t *is, double pts, int64_t pos, int serial) {
    /* update current video pts */
    set_clock(&is->video_clk, pts, serial);            // ����vidclock
    //-sync_clock_to_slave(&is->extclk, &is->vidclk);  // ��extclockͬ����vidclock
}

static void video_display(player_stat_t *is)
{
    frame_t *vp;

    vp = frame_queue_peek_last(&is->video_frm_queue);

    // ͼ��ת����p_frm_raw->data ==> p_frm_yuv->data
    // ��Դͼ����һƬ���������򾭹��������µ�Ŀ��ͼ���Ӧ���򣬴����ͼ�����������������
    // plane: ��YUV��Y��U��V����plane��RGB��R��G��B����plane
    // slice: ͼ����һƬ�������У������������ģ�˳���ɶ������ײ����ɵײ�������
    // stride/pitch: һ��ͼ����ռ���ֽ�����Stride=BytesPerPixel*Width+Padding��ע�����
    // AVFrame.*data[]: ÿ������Ԫ��ָ���Ӧplane
    // AVFrame.linesize[]: ÿ������Ԫ�ر�ʾ��Ӧplane��һ��ͼ����ռ���ֽ���
    sws_scale(is->img_convert_ctx,                      // sws context
              (const uint8_t *const *)vp->frame->data,// src slice
              vp->frame->linesize,                    // src stride
              0,                                      // src slice y
              is->p_vcodec_ctx->height,               // src slice height
              is->p_frm_yuv->data,                    // dst planes
              is->p_frm_yuv->linesize                 // dst strides
             );
    
    // ʹ���µ�YUV�������ݸ���SDL_Rect
    SDL_UpdateYUVTexture(is->sdl_video.texture,         // sdl texture
                         &is->sdl_video.rect,           // sdl rect
                         is->p_frm_yuv->data[0],        // y plane
                         is->p_frm_yuv->linesize[0],    // y pitch
                         is->p_frm_yuv->data[1],        // u plane
                         is->p_frm_yuv->linesize[1],    // u pitch
                         is->p_frm_yuv->data[2],        // v plane
                         is->p_frm_yuv->linesize[2]     // v pitch
                        );
    
    // ʹ���ض���ɫ��յ�ǰ��ȾĿ��
    SDL_RenderClear(is->sdl_video.renderer);
    // ʹ�ò���ͼ������(texture)���µ�ǰ��ȾĿ��
    SDL_RenderCopy(is->sdl_video.renderer,              // sdl renderer
                   is->sdl_video.texture,               // sdl texture
                   NULL,                                // src rect, if NULL copy texture
                   &is->sdl_video.rect                  // dst rect
                  );
    
    // ִ����Ⱦ��������Ļ��ʾ
    SDL_RenderPresent(is->sdl_video.renderer);
}

/* called to display each frame */
static void video_refresh(void *opaque, double *remaining_time)
{
    player_stat_t *is = (player_stat_t *)opaque;
    double time;
    static bool first_frame = true;

retry:
    if (frame_queue_nb_remaining(&is->video_frm_queue) == 0)  // ����֡����ʾ
    {    
        // nothing to do, no picture to display in the queue
        return;
    }

    double last_duration, duration, delay;
    frame_t *vp, *lastvp;

    /* dequeue the picture */
    lastvp = frame_queue_peek_last(&is->video_frm_queue);     // ��һ֡���ϴ�����ʾ��֡
    vp = frame_queue_peek(&is->video_frm_queue);              // ��ǰ֡����ǰ����ʾ��֡

    // lastvp��vp����ͬһ��������(һ��seek�Ὺʼһ���²�������)����frame_timer����Ϊ��ǰʱ��
    if (first_frame)
    {
        is->frame_timer = av_gettime_relative() / 1000000.0;
        first_frame = false;
    }

    // ��ͣ������ͣ������һ֡ͼ��
    if (is->paused)
        goto display;

    /* compute nominal last_duration */
    last_duration = vp_duration(is, lastvp, vp);        // ��һ֡����ʱ����vp->pts - lastvp->pts
    delay = compute_target_delay(last_duration, is);    // ������Ƶʱ�Ӻ�ͬ��ʱ�ӵĲ�ֵ������delayֵ

    time= av_gettime_relative()/1000000.0;
    // ��ǰ֡����ʱ��(is->frame_timer+delay)���ڵ�ǰʱ��(time)����ʾ����ʱ��δ��
    if (time < is->frame_timer + delay) {
        // ����ʱ��δ���������ˢ��ʱ��remaining_timeΪ��ǰʱ�̵���һ����ʱ�̵�ʱ���
        *remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
        // ����ʱ��δ�����򲻲��ţ�ֱ�ӷ���
        return;
    }

    // ����frame_timerֵ
    is->frame_timer += delay;
    // У��frame_timerֵ����frame_timer����ڵ�ǰϵͳʱ��̫��(�������ͬ����ֵ)�������Ϊ��ǰϵͳʱ��
    if (delay > 0 && time - is->frame_timer > AV_SYNC_THRESHOLD_MAX)
    {
        is->frame_timer = time;
    }

    SDL_LockMutex(is->video_frm_queue.mutex);
    if (!isnan(vp->pts))
    {
        update_video_pts(is, vp->pts, vp->pos, vp->serial); // ������Ƶʱ�ӣ�ʱ�����ʱ��ʱ��
    }
    SDL_UnlockMutex(is->video_frm_queue.mutex);

    // �Ƿ�Ҫ����δ�ܼ�ʱ���ŵ���Ƶ֡
    if (frame_queue_nb_remaining(&is->video_frm_queue) > 1)  // ������δ��ʾ֡��>1(ֻ��һ֡�򲻿��Ƕ�֡)
    {         
        frame_t *nextvp = frame_queue_peek_next(&is->video_frm_queue);  // ��һ֡����һ����ʾ��֡
        duration = vp_duration(is, vp, nextvp);             // ��ǰ֡vp����ʱ�� = nextvp->pts - vp->pts
        // ��ǰ֡vpδ�ܼ�ʱ���ţ�����һ֡����ʱ��(is->frame_timer+duration)С�ڵ�ǰϵͳʱ��(time)
        if (time > is->frame_timer + duration)
        {
            frame_queue_next(&is->video_frm_queue);   // ɾ����һ֡����ʾ֡����ɾ��lastvp����ָ���1(��lastvp���µ�vp)
            goto retry;
        }
    }

    // ɾ����ǰ��ָ��Ԫ�أ���ָ��+1����δ��֡����ָ���lastvp���µ�vp�����ж�֡����ָ���vp���µ�nextvp
    frame_queue_next(&is->video_frm_queue);

display:
    video_display(is);                      // ȡ����ǰ֡vp(���ж�֡��nextvp)���в���
}

static int video_playing_thread(void *arg)
{
    player_stat_t *is = (player_stat_t *)arg;
    double remaining_time = 0.0;

    while (1)
    {
        if (remaining_time > 0.0)
        {
            av_usleep((unsigned)(remaining_time * 1000000.0));
        }
        remaining_time = REFRESH_RATE;
        // ������ʾ��ǰ֡������ʱremaining_time������ʾ
        video_refresh(is, &remaining_time);
    }

    return 0;
}

static int open_video_playing(void *arg)
{
    player_stat_t *is = (player_stat_t *)arg;
    int ret;
    int buf_size;
    uint8_t* buffer = NULL;

    is->p_frm_yuv = av_frame_alloc();
    if (is->p_frm_yuv == NULL)
    {
        printf("av_frame_alloc() for p_frm_raw failed\n");
        return -1;
    }

    // ΪAVFrame.*data[]�ֹ����仺���������ڴ洢sws_scale()��Ŀ��֡��Ƶ����
    buf_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, 
                                        is->p_vcodec_ctx->width, 
                                        is->p_vcodec_ctx->height, 
                                        1
                                        );
    // buffer����Ϊp_frm_yuv����Ƶ���ݻ�����
    buffer = (uint8_t *)av_malloc(buf_size);
    if (buffer == NULL)
    {
        printf("av_malloc() for buffer failed\n");
        return -1;
    }
    // ʹ�ø��������趨p_frm_yuv->data��p_frm_yuv->linesize
    ret = av_image_fill_arrays(is->p_frm_yuv->data,     // dst data[]
                               is->p_frm_yuv->linesize, // dst linesize[]
                               buffer,                  // src buffer
                               AV_PIX_FMT_YUV420P,      // pixel format
                               is->p_vcodec_ctx->width, // width
                               is->p_vcodec_ctx->height,// height
                               1                        // align
                               );
    if (ret < 0)
    {
        printf("av_image_fill_arrays() failed %d\n", ret);
        return -1;;
    }

    // A2. ��ʼ��SWS context�����ں���ͼ��ת��
    //     �˴���6������ʹ�õ���FFmpeg�е����ظ�ʽ���ԱȲο�ע��B3
    //     FFmpeg�е����ظ�ʽAV_PIX_FMT_YUV420P��ӦSDL�е����ظ�ʽSDL_PIXELFORMAT_IYUV
    //     ��������õ�ͼ��Ĳ���SDL֧�֣�������ͼ��ת���Ļ���SDL���޷�������ʾͼ���
    //     ��������õ�ͼ����ܱ�SDL֧�֣��򲻱ؽ���ͼ��ת��
    //     ����Ϊ�˱����㣬ͳһת��ΪSDL֧�ֵĸ�ʽAV_PIX_FMT_YUV420P==>SDL_PIXELFORMAT_IYUV
    is->img_convert_ctx = sws_getContext(is->p_vcodec_ctx->width,   // src width
                                         is->p_vcodec_ctx->height,  // src height
                                         is->p_vcodec_ctx->pix_fmt, // src format
                                         is->p_vcodec_ctx->width,   // dst width
                                         is->p_vcodec_ctx->height,  // dst height
                                         AV_PIX_FMT_YUV420P,        // dst format
                                         SWS_BICUBIC,               // flags
                                         NULL,                      // src filter
                                         NULL,                      // dst filter
                                         NULL                       // param
                                         );
    if (is->img_convert_ctx == NULL)
    {
        printf("sws_getContext() failed\n");
        return -1;
    }

    // SDL_Rect��ֵ
    is->sdl_video.rect.x = 0;
    is->sdl_video.rect.y = 0;
    is->sdl_video.rect.w = is->p_vcodec_ctx->width;
    is->sdl_video.rect.h = is->p_vcodec_ctx->height;

    // 1. ����SDL���ڣ�SDL 2.0֧�ֶര��
    //    SDL_Window�����г���󵯳�����Ƶ���ڣ�ͬSDL 1.x�е�SDL_Surface
    is->sdl_video.window = SDL_CreateWindow("simple ffplayer", 
                              SDL_WINDOWPOS_UNDEFINED,// �����Ĵ���X����
                              SDL_WINDOWPOS_UNDEFINED,// �����Ĵ���Y����
                              is->sdl_video.rect.w, 
                              is->sdl_video.rect.h,
                              SDL_WINDOW_OPENGL
                              );
    if (is->sdl_video.window == NULL)
    {  
        printf("SDL_CreateWindow() failed: %s\n", SDL_GetError());  
        return -1;
    }

    // 2. ����SDL_Renderer
    //    SDL_Renderer����Ⱦ��
    is->sdl_video.renderer = SDL_CreateRenderer(is->sdl_video.window, -1, 0);
    if (is->sdl_video.renderer == NULL)
    {  
        printf("SDL_CreateRenderer() failed: %s\n", SDL_GetError());  
        return -1;
    }

    // 3. ����SDL_Texture
    //    һ��SDL_Texture��Ӧһ֡YUV���ݣ�ͬSDL 1.x�е�SDL_Overlay
   is->sdl_video.texture = SDL_CreateTexture(is->sdl_video.renderer, 
                                    SDL_PIXELFORMAT_IYUV, 
                                    SDL_TEXTUREACCESS_STREAMING,
                                    is->sdl_video.rect.w,
                                    is->sdl_video.rect.h
                                    );
    if (is->sdl_video.texture == NULL)
    {  
        printf("SDL_CreateTexture() failed: %s\n", SDL_GetError());  
        return -1;
    }

    SDL_CreateThread(video_playing_thread, "video playing thread", is);

    return 0;
}

static int open_video_stream(player_stat_t *is)
{
    AVCodecParameters* p_codec_par = NULL;
    AVCodec* p_codec = NULL;
    AVCodecContext* p_codec_ctx = NULL;
    AVStream *p_stream = is->p_video_stream;
    int ret;

    // 1. Ϊ��Ƶ������������AVCodecContext
    // 1.1 ��ȡ����������AVCodecParameters
    p_codec_par = p_stream->codecpar;

    // 1.2 ��ȡ������
    p_codec = avcodec_find_decoder(p_codec_par->codec_id);
    if (p_codec == NULL)
    {
        printf("Cann't find codec!\n");
        return -1;
    }

    // 1.3 ����������AVCodecContext
    // 1.3.1 p_codec_ctx��ʼ��������ṹ�壬ʹ��p_codec��ʼ����Ӧ��ԱΪĬ��ֵ
    p_codec_ctx = avcodec_alloc_context3(p_codec);
    if (p_codec_ctx == NULL)
    {
        printf("avcodec_alloc_context3() failed\n");
        return -1;
    }
    // 1.3.2 p_codec_ctx��ʼ����p_codec_par ==> p_codec_ctx����ʼ����Ӧ��Ա
    ret = avcodec_parameters_to_context(p_codec_ctx, p_codec_par);
    if (ret < 0)
    {
        printf("avcodec_parameters_to_context() failed\n");
        return -1;
    }
    // 1.3.3 p_codec_ctx��ʼ����ʹ��p_codec��ʼ��p_codec_ctx����ʼ�����
    ret = avcodec_open2(p_codec_ctx, p_codec, NULL);
    if (ret < 0)
    {
        printf("avcodec_open2() failed %d\n", ret);
        return -1;
    }

    is->p_vcodec_ctx = p_codec_ctx;
    
    // 2. ������Ƶ�����߳�
    SDL_CreateThread(video_decode_thread, "video decode thread", is);

    return 0;
}

int open_video(player_stat_t *is)
{
    open_video_stream(is);
    open_video_playing(is);

    return 0;
}
