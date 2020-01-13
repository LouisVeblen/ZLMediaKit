﻿/*
 * MIT License
 *
 * Copyright (c) 2016-2019 xiongziliang <771730766@qq.com>
 *
 * This file is part of ZLMediaKit(https://github.com/xiongziliang/ZLMediaKit).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef ZLMEDIAKIT_H264_H
#define ZLMEDIAKIT_H264_H

#include "Frame.h"
#include "Track.h"
#include "Util/base64.h"
using namespace toolkit;
#define H264_TYPE(v) ((uint8_t)(v) & 0x1F)

namespace mediakit{

bool getAVCInfo(const string &strSps,int &iVideoWidth, int &iVideoHeight, float  &iVideoFps);
void splitH264(const char *ptr, int len, const std::function<void(const char *, int)> &cb);

/**
 * 264帧类
 */
class H264Frame : public Frame {
public:
    typedef std::shared_ptr<H264Frame> Ptr;

    typedef enum {
        NAL_SPS = 7,
        NAL_PPS = 8,
        NAL_IDR = 5,
        NAL_SEI = 6,
    } NalType;

    char *data() const override{
        return (char *)buffer.data();
    }
    uint32_t size() const override {
        return buffer.size();
    }
    uint32_t dts() const override {
        return timeStamp;
    }

    uint32_t pts() const override {
        return ptsStamp ? ptsStamp : timeStamp;
    }

    uint32_t prefixSize() const override{
        return iPrefixSize;
    }

    TrackType getTrackType() const override{
        return TrackVideo;
    }

    CodecId getCodecId() const override{
        return CodecH264;
    }

    bool keyFrame() const override {
        return H264_TYPE(buffer[iPrefixSize]) == H264Frame::NAL_IDR;
    }

    bool configFrame() const override{
        switch(H264_TYPE(buffer[iPrefixSize]) ){
            case H264Frame::NAL_SPS:
            case H264Frame::NAL_PPS:
                return true;
            default:
                return false;
        }
    }
public:
    uint32_t timeStamp;
    uint32_t ptsStamp = 0;
    string buffer;
    uint32_t iPrefixSize = 4;
};


/**
 * 防止内存拷贝的H264类
 * 用户可以通过该类型快速把一个指针无拷贝的包装成Frame类
 * 该类型在DevChannel中有使用
 */
class H264FrameNoCacheAble : public FrameNoCacheAble {
public:
    typedef std::shared_ptr<H264FrameNoCacheAble> Ptr;

    H264FrameNoCacheAble(char *ptr,uint32_t size,uint32_t dts , uint32_t pts ,int prefixeSize = 4){
        _ptr = ptr;
        _size = size;
        _dts = dts;
        _pts = pts;
        _prefixSize = prefixeSize;
    }

    TrackType getTrackType() const override{
        return TrackVideo;
    }

    CodecId getCodecId() const override{
        return CodecH264;
    }

    bool keyFrame() const override {
        return H264_TYPE(_ptr[_prefixSize]) == H264Frame::NAL_IDR;
    }

    bool configFrame() const override{
        switch(H264_TYPE(_ptr[_prefixSize])){
            case H264Frame::NAL_SPS:
            case H264Frame::NAL_PPS:
                return true;
            default:
                return false;
        }
    }
};

/**
 * 一个H264Frame类中可以有多个帧，他们通过 0x 00 00 01 分隔
 * ZLMediaKit会先把这种复合帧split成单个帧然后再处理
 * 一个复合帧可以通过无内存拷贝的方式切割成多个H264FrameSubFrame
 * 提供该类的目的是切换复合帧时防止内存拷贝，提高性能
 */
template<typename Parent>
class FrameInternal : public Parent{
public:
    typedef std::shared_ptr<FrameInternal> Ptr;
    FrameInternal(const Frame::Ptr &parent_frame,
                  char *ptr,
                  uint32_t size,
                  int prefixeSize) : Parent(ptr,size,parent_frame->dts(),parent_frame->pts(),prefixeSize){
        _parent_frame = parent_frame;
    }
    bool cacheAble() const override {
        return _parent_frame->cacheAble();
    }
private:
    Frame::Ptr _parent_frame;
};

typedef FrameInternal<H264FrameNoCacheAble> H264FrameInternal;

/**
 * 264视频通道
 */
class H264Track : public VideoTrack{
public:
    typedef std::shared_ptr<H264Track> Ptr;

    /**
     * 不指定sps pps构造h264类型的媒体
     * 在随后的inputFrame中获取sps pps
     */
    H264Track(){}
    /**
     * 构造h264类型的媒体
     * @param sps sps帧数据
     * @param pps pps帧数据
     * @param sps_prefix_len 264头长度，可以为3个或4个字节，一般为0x00 00 00 01
     * @param pps_prefix_len 264头长度，可以为3个或4个字节，一般为0x00 00 00 01
     */
    H264Track(const string &sps,const string &pps,int sps_prefix_len = 4,int pps_prefix_len = 4){
        _sps = sps.substr(sps_prefix_len);
        _pps = pps.substr(pps_prefix_len);
        onReady();
    }

    /**
     * 构造h264类型的媒体
     * @param sps sps帧
     * @param pps pps帧
     */
    H264Track(const Frame::Ptr &sps,const Frame::Ptr &pps){
        if(sps->getCodecId() != CodecH264 || pps->getCodecId() != CodecH264 ){
            throw std::invalid_argument("必须输入H264类型的帧");
        }
        _sps = string(sps->data() + sps->prefixSize(),sps->size() - sps->prefixSize());
        _pps = string(pps->data() + pps->prefixSize(),pps->size() - pps->prefixSize());
        onReady();
    }

    /**
     * 返回不带0x00 00 00 01头的sps
     * @return
     */
    const string &getSps() const{
        return _sps;
    }

    /**
     * 返回不带0x00 00 00 01头的pps
     * @return
     */
    const string &getPps() const{
        return _pps;
    }

    CodecId getCodecId() const override {
        return CodecH264;
    }

    /**
     * 返回视频高度
     * @return
     */
    int getVideoHeight() const override{
        return _height ;
    }

    /**
     * 返回视频宽度
     * @return
     */
    int getVideoWidth() const override{
        return _width;
    }

    /**
     * 返回视频fps
     * @return
     */
    float getVideoFps() const override{
        return _fps;
    }

    bool ready() override {
        return !_sps.empty() && !_pps.empty();
    }

    /**
    * 输入数据帧,并获取sps pps
    * @param frame 数据帧
    */
    void inputFrame(const Frame::Ptr &frame) override{
        int type = H264_TYPE(*((uint8_t *)frame->data() + frame->prefixSize()));
        if(type == H264Frame::NAL_SPS){
            //有些设备会把SPS PPS IDR帧当做一个帧打包，所以我们要split一下
            bool  first_frame = true;
            splitH264(frame->data() + frame->prefixSize(),
                      frame->size() - frame->prefixSize(),
                      [&](const char *ptr, int len){
                          if(first_frame){
                              H264FrameInternal::Ptr sub_frame = std::make_shared<H264FrameInternal>(frame,
                                                                                                     frame->data(),
                                                                                                     len + frame->prefixSize(),
                                                                                                     frame->prefixSize());
                              inputFrame_l(sub_frame);
                              first_frame = false;
                          }else{
                              H264FrameInternal::Ptr sub_frame = std::make_shared<H264FrameInternal>(frame,
                                                                                                     (char *)ptr,
                                                                                                     len ,
                                                                                                     3);
                              inputFrame_l(sub_frame);
                          }
                      });
        } else{
            inputFrame_l(frame);
        }
    }
private:
    /**
     * 解析sps获取宽高fps
     */
    void onReady(){
        getAVCInfo(_sps,_width,_height,_fps);
    }
    Track::Ptr clone() override {
        return std::make_shared<std::remove_reference<decltype(*this)>::type >(*this);
    }

    /**
     * 输入数据帧,并获取sps pps
     * @param frame 数据帧
     */
    void inputFrame_l(const Frame::Ptr &frame){
        int type = H264_TYPE(*((uint8_t *)frame->data() + frame->prefixSize()));
        switch (type){
            case H264Frame::NAL_SPS:{
                //sps
                _sps = string(frame->data() + frame->prefixSize(),frame->size() - frame->prefixSize());
            }
                break;
            case H264Frame::NAL_PPS:{
                //pps
                _pps = string(frame->data() + frame->prefixSize(),frame->size() - frame->prefixSize());
            }
                break;

            case H264Frame::NAL_IDR:{
                //I
                insertConfigFrame(frame);
                VideoTrack::inputFrame(frame);
            }
                break;

            default:
                VideoTrack::inputFrame(frame);
                break;
        }

        _last_frame_is_idr = type == H264Frame::NAL_IDR;
        if(_width == 0 && ready()){
            onReady();
        }
    }

    //生成sdp
    Sdp::Ptr getSdp() override ;
private:
    //在idr帧前插入sps pps帧
    void insertConfigFrame(const Frame::Ptr &frame){
        if(_last_frame_is_idr){
            return;
        }

        if(!_sps.empty()){
            auto spsFrame = std::make_shared<H264Frame>();
            spsFrame->iPrefixSize = 4;
            spsFrame->buffer.assign("\x0\x0\x0\x1",4);
            spsFrame->buffer.append(_sps);
            spsFrame->timeStamp = frame->stamp();
            VideoTrack::inputFrame(spsFrame);
        }

        if(!_pps.empty()){
            auto ppsFrame = std::make_shared<H264Frame>();
            ppsFrame->iPrefixSize = 4;
            ppsFrame->buffer.assign("\x0\x0\x0\x1",4);
            ppsFrame->buffer.append(_pps);
            ppsFrame->timeStamp = frame->stamp();
            VideoTrack::inputFrame(ppsFrame);
        }
    }
private:
    string _sps;
    string _pps;
    int _width = 0;
    int _height = 0;
    float _fps = 0;
    bool _last_frame_is_idr = false;
};


/**
* h264类型sdp
*/
class H264Sdp : public Sdp {
public:

    /**
     *
     * @param sps 264 sps,不带0x00000001头
     * @param pps 264 pps,不带0x00000001头
     * @param playload_type  rtp playload type 默认96
     * @param bitrate 比特率
     */
    H264Sdp(const string &strSPS,
            const string &strPPS,
            int playload_type = 96,
            int bitrate = 4000) : Sdp(90000,playload_type) {
        //视频通道
        _printer << "m=video 0 RTP/AVP " << playload_type << "\r\n";
        _printer << "b=AS:" << bitrate << "\r\n";
        _printer << "a=rtpmap:" << playload_type << " H264/" << 90000 << "\r\n";
        _printer << "a=fmtp:" << playload_type << " packetization-mode=1; profile-level-id=";

        char strTemp[100];
        uint32_t profile_level_id = 0;
        if (strSPS.length() >= 4) { // sanity check
            profile_level_id = (uint8_t(strSPS[1]) << 16) |
                               (uint8_t(strSPS[2]) << 8)  |
                               (uint8_t(strSPS[3])); // profile_idc|constraint_setN_flag|level_idc
        }
        memset(strTemp, 0, 100);
        sprintf(strTemp, "%06X", profile_level_id);
        _printer << strTemp;
        _printer << "; sprop-parameter-sets=";
        memset(strTemp, 0, 100);
        av_base64_encode(strTemp, 100, (uint8_t *) strSPS.data(), strSPS.size());
        _printer << strTemp << ",";
        memset(strTemp, 0, 100);
        av_base64_encode(strTemp, 100, (uint8_t *) strPPS.data(), strPPS.size());
        _printer << strTemp << "\r\n";
        _printer << "a=control:trackID=" << getTrackType() << "\r\n";
    }

    string getSdp() const override {
        return _printer;
    }

    TrackType getTrackType() const override {
        return TrackVideo;
    }

    CodecId getCodecId() const override {
        return CodecH264;
    }
private:
    _StrPrinter _printer;
};


}//namespace mediakit


#endif //ZLMEDIAKIT_H264_H
