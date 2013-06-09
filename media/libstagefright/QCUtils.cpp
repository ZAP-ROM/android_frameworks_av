/*Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "QCUtils"
#include <utils/Log.h>

#include <utils/Errors.h>
#include <sys/types.h>
#include <ctype.h>
#include <unistd.h>

#include <media/stagefright/MetaData.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/OMXCodec.h>
#include <cutils/properties.h>

#include "include/QCUtils.h"

#ifdef ENABLE_QC_AV_ENHANCEMENTS

#include <QCMetaData.h>
#include <QCMediaDefs.h>

namespace android {

void QCUtils::HFR::setHFRIfEnabled(
        const CameraParameters& params,
        sp<MetaData> &meta) {
    const char *hfr_str = params.get("video-hfr");
    int32_t hfr = -1;
    if ( hfr_str != NULL ) {
        hfr = atoi(hfr_str);
    }
    if (hfr < 0) {
        ALOGW("Invalid hfr value(%d) set from app. Disabling HFR.", hfr);
        hfr = 0;
    }

    meta->setInt32(kKeyHFR, hfr);
}

status_t QCUtils::HFR::reCalculateFileDuration(
        sp<MetaData> &meta, sp<MetaData> &enc_meta,
        int64_t &maxFileDurationUs, int32_t frameRate,
        video_encoder videoEncoder) {
    status_t retVal = OK;
    int32_t hfr = 0;

    if (!meta->findInt32(kKeyHFR, &hfr)) {
        ALOGW("hfr not found, default to 0");
    }

    if (hfr && frameRate) {
        maxFileDurationUs = maxFileDurationUs * (hfr/frameRate);
    }

    enc_meta->setInt32(kKeyHFR, hfr);
    int32_t width = 0, height = 0;

    CHECK(meta->findInt32(kKeyWidth, &width));
    CHECK(meta->findInt32(kKeyHeight, &height));

    char mDeviceName[100];
    property_get("ro.board.platform",mDeviceName,"0");
    if (!strncmp(mDeviceName, "msm7627a", 8)) {
        if (hfr && (width * height > 432*240)) {
            ALOGE("HFR mode is supported only upto WQVGA resolution");
            return INVALID_OPERATION;
        }
    } else if (!strncmp(mDeviceName, "msm8974", 7) ||
               !strncmp(mDeviceName, "msm8610", 7)) {
        if (hfr && (width * height > 1920*1088)) {
            ALOGE("HFR mode is supported only upto 1080p resolution");
            return INVALID_OPERATION;
        }
    } else {
        if (hfr && ((videoEncoder != VIDEO_ENCODER_H264) || (width * height > 800*480))) {
            ALOGE("HFR mode is supported only upto WVGA and H264 codec.");
            return INVALID_OPERATION;
        }
    }
    return retVal;
}

void QCUtils::HFR::reCalculateTimeStamp(
        sp<MetaData> &meta, int64_t &timestampUs) {
    int32_t frameRate = 0, hfr = 0;
    if (!(meta->findInt32(kKeyFrameRate, &frameRate))) {
        return;
    }

    if (!(meta->findInt32(kKeyHFR, &hfr))) {
        return;
    }

    if (hfr && frameRate) {
        timestampUs = (hfr * timestampUs) / frameRate;
    }
}

void QCUtils::HFR::reCalculateHFRParams(
        const sp<MetaData> &meta, int32_t &frameRate,
        int32_t &bitRate) {
    int32_t hfr = 0;
    if (!(meta->findInt32(kKeyHFR, &hfr))) {
        return;
    }

    if (hfr && frameRate) {
        bitRate = (hfr * bitRate) / frameRate;
        frameRate = hfr;
    }
}

void QCUtils::HFR::copyHFRParams(
        const sp<MetaData> &inputFormat,
        sp<MetaData> &outputFormat) {
    int32_t frameRate = 0, hfr = 0;
    inputFormat->findInt32(kKeyHFR, &hfr);
    inputFormat->findInt32(kKeyFrameRate, &frameRate);
    outputFormat->setInt32(kKeyHFR, hfr);
    outputFormat->setInt32(kKeyFrameRate, frameRate);
}

bool QCUtils::ShellProp::isAudioDisabled() {
    bool retVal = false;
    char disableAudio[PROPERTY_VALUE_MAX];
    property_get("persist.debug.sf.noaudio", disableAudio, "0");
    if (atoi(disableAudio) == 1) {
        retVal = true;
    }
    return retVal;
}

void QCUtils::ShellProp::setEncoderprofile(
        video_encoder &videoEncoder, int32_t &videoEncoderProfile) {
    char value[PROPERTY_VALUE_MAX];
    bool customProfile = false;
    if (!property_get("encoder.video.profile", value, NULL) > 0) {
        return;
    }

    switch (videoEncoder) {
        case VIDEO_ENCODER_H264:
            if (strncmp("base", value, 4) == 0) {
                videoEncoderProfile = OMX_VIDEO_AVCProfileBaseline;
                ALOGI("H264 Baseline Profile");
            } else if (strncmp("main", value, 4) == 0) {
                videoEncoderProfile = OMX_VIDEO_AVCProfileMain;
                ALOGI("H264 Main Profile");
            } else if (strncmp("high", value, 4) == 0) {
                videoEncoderProfile = OMX_VIDEO_AVCProfileHigh;
                ALOGI("H264 High Profile");
            } else {
                ALOGW("Unsupported H264 Profile");
            }
            break;
        case VIDEO_ENCODER_MPEG_4_SP:
            if (strncmp("simple", value, 5) == 0 ) {
                videoEncoderProfile = OMX_VIDEO_MPEG4ProfileSimple;
                ALOGI("MPEG4 Simple profile");
            } else if (strncmp("asp", value, 3) == 0 ) {
                videoEncoderProfile = OMX_VIDEO_MPEG4ProfileAdvancedSimple;
                ALOGI("MPEG4 Advanced Simple Profile");
            } else {
                ALOGW("Unsupported MPEG4 Profile");
            }
            break;
        default:
            ALOGW("No custom profile support for other codecs");
            break;
    }
}

void QCUtils::setBFrames(
        OMX_VIDEO_PARAM_MPEG4TYPE &mpeg4type, bool &numBFrames) {
    if (mpeg4type.eProfile > OMX_VIDEO_MPEG4ProfileSimple) {
        mpeg4type.nAllowedPictureTypes |= OMX_VIDEO_PictureTypeB;
        mpeg4type.nBFrames = 1;
        mpeg4type.nPFrames /= (mpeg4type.nBFrames + 1);
        numBFrames = mpeg4type.nBFrames;
    }
    return;
}

void QCUtils::setBFrames(
        OMX_VIDEO_PARAM_AVCTYPE &h264type, bool &numBFrames,
        int32_t iFramesInterval, int32_t frameRate) {
    OMX_U32 val = 0;
    if (iFramesInterval < 0) {
        val =  0xFFFFFFFF;
    } else if (iFramesInterval == 0) {
        val = 0;
    } else {
        val  = frameRate * iFramesInterval - 1;
        CHECK(val > 1);
    }

    h264type.nPFrames = val;

    if (h264type.nPFrames == 0) {
        h264type.nAllowedPictureTypes = OMX_VIDEO_PictureTypeI;
    }

    if (h264type.eProfile > OMX_VIDEO_AVCProfileBaseline) {
        h264type.nAllowedPictureTypes |= OMX_VIDEO_PictureTypeB;
        h264type.nBFrames = 1;
        h264type.nPFrames /= (h264type.nBFrames + 1);
        //enable CABAC as default entropy mode for Hihg/Main profiles
        h264type.bEntropyCodingCABAC = OMX_TRUE;
        h264type.nCabacInitIdc = 0;
        numBFrames = h264type.nBFrames;
    }
    return;
}

}

#else //ENABLE_QC_AV_ENHANCEMENTS

namespace android {

void QCUtils::HFR::setHFRIfEnabled(
        const CameraParameters& params, sp<MetaData> &meta) {
}

status_t QCUtils::HFR::reCalculateFileDuration(
        sp<MetaData> &meta, sp<MetaData> &enc_meta,
        int64_t &maxFileDurationUs, int32_t frameRate,
        video_encoder videoEncoder) {
    return OK;
}

void QCUtils::HFR::reCalculateTimeStamp(
        sp<MetaData> &meta, int64_t &timestampUs) {
}

void QCUtils::HFR::reCalculateHFRParams(
        const sp<MetaData> &meta, int32_t &frameRate,
        int32_t &bitrate) {
}

void QCUtils::HFR::copyHFRParams(
        const sp<MetaData> &inputFormat,
        sp<MetaData> &outputFormat) {
}

bool QCUtils::ShellProp::isAudioDisabled() {
    return false;
}

void QCUtils::ShellProp::setEncoderprofile(
        video_encoder &videoEncoder, int32_t &videoEncoderProfile) {
}

void QCUtils::setBFrames(
        OMX_VIDEO_PARAM_MPEG4TYPE &mpeg4type, bool &numBFrames) {
}

void QCUtils::setBFrames(
        OMX_VIDEO_PARAM_AVCTYPE &h264type, bool &numBFrames,
        int32_t iFramesInterval, int32_t frameRate) {
}
}
#endif //ENABLE_QC_AV_ENHANCEMENTS
