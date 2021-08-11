/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkCodec.h"
#include "SkJpegCodec_MTK.h"
#include "SkJpegDecoderMgr_MTK.h"
#include "SkCodecPriv.h"
#include "SkColorData.h"
#include "SkStream.h"
#include "SkTemplates.h"
#include "SkTypes.h"

// stdio is needed for libjpeg-turbo
#include <stdio.h>
#include "SkJpegUtility.h"

#if defined(MTK_JPEG_HW_DECODER) || defined(MTK_JPEG_HW_REGION_RESIZER)
  #if defined(MTK_JPEG_HW_DECODER)
  #include "mhal/MediaHal.h"
  #endif
  #if defined(MTK_JPEG_HW_REGION_RESIZER)
  #include "SkFrontBufferedStream.h"
  #include "DpBlitStream.h"
  #endif
#include <cutils/properties.h>
#include <cutils/log.h>
#undef LOG_TAG
#define LOG_TAG "skia"
#define MAX_APP1_HEADER_SIZE 8 * 1024
#define TO_CEIL(x,a) ( ( (unsigned long)(x) + ((a)-1)) & ~((a)-1) )
void* allocateIONBuffer(int ionClientHnd, ion_user_handle_t *ionAllocHnd, int *bufferFD, size_t size)
{
    int ret;
    void *bufAddr = 0;
    ret = ion_alloc(ionClientHnd, size, 0, ION_HEAP_MULTIMEDIA_MASK, ION_FLAG_CACHED | ION_FLAG_CACHED_NEEDS_SYNC, ionAllocHnd);
    if (ret)
    {
         SkCodecPrintf("allocateIONBuffer ion_alloc failed (%d, %d, %d)\n", ionClientHnd, size, *ionAllocHnd);
         return 0;
    }
    ret = ion_share(ionClientHnd, *ionAllocHnd, bufferFD);
    if (ret)
    {
        SkCodecPrintf("allocateIONBuffer ion_share failed (%d, %d, %d)\n", ionClientHnd, *ionAllocHnd, *bufferFD);
        ion_free(ionClientHnd, *ionAllocHnd);
        return 0;
    }
    bufAddr = ion_mmap(ionClientHnd, 0, size, PROT_READ | PROT_WRITE, MAP_SHARED, *bufferFD, 0);
    if (bufAddr == MAP_FAILED)
    {
        SkCodecPrintf("allocateIONBuffer ion_mmap failed (%d, %d, %d)\n", ionClientHnd, size, *bufferFD);
        ion_share_close(ionClientHnd, *bufferFD);
        ion_free(ionClientHnd, *ionAllocHnd);
        return 0;
    }
#if 0
    IonBufferInfo *info = (IonBufferInfo*)malloc(sizeof(IonBufferInfo));
    bool res;
    info->ionClientHnd = ionClientHnd;
    info->ionAllocHnd = *ionAllocHnd;
    info->size = bm->getSize();
    info->addr = dstBuffer;
    res = bm->installPixels(bm->info(), dstBuffer, bm->rowBytes(), NULL,
            onReleaseIONBuffer, (void *)info);
    if (res == false)
        SkCodecPrintf("onDecodeHW installPixels failed: (%d, %d, %p, %d)\n", info->ionClientHnd, info->ionAllocHnd, info->addr, info->size);
#endif
    return bufAddr;
}
void freeIONBuffer(int ionClientHnd, ion_user_handle_t ionAllocHnd, void* bufferAddr, int bufferFD, size_t size)
{
    if(bufferAddr != NULL)
    {
        int ret = ion_munmap(ionClientHnd, bufferAddr, size);
        if (ret < 0)
            SkCodecPrintf("onDecodeHW ion_munmap failed (%d, %p, %d)\n", ionClientHnd, bufferAddr, size);
    }
    if (bufferFD != -1)
    {
        if (ion_share_close(ionClientHnd, bufferFD))
        {
            SkCodecPrintf("onDecodeHW ion_share_close failed (%d, %d)\n", ionClientHnd, bufferFD);
        }
    }
    if (ion_free(ionClientHnd, ionAllocHnd))
    {
        SkCodecPrintf("onDecodeHW ion_free failed (%d, %d)\n", ionClientHnd, ionAllocHnd);
    }
}
unsigned int getISOSpeedRatings(void *buffer, unsigned int size)
{
    unsigned char *bufPtr = (unsigned char *)buffer;
    unsigned char *TIFFPtr;
    int bytesLeft = size;
    bool findAPP1Marker = false;
    bool findTIFFHeader = false;
    bool isIntelAlign = true;
    bool findEXIFOffset = false;
    bool findISOSpeedRatings = false;
    unsigned int APP1Length;
    unsigned int IFDOffset;
    unsigned int EXIFOffset;
    unsigned int nextIFDOffset;

    /* find APP1 marker*/
    while(bytesLeft > 1)
    {
        if ((0xFF == *bufPtr) && (0xE1 == *(bufPtr + 1)))
        {
            findAPP1Marker = true;
            break;
        }
        bufPtr++;
        bytesLeft--;
    }
    if (findAPP1Marker == true && bytesLeft >= 4)
    {
        // get APP1 marker length as search limit size
        APP1Length = (*(bufPtr + 2) << 8) + *(bufPtr + 3);
        if (bytesLeft > (int)APP1Length)
            bytesLeft = (int)APP1Length;
    }
    else
        return 0;

    /* find TIFF header */
    while(bytesLeft >= 4)
    {
        // Intel type byte alignment
        if (((0x49 == *bufPtr) && (0x49 == *(bufPtr + 1)) &&
            (0x2a == *(bufPtr + 2)) && (0x00 == *(bufPtr + 3))))
        {
            findTIFFHeader = true;
            break;
        }
        // Motorola type byte alignment
        else if (((0x4d == *bufPtr) && (0x4d == *(bufPtr + 1)) &&
            (0x00 == *(bufPtr + 2)) && (0x2a == *(bufPtr + 3))))
        {
            findTIFFHeader = true;
            isIntelAlign = false;
            break;
        }
        bufPtr++;
        bytesLeft--;
    }
    if (findTIFFHeader == true && bytesLeft >= 8)
    {
        // get IFD offset to reach IFD position
        TIFFPtr = bufPtr;
        if (isIntelAlign == true)
            IFDOffset = (*(bufPtr + 7) << 24) + (*(bufPtr + 6) << 16)
                               + (*(bufPtr + 5) << 8) + *(bufPtr + 4);
        else
            IFDOffset = (*(bufPtr + 4) << 24) + (*(bufPtr + 5) << 16)
                        + (*(bufPtr + 6) << 8) + *(bufPtr + 7);
        if (bytesLeft >= (int)IFDOffset)
        {
            bufPtr += IFDOffset;
            bytesLeft -= IFDOffset;
        }
        else
            return 0;
    }
    else
        return 0;

    /* find IFD tag ExifOffset */
    while(findEXIFOffset == false && bytesLeft >= 2)
    {
        unsigned int dirEntries;
        if (isIntelAlign == true)
            dirEntries = (*(bufPtr + 1) << 8) + *bufPtr;
        else
            dirEntries = (*bufPtr << 8) + *(bufPtr + 1);
        bufPtr += 2;
        bytesLeft -= 2;
        while(dirEntries > 0 && bytesLeft >= 12)
        {
            if ((isIntelAlign == true && (0x69 == *bufPtr) && (0x87 == *(bufPtr + 1))) ||
                 (isIntelAlign == false && (0x87 == *bufPtr) && (0x69 == *(bufPtr + 1))))
            {
                if (isIntelAlign == true)
                    EXIFOffset = (*(bufPtr + 11) << 24) + (*(bufPtr + 10) << 16)
                                        + (*(bufPtr + 9) << 8) + *(bufPtr + 8);
                else
                    EXIFOffset = (*(bufPtr + 8) << 24) + (*(bufPtr + 9) << 16)
                                 + (*(bufPtr + 10) << 8) + *(bufPtr + 11);
                if (EXIFOffset - ((unsigned long)bufPtr - (unsigned long)TIFFPtr) > (unsigned long)bytesLeft) // EXIFOffset is invalid, ignore the next step
                    return 0;
                unsigned char *EXIFPtr = TIFFPtr + EXIFOffset;
                bytesLeft -= (unsigned long)EXIFPtr - (unsigned long)bufPtr;
                if (bytesLeft > 0)
                {
                    bufPtr = EXIFPtr;
                    findEXIFOffset = true;
                    break;
                }
                else
                    return 0;
            }
            dirEntries--;
            bufPtr += 12;
            bytesLeft -= 12;
        }

        /* move to next IFD */
        if (dirEntries == 0 && findEXIFOffset == false && bytesLeft >= 4)
        {
            if (isIntelAlign == true)
                nextIFDOffset = (*(bufPtr + 3) << 24) + (*(bufPtr + 2) << 16)
                                        + (*(bufPtr + 1) << 8) + *(bufPtr);
            else
                nextIFDOffset = (*(bufPtr) << 24) + (*(bufPtr + 1) << 16)
                                + (*(bufPtr + 2) << 8) + *(bufPtr + 3);

            // There is no next IFD existed, so abort the searching
            if (nextIFDOffset == 0 || nextIFDOffset > (bufPtr - TIFFPtr + (unsigned int)bytesLeft))
                return 0;
            unsigned char *nextIFDPtr = TIFFPtr + nextIFDOffset;
            bytesLeft -= (unsigned long)nextIFDPtr - (unsigned long)bufPtr;
            if (bytesLeft > 0)
                bufPtr = nextIFDPtr;
            else
                return 0;
        }
    }
    if (findEXIFOffset == true && bytesLeft >= 12)
    {
        unsigned int ISOSpeedRatings = 0;
        unsigned int dirEntries;
        if (isIntelAlign == true)
            dirEntries = (*(bufPtr + 1) << 8) + *bufPtr;
        else
            dirEntries = (*bufPtr << 8) + *(bufPtr + 1);
        bufPtr += 2;
        bytesLeft -= 2;
        while(dirEntries > 0 && bytesLeft >= 2)
        {
            /* find IFD tag ISOSpeedRatings */
            if ((isIntelAlign == true && (0x27 == *bufPtr) && (0x88 == *(bufPtr + 1))) ||
                (isIntelAlign == false && (0x88 == *bufPtr) && (0x27 == *(bufPtr + 1))))
            {
                if (isIntelAlign == true)
                    ISOSpeedRatings = (*(bufPtr + 9) << 8) + *(bufPtr + 8);
                else
                    ISOSpeedRatings = (*(bufPtr + 8) << 8) + *(bufPtr + 9);
                findISOSpeedRatings = true;
                break;
            }
            dirEntries--;
            bufPtr += 12;
            bytesLeft -= 12;
        }
        if (findISOSpeedRatings == true)
            return ISOSpeedRatings;
        else
            return 0;
    }
    else
        return 0;
}
#if defined(MTK_JPEG_HW_DECODER)
static bool onDecodeParser(unsigned char* srcBuffer, unsigned int srcSize, void** jpegDecHandle)
{
    int width, height;
    MHAL_JPEG_DEC_SRC_IN    srcInfo;
    *jpegDecHandle = srcInfo.jpgDecHandle = NULL;
    int result ;
    int try_times = 0;
    srcInfo.srcBuffer = srcBuffer;
    srcInfo.srcLength = srcSize;
    srcInfo.srcFD = 0x0;
    do
    {
        try_times++;
        result = mHalJpeg(MHAL_IOCTL_JPEG_DEC_PARSER, (void *)&srcInfo, sizeof(srcInfo), NULL, 0, NULL);
        if(result == MHAL_INVALID_RESOURCE && try_times < 5)
        {
            SkCodecPrintf("onDecodeParser: HW busy ! Sleep 10ms & try again");
            usleep(10 * 1000);
        }
        else if (MHAL_NO_ERROR != result)
        {
            return false;
        }
    } while(result == MHAL_INVALID_RESOURCE && try_times < 5);
    *jpegDecHandle = srcInfo.jpgDecHandle ;
    return true;
}
bool onDecodeHW(void* srcBuffer, int ionClientHnd, unsigned int srcBufSize, unsigned int srcStreamSize,
                      int srcFD, void* dstBuffer, int width, int height, int rowBytes, SkColorType colorType,
                      void* jpegDecHandle, int tdsp, void* pPPParam, unsigned int ISOSpeed)
{
    MHAL_JPEG_DEC_START_IN inParams;
    memset(&inParams, 0,sizeof(MHAL_JPEG_DEC_START_IN));
    unsigned int enTdshp = 0x1;
    switch (colorType)
    {
        case kRGBA_8888_SkColorType:
            inParams.dstFormat = JPEG_OUT_FORMAT_ARGB8888;
            break;
        case kRGB_565_SkColorType:
            inParams.dstFormat = JPEG_OUT_FORMAT_RGB565;
            break;
        default:
            inParams.dstFormat = JPEG_OUT_FORMAT_ARGB8888;
            break;
    }
    ion_user_handle_t ionAllocHnd;
    int     dstFD = -1;
    void*   dstTmpBuffer = NULL;
    if (srcFD >= 0)
    {
        dstTmpBuffer = allocateIONBuffer(ionClientHnd, &ionAllocHnd, &dstFD, rowBytes * height);
        //SkDebugf("onDecodeHW allocateIONBuffer src:(%d), dst:(%d, %d, %d, %d, %p)",
        //        srcFD, ionClientHnd, ionAllocHnd, dstFD, rowBytes * height, dstTmpBuffer);
    }
    inParams.srcBuffer = (unsigned char*)srcBuffer;
    inParams.srcBufSize = srcBufSize ;
    inParams.srcLength= srcStreamSize;
    inParams.srcFD = srcFD;
    inParams.dstWidth = width;
    inParams.dstHeight = height;
    inParams.dstStride = rowBytes;
    inParams.dstPhysAddr = NULL;
    inParams.dstFD = dstFD;
    if (dstTmpBuffer != NULL && dstFD >= 0)
        inParams.dstVirAddr = (UINT8*) dstTmpBuffer;
    else
        inParams.dstVirAddr = (UINT8*) dstBuffer;
    inParams.doDithering = 0;
    inParams.doRangeDecode = 0;
    inParams.doPostProcessing = 0;
    inParams.doPostProcessing = enTdshp;
    inParams.postProcessingParam = (MHAL_JPEG_POST_PROC_PARAM*)malloc(sizeof(MHAL_JPEG_POST_PROC_PARAM));
#ifdef MTK_IMAGE_DC_SUPPORT
    inParams.postProcessingParam->imageDCParam = this->getDynamicCon();
#else
    inParams.postProcessingParam->imageDCParam = NULL;
#endif
    inParams.postProcessingParam->ISOSpeedRatings = ISOSpeed;
    inParams.PreferQualityOverSpeed = 0;
    inParams.jpgDecHandle = jpegDecHandle ;
    if (MHAL_NO_ERROR != mHalJpeg(MHAL_IOCTL_JPEG_DEC_START,
                                   (void *)&inParams, sizeof(inParams),
                                   NULL, 0, NULL))
    {
        // if dstTmpBuffer is not NULL, need to copy pixels to bitmap and free ION buffer
        if (dstTmpBuffer != NULL)
        {
            memcpy(dstBuffer, dstTmpBuffer, rowBytes * height);
            freeIONBuffer(ionClientHnd, ionAllocHnd, dstTmpBuffer, dstFD, rowBytes * height);
        }
        ALOGW("JPEG HW Decoder return Fail!!\n");
        free(inParams.postProcessingParam);
        return false;
    }

    // if dstTmpBuffer is not NULL, need to copy pixels to bitmap and free ION buffer
    if (dstTmpBuffer != NULL)
    {
        memcpy(dstBuffer, dstTmpBuffer, rowBytes * height);
        freeIONBuffer(ionClientHnd, ionAllocHnd, dstTmpBuffer, dstFD, rowBytes * height);
    }
    free(inParams.postProcessingParam);
    return true;
}
bool getEOImarker(unsigned char* start, unsigned char* end, unsigned int *bs_offset)
{
    unsigned int eoi_flag = 0;
    unsigned char* bs_tail ;
    if((start+1 >= end) || start == NULL || end == NULL)
    {
        ALOGW("SkiaJpeg:getEOImarker find no EOI [%p %p], L:%d!! \n", start, end, __LINE__);
        return false ;
    }
    bs_tail = start+1;
    for( ;bs_tail < end ; bs_tail++)
    {
        if( (*(uint8_t*)(bs_tail-1) == 0xFF) && (*(uint8_t*)(bs_tail) == 0xD9) )
        {
            *bs_offset = bs_tail - start ;
            eoi_flag = 1;
        }
    }
    if(eoi_flag == 0)
    {
        ALOGW("SkiaJpeg:getEOImarker find no EOI [%p %p], L:%d!! \n", start, end, __LINE__);
        return false ;
    }
    else
        return true ;
}
#endif
#if defined(MTK_JPEG_HW_REGION_RESIZER)
bool ImgPostProc(void* src, int ionClientHnd, int srcFD, void* dst, int width, int height, int rowBytes,
                     SkColorType colorType, int tdsp, void* pPPParam, unsigned int ISOSpeed)
{
    if(NULL == dst)
    {
        SkCodecPrintf("ImgPostProc : null pixels");
        return false;
    }
    if((colorType == kRGBA_8888_SkColorType) ||
       (colorType == kRGB_565_SkColorType))
    {
        DpBlitStream bltStream;
        void* src_addr[3];
        unsigned int src_size[3];
        unsigned int plane_num = 1;
        DpColorFormat dp_out_fmt ;
        DpColorFormat dp_in_fmt ;
        unsigned int src_pByte = 4;
        src_addr[0] = src ;
        DP_STATUS_ENUM rst ;
        switch(colorType)
        {
            case kRGBA_8888_SkColorType:
                dp_out_fmt = eRGBX8888;
                src_pByte = 4;
                break;
            case kRGB_565_SkColorType:
                dp_out_fmt = eRGB565;
                src_pByte = 2;
                break;
            default :
                SkCodecPrintf("ImgPostProc : invalid bitmap config %d!!\n", colorType);
                return false;
        }
        dp_in_fmt = dp_out_fmt ;
        src_size[0] = rowBytes * height;
        SkCodecPrintf("ImgPostProc: wh (%d %d)->(%d %d), fmt %d, size %d->%d, regionPQ %d!!\n", width, height, width, height
        , colorType, src_size[0], src_size[0], tdsp);
        DpPqParam pqParam;
        uint32_t* pParam = &pqParam.u.image.info[0];
        pqParam.enable = (tdsp == 0)? false:true;
        pqParam.scenario = MEDIA_PICTURE;
        pqParam.u.image.iso = ISOSpeed;
        if (pPPParam)
        {
            SkCodecPrintf("ImgPostProc: enable imgDc pParam %p", pPPParam);
            pqParam.u.image.withHist = true;
            memcpy((void*)pParam, pPPParam, 20 * sizeof(uint32_t));
        }
        else
            pqParam.u.image.withHist = false;
        bltStream.setPQParameter(pqParam);
        if (srcFD >= 0)
            bltStream.setSrcBuffer(srcFD, src_size, plane_num);
        else
            bltStream.setSrcBuffer((void**)src_addr, src_size, plane_num);
        DpRect src_roi;
        src_roi.x = 0;
        src_roi.y = 0;
        src_roi.w = width;
        src_roi.h = height;
        bltStream.setSrcConfig(width, height, rowBytes, 0, dp_in_fmt, DP_PROFILE_JPEG);

        // set dst buffer
        ion_user_handle_t ionAllocHnd = 0;
        int dstFD = 0;
        void* dstBuffer = NULL;
        // if srcFD >= 0, need to use ion for buffer allocation
        if (srcFD >= 0)
        {
            uint dst_size[1];
            dst_size[0] = src_size[0];
            dstBuffer = allocateIONBuffer(ionClientHnd, &ionAllocHnd, &dstFD, dst_size[0]);
            SkCodecPrintf("ImgPostProc allocateIONBuffer src:(%d), dst:(%d, %d, %d, %d, %p)",
                    srcFD, ionClientHnd, ionAllocHnd, dstFD, dst_size[0], dstBuffer);
            bltStream.setDstBuffer(dstFD, dst_size, 1 );
        }
        else
            bltStream.setDstBuffer(dst, src_size[0] );
        DpRect dst_roi;
        dst_roi.x = 0;
        dst_roi.y = 0;
        dst_roi.w = width;
        dst_roi.h = height;
        bltStream.setDstConfig(width, height, rowBytes, 0, dp_out_fmt, DP_PROFILE_JPEG);
        rst = bltStream.invalidate() ;

        // if dstBuffer is not NULL, need to copy pixels to bitmap and free ION buffer
        if (dstBuffer != NULL)
        {
            memcpy(dst, dstBuffer, src_size[0]);
            freeIONBuffer(ionClientHnd, ionAllocHnd, dstBuffer, dstFD, src_size[0]);
        }
        if ( rst < 0)
        {
            SkCodecPrintf("ImgPostProc: DpBlitStream invalidate failed, L:%d!!\n", __LINE__);
            return false;
        }
        else
        {
            return true ;
        }
    }
    return false;
}
#endif
#endif

// This warning triggers false postives way too often in here.
#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic ignored "-Wclobbered"
#endif

extern "C" {
    #include "jerror_alpha.h"
    #include "jpeglib_alpha.h"
}

bool SkJpegCodec::IsJpeg(const void* buffer, size_t bytesRead) {
    constexpr uint8_t jpegSig[] = { 0xFF, 0xD8, 0xFF };
    return bytesRead >= 3 && !memcmp(buffer, jpegSig, sizeof(jpegSig));
}

static uint32_t get_endian_int(const uint8_t* data, bool littleEndian) {
    if (littleEndian) {
        return (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | (data[0]);
    }

    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | (data[3]);
}

const uint32_t kExifHeaderSize = 14;
const uint32_t kExifMarker = JPEG_APP0 + 1;

static bool is_orientation_marker(jpeg_marker_struct_ALPHA* marker, SkEncodedOrigin* orientation) {
    if (kExifMarker != marker->marker || marker->data_length < kExifHeaderSize) {
        return false;
    }

    constexpr uint8_t kExifSig[] { 'E', 'x', 'i', 'f', '\0' };
    if (memcmp(marker->data, kExifSig, sizeof(kExifSig))) {
        return false;
    }

    // Account for 'E', 'x', 'i', 'f', '\0', '<fill byte>'.
    constexpr size_t kOffset = 6;
    return is_orientation_marker(marker->data + kOffset, marker->data_length - kOffset,
            orientation);
}

bool is_orientation_marker(const uint8_t* data, size_t data_length, SkEncodedOrigin* orientation) {
    bool littleEndian;
    // We need eight bytes to read the endian marker and the offset, below.
    if (data_length < 8 || !is_valid_endian_marker(data, &littleEndian)) {
        return false;
    }

    // Get the offset from the start of the marker.
    // Though this only reads four bytes, use a larger int in case it overflows.
    uint64_t offset = get_endian_int(data + 4, littleEndian);

    // Require that the marker is at least large enough to contain the number of entries.
    if (data_length < offset + 2) {
        return false;
    }
    uint32_t numEntries = get_endian_short(data + offset, littleEndian);

    // Tag (2 bytes), Datatype (2 bytes), Number of elements (4 bytes), Data (4 bytes)
    const uint32_t kEntrySize = 12;
    const auto max = SkTo<uint32_t>((data_length - offset - 2) / kEntrySize);
    numEntries = SkTMin(numEntries, max);

    // Advance the data to the start of the entries.
    data += offset + 2;

    const uint16_t kOriginTag = 0x112;
    const uint16_t kOriginType = 3;
    for (uint32_t i = 0; i < numEntries; i++, data += kEntrySize) {
        uint16_t tag = get_endian_short(data, littleEndian);
        uint16_t type = get_endian_short(data + 2, littleEndian);
        uint32_t count = get_endian_int(data + 4, littleEndian);
        if (kOriginTag == tag && kOriginType == type && 1 == count) {
            uint16_t val = get_endian_short(data + 8, littleEndian);
            if (0 < val && val <= kLast_SkEncodedOrigin) {
                *orientation = (SkEncodedOrigin) val;
                return true;
            }
        }
    }

    return false;
}

static SkEncodedOrigin get_exif_orientation(jpeg_decompress_struct_ALPHA* dinfo) {
    SkEncodedOrigin orientation;
    for (jpeg_marker_struct_ALPHA* marker = dinfo->marker_list; marker; marker = marker->next) {
        if (is_orientation_marker(marker, &orientation)) {
            return orientation;
        }
    }

    return kDefault_SkEncodedOrigin;
}

static bool is_icc_marker(jpeg_marker_struct_ALPHA* marker) {
    if (kICCMarker != marker->marker || marker->data_length < kICCMarkerHeaderSize) {
        return false;
    }

    return !memcmp(marker->data, kICCSig, sizeof(kICCSig));
}

/*
 * ICC profiles may be stored using a sequence of multiple markers.  We obtain the ICC profile
 * in two steps:
 *     (1) Discover all ICC profile markers and verify that they are numbered properly.
 *     (2) Copy the data from each marker into a contiguous ICC profile.
 */
static sk_sp<SkColorSpace> read_color_space(jpeg_decompress_struct_ALPHA* dinfo) {
    // Note that 256 will be enough storage space since each markerIndex is stored in 8-bits.
    jpeg_marker_struct_ALPHA* markerSequence[256];
    memset(markerSequence, 0, sizeof(markerSequence));
    uint8_t numMarkers = 0;
    size_t totalBytes = 0;

    // Discover any ICC markers and verify that they are numbered properly.
    for (jpeg_marker_struct_ALPHA* marker = dinfo->marker_list; marker; marker = marker->next) {
        if (is_icc_marker(marker)) {
            // Verify that numMarkers is valid and consistent.
            if (0 == numMarkers) {
                numMarkers = marker->data[13];
                if (0 == numMarkers) {
                    SkCodecPrintf("ICC Profile Error: numMarkers must be greater than zero.\n");
                    return nullptr;
                }
            } else if (numMarkers != marker->data[13]) {
                SkCodecPrintf("ICC Profile Error: numMarkers must be consistent.\n");
                return nullptr;
            }

            // Verify that the markerIndex is valid and unique.  Note that zero is not
            // a valid index.
            uint8_t markerIndex = marker->data[12];
            if (markerIndex == 0 || markerIndex > numMarkers) {
                SkCodecPrintf("ICC Profile Error: markerIndex is invalid.\n");
                return nullptr;
            }
            if (markerSequence[markerIndex]) {
                SkCodecPrintf("ICC Profile Error: Duplicate value of markerIndex.\n");
                return nullptr;
            }
            markerSequence[markerIndex] = marker;
            SkASSERT(marker->data_length >= kICCMarkerHeaderSize);
            totalBytes += marker->data_length - kICCMarkerHeaderSize;
        }
    }

    if (0 == totalBytes) {
        // No non-empty ICC profile markers were found.
        return nullptr;
    }

    // Combine the ICC marker data into a contiguous profile.
    sk_sp<SkData> iccData = SkData::MakeUninitialized(totalBytes);
    void* dst = iccData->writable_data();
    for (uint32_t i = 1; i <= numMarkers; i++) {
        jpeg_marker_struct_ALPHA* marker = markerSequence[i];
        if (!marker) {
            SkCodecPrintf("ICC Profile Error: Missing marker %d of %d.\n", i, numMarkers);
            return nullptr;
        }

        void* src = SkTAddOffset<void>(marker->data, kICCMarkerHeaderSize);
        size_t bytes = marker->data_length - kICCMarkerHeaderSize;
        memcpy(dst, src, bytes);
        dst = SkTAddOffset<void>(dst, bytes);
    }

    return SkColorSpace::MakeICC(iccData->data(), iccData->size());
}

SkCodec::Result SkJpegCodec::ReadHeader(SkStream* stream, SkCodec** codecOut,
        JpegDecoderMgr_MTK** decoderMgrOut, sk_sp<SkColorSpace> defaultColorSpace) {

#if defined(MTK_JPEG_HW_REGION_RESIZER)
    // assume maximum header size is 256KB
    #define JPEG_HEADER_SIZE 256 * 1024
    if (codecOut && (!stream->hasLength() || !stream->hasPosition()))
    {
        //SkCodecPrintf("SkJpegCodec::ReadHeader wrap stream with SkFrontBufferedStream");
        stream = SkFrontBufferedStream::Make(std::unique_ptr<SkStream>(stream), JPEG_HEADER_SIZE).release();
    }
#endif

    // Create a JpegDecoderMgr to own all of the decompress information
    std::unique_ptr<JpegDecoderMgr_MTK> decoderMgr(new JpegDecoderMgr_MTK(stream));

    // libjpeg errors will be caught and reported here
    skjpeg_error_mgr_MTK::AutoPushJmpBuf jmp(decoderMgr->errorMgr_MTK());
    if (setjmp(jmp)) {
        return decoderMgr->returnFailure_MTK("ReadHeader", kInvalidInput);
    }

    // Initialize the decompress info and the source manager
    decoderMgr->init_MTK();

    // Instruct jpeg library to save the markers that we care about.  Since
    // the orientation and color profile will not change, we can skip this
    // step on rewinds.
    if (codecOut) {
        jpeg_save_markers_ALPHA(decoderMgr->dinfo_MTK(), kExifMarker, 0xFFFF);
        jpeg_save_markers_ALPHA(decoderMgr->dinfo_MTK(), kICCMarker, 0xFFFF);
    }

    // Read the jpeg header
    switch (jpeg_read_header_ALPHA(decoderMgr->dinfo_MTK(), true)) {
        case JPEG_HEADER_OK_ALPHA:
            break;
        case JPEG_SUSPENDED_ALPHA:
            return decoderMgr->returnFailure_MTK("ReadHeader", kIncompleteInput);
        default:
            return decoderMgr->returnFailure_MTK("ReadHeader", kInvalidInput);
    }

    if (codecOut) {
        // Get the encoded color type
        SkEncodedInfo::Color color;
        if (!decoderMgr->getEncodedColor_MTK(&color)) {
            return kInvalidInput;
        }

        // Create image info object and the codec
        SkEncodedInfo info = SkEncodedInfo::Make(color, SkEncodedInfo::kOpaque_Alpha, 8);

        SkEncodedOrigin orientation = get_exif_orientation(decoderMgr->dinfo_MTK());
        sk_sp<SkColorSpace> colorSpace = read_color_space(decoderMgr->dinfo_MTK());
        if (colorSpace) {
            switch (decoderMgr->dinfo_MTK()->jpeg_color_space) {
                case JCS_CMYK_ALPHA:
                case JCS_YCCK_ALPHA:
                    if (colorSpace->type() != SkColorSpace::kCMYK_Type) {
                        colorSpace = nullptr;
                    }
                    break;
                case JCS_GRAYSCALE_ALPHA:
                    if (colorSpace->type() != SkColorSpace::kGray_Type &&
                        colorSpace->type() != SkColorSpace::kRGB_Type)
                    {
                        colorSpace = nullptr;
                    }
                    break;
                default:
                    if (colorSpace->type() != SkColorSpace::kRGB_Type) {
                        colorSpace = nullptr;
                    }
                    break;
            }
        }
        if (!colorSpace) {
            colorSpace = defaultColorSpace;
        }

        const int width = decoderMgr->dinfo_MTK()->image_width;
        const int height = decoderMgr->dinfo_MTK()->image_height;
        SkJpegCodec* codec = new SkJpegCodec(width, height, info, std::unique_ptr<SkStream>(stream),
                                             decoderMgr.release(), std::move(colorSpace),
                                             orientation);
        *codecOut = codec;
    } else {
        SkASSERT(nullptr != decoderMgrOut);
        *decoderMgrOut = decoderMgr.release();
    }
    return kSuccess;
}

std::unique_ptr<SkCodec> SkJpegCodec::MakeFromStream(std::unique_ptr<SkStream> stream,
                                                     Result* result) {
    return SkJpegCodec::MakeFromStream(std::move(stream), result, SkColorSpace::MakeSRGB());
}

std::unique_ptr<SkCodec> SkJpegCodec::MakeFromStream(std::unique_ptr<SkStream> stream,
                                                     Result* result,
                                    sk_sp<SkColorSpace> defaultColorSpace) {
    SkCodec* codec = nullptr;
    *result = ReadHeader(stream.get(), &codec, nullptr, std::move(defaultColorSpace));
    if (kSuccess == *result) {
        // Codec has taken ownership of the stream, we do not need to delete it
        SkASSERT(codec);
        stream.release();
        return std::unique_ptr<SkCodec>(codec);
    }
    return nullptr;
}

SkJpegCodec::SkJpegCodec(int width, int height, const SkEncodedInfo& info,
                         std::unique_ptr<SkStream> stream, JpegDecoderMgr_MTK* decoderMgr,
                         sk_sp<SkColorSpace> colorSpace, SkEncodedOrigin origin)
    : INHERITED(width, height, info, SkColorSpaceXform::kRGBA_8888_ColorFormat, std::move(stream),
                std::move(colorSpace), origin)
    , fDecoderMgr(decoderMgr)
    , fReadyState(decoderMgr->dinfo_MTK()->global_state)
    , fSwizzleSrcRow(nullptr)
    , fColorXformSrcRow(nullptr)
    , fSwizzlerSubset(SkIRect::MakeEmpty())
{
#if defined(MTK_JPEG_HW_DECODER) || defined(MTK_JPEG_HW_REGION_RESIZER)
    fIonClientHnd = ion_open();
    if (fIonClientHnd < 0)
    {
        SkCodecPrintf("ion_open failed\n");
        fIonClientHnd = -1;
    }
    fISOSpeedRatings = -1;
    fIonBufferStorage = NULL;
    fIsSampleDecode = false;
    fSampleDecodeY = 0;
#if defined(MTK_JPEG_HW_REGION_RESIZER)
    fFirstTileDone = false;
    fUseHWResizer = false;
    fEnTdshp = false;
    fRegionHeight = 0x0;
#endif
#endif
}

#if defined(MTK_JPEG_HW_DECODER) || defined(MTK_JPEG_HW_REGION_RESIZER)
SkJpegCodec::~SkJpegCodec()
{
#if defined(MTK_JPEG_HW_DECODER) || defined(MTK_JPEG_HW_REGION_RESIZER)
    if (fIonBufferStorage)
    {
        delete fIonBufferStorage;
        fIonBufferStorage = NULL;
    }
#endif
    if (fIonClientHnd != -1)
        ion_close(fIonClientHnd);
}
#endif

/*
 * Return the row bytes of a particular image type and width
 */
static size_t get_row_bytes(const j_decompress_ptr_ALPHA dinfo) {
    const size_t colorBytes = (dinfo->out_color_space == JCS_RGB565) ? 2 :
            dinfo->out_color_components;
    return dinfo->output_width * colorBytes;

}

/*
 *  Calculate output dimensions based on the provided factors.
 *
 *  Not to be used on the actual jpeg_decompress_struct used for decoding, since it will
 *  incorrectly modify num_components.
 */
void calc_output_dimensions(jpeg_decompress_struct_ALPHA* dinfo, unsigned int num, unsigned int denom) {
    dinfo->num_components = 0;
    dinfo->scale_num = num;
    dinfo->scale_denom = denom;
    jpeg_calc_output_dimensions_ALPHA(dinfo);
}

/*
 * Return a valid set of output dimensions for this decoder, given an input scale
 */
SkISize SkJpegCodec::onGetScaledDimensions(float desiredScale) const {
    // libjpeg-turbo supports scaling by 1/8, 1/4, 3/8, 1/2, 5/8, 3/4, 7/8, and 1/1, so we will
    // support these as well
    unsigned int num;
    unsigned int denom = 8;
    if (desiredScale >= 0.9375) {
        num = 8;
    } else if (desiredScale >= 0.8125) {
        num = 7;
    } else if (desiredScale >= 0.6875f) {
        num = 6;
    } else if (desiredScale >= 0.5625f) {
        num = 5;
    } else if (desiredScale >= 0.4375f) {
        num = 4;
    } else if (desiredScale >= 0.3125f) {
        num = 3;
    } else if (desiredScale >= 0.1875f) {
        num = 2;
    } else {
        num = 1;
    }

    // Set up a fake decompress struct in order to use libjpeg to calculate output dimensions
    jpeg_decompress_struct_ALPHA dinfo;
    sk_bzero(&dinfo, sizeof(dinfo));
    dinfo.image_width = this->getInfo().width();
    dinfo.image_height = this->getInfo().height();
    dinfo.global_state = fReadyState;
    calc_output_dimensions(&dinfo, num, denom);

    // Return the calculated output dimensions for the given scale
    return SkISize::Make(dinfo.output_width, dinfo.output_height);
}

bool SkJpegCodec::onRewind() {
    JpegDecoderMgr_MTK* decoderMgr = nullptr;
    if (kSuccess != ReadHeader(this->stream(), nullptr, &decoderMgr, nullptr)) {
        return fDecoderMgr->returnFalse_MTK("onRewind");
    }
    SkASSERT(nullptr != decoderMgr);
    fDecoderMgr.reset(decoderMgr);

    fSwizzler.reset(nullptr);
    fSwizzleSrcRow = nullptr;
    fColorXformSrcRow = nullptr;
    fStorage.reset();

    return true;
}

/*
 * Checks if the conversion between the input image and the requested output
 * image has been implemented
 * Sets the output color space
 */
bool SkJpegCodec::setOutputColorSpace(const SkImageInfo& dstInfo) {
    if (kUnknown_SkAlphaType == dstInfo.alphaType()) {
        return false;
    }

    if (kOpaque_SkAlphaType != dstInfo.alphaType()) {
        SkCodecPrintf("Warning: an opaque image should be decoded as opaque "
                      "- it is being decoded as non-opaque, which will draw slower\n");
    }

    J_COLOR_SPACE_ALPHA encodedColorType = fDecoderMgr->dinfo_MTK()->jpeg_color_space;

    // Check for valid color types and set the output color space
    switch (dstInfo.colorType()) {
        case kRGBA_8888_SkColorType:
            fDecoderMgr->dinfo_MTK()->out_color_space = JCS_EXT_RGBA_ALPHA;
            break;
        case kBGRA_8888_SkColorType:
            if (this->colorXform()) {
                // Always using RGBA as the input format for color xforms makes the
                // implementation a little simpler.
                fDecoderMgr->dinfo_MTK()->out_color_space = JCS_EXT_RGBA_ALPHA;
            } else {
                fDecoderMgr->dinfo_MTK()->out_color_space = JCS_EXT_BGRA_ALPHA;
            }
            break;
        case kRGB_565_SkColorType:
            if (this->colorXform()) {
                fDecoderMgr->dinfo_MTK()->out_color_space = JCS_EXT_RGBA_ALPHA;
            } else {
                fDecoderMgr->dinfo_MTK()->dither_mode = JDITHER_NONE_ALPHA;
                fDecoderMgr->dinfo_MTK()->out_color_space = JCS_RGB565_ALPHA;
            }
            break;
        case kGray_8_SkColorType:
            if (this->colorXform() || JCS_GRAYSCALE_ALPHA != encodedColorType) {
                return false;
            }

            fDecoderMgr->dinfo_MTK()->out_color_space = JCS_GRAYSCALE_ALPHA;
            break;
        case kRGBA_F16_SkColorType:
            SkASSERT(this->colorXform());

            if (!dstInfo.colorSpace()->gammaIsLinear()) {
                return false;
            }

            fDecoderMgr->dinfo_MTK()->out_color_space = JCS_EXT_RGBA_ALPHA;
            break;
        default:
            return false;
    }

    // Check if we will decode to CMYK.  libjpeg-turbo does not convert CMYK to RGBA, so
    // we must do it ourselves.
    if (JCS_CMYK_ALPHA == encodedColorType || JCS_YCCK_ALPHA == encodedColorType) {
        fDecoderMgr->dinfo_MTK()->out_color_space = JCS_CMYK_ALPHA;
    }

    return true;
}

/*
 * Checks if we can natively scale to the requested dimensions and natively scales the
 * dimensions if possible
 */
bool SkJpegCodec::onDimensionsSupported(const SkISize& size) {
    skjpeg_error_mgr_MTK::AutoPushJmpBuf jmp(fDecoderMgr->errorMgr_MTK());
    if (setjmp(jmp)) {
        return fDecoderMgr->returnFalse_MTK("onDimensionsSupported");
    }

    const unsigned int dstWidth = size.width();
    const unsigned int dstHeight = size.height();

    // Set up a fake decompress struct in order to use libjpeg to calculate output dimensions
    // FIXME: Why is this necessary?
    jpeg_decompress_struct_ALPHA dinfo;
    sk_bzero(&dinfo, sizeof(dinfo));
    dinfo.image_width = this->getInfo().width();
    dinfo.image_height = this->getInfo().height();
    dinfo.global_state = fReadyState;

    // libjpeg-turbo can scale to 1/8, 1/4, 3/8, 1/2, 5/8, 3/4, 7/8, and 1/1
    unsigned int num = 8;
    const unsigned int denom = 8;
    calc_output_dimensions(&dinfo, num, denom);
    while (dinfo.output_width != dstWidth || dinfo.output_height != dstHeight) {

        // Return a failure if we have tried all of the possible scales
        if (1 == num || dstWidth > dinfo.output_width || dstHeight > dinfo.output_height) {
            return false;
        }

        // Try the next scale
        num -= 1;
        calc_output_dimensions(&dinfo, num, denom);
    }

    fDecoderMgr->dinfo_MTK()->scale_num = num;
    fDecoderMgr->dinfo_MTK()->scale_denom = denom;
    return true;
}

int SkJpegCodec::readRows(const SkImageInfo& dstInfo, void* dst, size_t rowBytes, int count,
                          const Options& opts) {
    // Set the jump location for libjpeg-turbo errors
    skjpeg_error_mgr_MTK::AutoPushJmpBuf jmp(fDecoderMgr->errorMgr_MTK());
    if (setjmp(jmp)) {
        return 0;
    }

    // When fSwizzleSrcRow is non-null, it means that we need to swizzle.  In this case,
    // we will always decode into fSwizzlerSrcRow before swizzling into the next buffer.
    // We can never swizzle "in place" because the swizzler may perform sampling and/or
    // subsetting.
    // When fColorXformSrcRow is non-null, it means that we need to color xform and that
    // we cannot color xform "in place" (many times we can, but not when the dst is F16).
    // In this case, we will color xform from fColorXformSrcRow into the dst.
    JSAMPLE* decodeDst = (JSAMPLE*) dst;
    uint32_t* swizzleDst = (uint32_t*) dst;
    size_t decodeDstRowBytes = rowBytes;
    size_t swizzleDstRowBytes = rowBytes;
    int dstWidth = opts.fSubset ? opts.fSubset->width() : dstInfo.width();
    if (fSwizzleSrcRow && fColorXformSrcRow) {
        decodeDst = (JSAMPLE*) fSwizzleSrcRow;
        swizzleDst = fColorXformSrcRow;
        decodeDstRowBytes = 0;
        swizzleDstRowBytes = 0;
        dstWidth = fSwizzler->swizzleWidth();
    } else if (fColorXformSrcRow) {
        decodeDst = (JSAMPLE*) fColorXformSrcRow;
        swizzleDst = fColorXformSrcRow;
        decodeDstRowBytes = 0;
        swizzleDstRowBytes = 0;
    } else if (fSwizzleSrcRow) {
        decodeDst = (JSAMPLE*) fSwizzleSrcRow;
        decodeDstRowBytes = 0;
        dstWidth = fSwizzler->swizzleWidth();
    }

    for (int y = 0; y < count; y++) {
        uint32_t lines = jpeg_read_scanlines_ALPHA(fDecoderMgr->dinfo_MTK(), &decodeDst, 1);
        if (0 == lines) {
            return y;
        }

        if (fSwizzler) {
            fSwizzler->swizzle(swizzleDst, decodeDst);
        }

        if (this->colorXform()) {
            this->applyColorXform(dst, swizzleDst, dstWidth, kOpaque_SkAlphaType);
            dst = SkTAddOffset<void>(dst, rowBytes);
        }

        decodeDst = SkTAddOffset<JSAMPLE>(decodeDst, decodeDstRowBytes);
        swizzleDst = SkTAddOffset<uint32_t>(swizzleDst, swizzleDstRowBytes);
    }

    return count;
}

#if defined(MTK_JPEG_HW_REGION_RESIZER)
int SkJpegCodec::readRows_MTK(const SkImageInfo& dstInfo, void* dst, size_t rowBytes, int count,
                          const Options& opts) {
    // Set the jump location for libjpeg-turbo errors
	skjpeg_error_mgr_MTK::AutoPushJmpBuf jmp(fDecoderMgr->errorMgr_MTK());
    if (setjmp(jmp)) {
        return 0;
    }

    JSAMPLE* tmpBuffer;
    int outputWidth = (fSwizzlerSubset.isEmpty()) ? fDecoderMgr->dinfo_MTK()->output_width : fSwizzlerSubset.width();
    int outputHeight = (fSwizzlerSubset.isEmpty()) ? fDecoderMgr->dinfo_MTK()->output_height :
        (fRegionHeight == 0) ? fSwizzlerSubset.height() : fRegionHeight;
    //size_t expectedStride = (fIonBufferStorage)? outputWidth * SkColorTypeBytesPerPixel(fIonBufferStorage->getColor()) : rowBytes;
    //SkCodecPrintf("SkJpegCodec::readRows_MTK %d %d %d %d %d %d %d %d %d %d %d\n", fDecoderMgr->dinfo_MTK()->output_width, fDecoderMgr->dinfo_MTK()->output_height,
    //    fRegionHeight, fSwizzlerSubset.width(), fSwizzlerSubset.height(), fFirstTileDone, fUseHWResizer, fIsSampleDecode, fSampleDecodeY, rowBytes, count);

    if ((!fFirstTileDone || fUseHWResizer) && fIonBufferStorage)
    {
        // do sampleDecode(full frame decode) => sampleSize does not support by decoder(1,2,4,8)
        // sampleDecode: 1. allocate buffer with size dstRowBytes * fDecoderMgr->dinfo_MTK()->output_height / fSwizzler->sampleX()
        if ((fSwizzler && fSwizzler->sampleX() != 1) && fIsSampleDecode == false)
        {
            fIonBufferStorage->reset(rowBytes * outputHeight / fSwizzler->sampleX());
            tmpBuffer = (JSAMPLE*) fIonBufferStorage->getAddr();
            fIsSampleDecode = true;
            fFirstTileDone = true;
            fUseHWResizer = false;
            SkCodecPrintf("SkJpegCodec::onGetScanlines SampleDecode region(%d, %d), size %d, tmpBuffer %p, dstAddr %p\n",
                outputWidth / fSwizzler->sampleX(),
                outputHeight / fSwizzler->sampleX(),
                rowBytes * outputHeight / fSwizzler->sampleX(),
                fIonBufferStorage->getAddr(), dst);
        }
        // non-sample decode case
        else
        {
            fIonBufferStorage->reset(rowBytes * count);
            tmpBuffer = (JSAMPLE*) fIonBufferStorage->getAddr();
            fIsSampleDecode = false;
        }
    }
    // sampleDecode: 2.1 calculate tmpBuffer with fSampleDecodeY and dstRowBytes
    else if (fIonBufferStorage && fIsSampleDecode == true)
    {
        tmpBuffer = ((JSAMPLE*) fIonBufferStorage->getAddr()) + (fSampleDecodeY * rowBytes);
    }
    else
    {
        // use SW decoder only
        tmpBuffer = (JSAMPLE*) dst;
        fFirstTileDone = true;
        fUseHWResizer = false;
        //SkCodecPrintf("onGetScanlines use SW fFirstTileDone %d, fUseHWResizer %d, bytesToAlloc %d\n",
        //    fFirstTileDone, fUseHWResizer, dstRowBytes * count);
    }

    // When fSwizzleSrcRow is non-null, it means that we need to swizzle.  In this case,
    // we will always decode into fSwizzlerSrcRow before swizzling into the next buffer.
    // We can never swizzle "in place" because the swizzler may perform sampling and/or
    // subsetting.
    // When fColorXformSrcRow is non-null, it means that we need to color xform and that
    // we cannot color xform "in place" (many times we can, but not when the dst is F16).
    // In this case, we will color xform from fColorXformSrcRow into the dst.
    JSAMPLE* decodeDst = (JSAMPLE*) tmpBuffer;
    uint32_t* swizzleDst = (uint32_t*) tmpBuffer;
    JSAMPLE* colorXDst = (JSAMPLE*) tmpBuffer;
    size_t decodeDstRowBytes = rowBytes;
    size_t swizzleDstRowBytes = rowBytes;
    int dstWidth = opts.fSubset ? opts.fSubset->width() : dstInfo.width();
    if (fSwizzleSrcRow && fColorXformSrcRow) {
        decodeDst = (JSAMPLE*) fSwizzleSrcRow;
        swizzleDst = fColorXformSrcRow;
        decodeDstRowBytes = 0;
        swizzleDstRowBytes = 0;
        dstWidth = fSwizzler->swizzleWidth();
    } else if (fColorXformSrcRow) {
        decodeDst = (JSAMPLE*) fColorXformSrcRow;
        swizzleDst = fColorXformSrcRow;
        decodeDstRowBytes = 0;
        swizzleDstRowBytes = 0;
    } else if (fSwizzleSrcRow) {
        decodeDst = (JSAMPLE*) fSwizzleSrcRow;
        decodeDstRowBytes = 0;
        dstWidth = fSwizzler->swizzleWidth();
    }

    for (int y = 0; y < count; y++) {
        uint32_t lines = jpeg_read_scanlines_ALPHA(fDecoderMgr->dinfo_MTK(), &decodeDst, 1);
        if (0 == lines) {
            if(fIonBufferStorage && ((!fFirstTileDone || fUseHWResizer) || fIsSampleDecode == true))
            {
                bool result = false;
                unsigned long addrOffset =0;
                if (fIsSampleDecode == false)
                    result = ImgPostProc(tmpBuffer, fIonClientHnd, fIonBufferStorage->getFD(), dst,
                        outputWidth, y, rowBytes, fIonBufferStorage->getColor(),
                        fEnTdshp, NULL, fISOSpeedRatings);
                else
                {
                    // sampledecode: 3. do ImgPostProc to apply PQ effect
                    addrOffset = fSampleDecodeY * rowBytes;
                    SkCodecPrintf("SkJpegCodec::onGetScanlines ImgPostProc src %p, dst %p, fSampleDecodeY %u\n",
                        tmpBuffer, dst, fSampleDecodeY);
                    result = ImgPostProc(tmpBuffer - addrOffset, fIonClientHnd,
                        fIonBufferStorage->getFD(), (void*)((unsigned char*)dst - addrOffset),
                        outputWidth / fSwizzler->sampleX(), (fSampleDecodeY + y),
                        rowBytes, fIonBufferStorage->getColor(), fEnTdshp, NULL, fISOSpeedRatings);
                    if (result)
                    {
                        fIsSampleDecode = false;
                        fSampleDecodeY = 0;
                    }
                }
                if(!result)
                {
                    fFirstTileDone = true;
                    SkCodecPrintf("ImgPostProc fail, use default solution, L:%d!!\n", __LINE__);
                    if (fIsSampleDecode == false)
                        memcpy(dst, tmpBuffer, rowBytes * y);
                    else
                    {
                        memcpy((void*)((unsigned char*)dst - addrOffset), (void*)(tmpBuffer - addrOffset),
                               rowBytes * (fSampleDecodeY + y));
                        fIsSampleDecode = false;
                        fSampleDecodeY = 0;
                     }
                }
                else
                {
                    fFirstTileDone = true;
                    fUseHWResizer = true;
                    //SkCodecPrintf("ImgPostProc successfully, L:%d!!\n", __LINE__);
                }
            }
            return y;
        }

        if (fSwizzler) {
            fSwizzler->swizzle(swizzleDst, decodeDst);
        }

        if (this->colorXform()) {
            this->applyColorXform(colorXDst, swizzleDst, dstWidth, kOpaque_SkAlphaType);
            if (fIsSampleDecode == false)
                colorXDst = SkTAddOffset<JSAMPLE>(colorXDst, rowBytes);
        }

        decodeDst = SkTAddOffset<JSAMPLE>(decodeDst, decodeDstRowBytes);
        swizzleDst = SkTAddOffset<uint32_t>(swizzleDst, swizzleDstRowBytes);
    }

    if(fIonBufferStorage && ((!fFirstTileDone || fUseHWResizer) ||
        (fIsSampleDecode == true && (fSampleDecodeY + count) == (unsigned int)(outputHeight / fSwizzler->sampleX()))))
    {
        bool result = false;
        unsigned long addrOffset = 0;
        if (fIsSampleDecode == false)
            result = ImgPostProc(tmpBuffer, fIonClientHnd, fIonBufferStorage->getFD(), dst,
                outputWidth, count, rowBytes, fIonBufferStorage->getColor(),
                fEnTdshp, NULL, fISOSpeedRatings);
        // sampledecode: 3. do ImgPostProc to apply PQ effect
        else
        {
            addrOffset = fSampleDecodeY * rowBytes;
            SkCodecPrintf("SkJpegCodec::onGetScanlines ImgPostProc src %p, dst %p, fSampleDecodeY %u\n",
                tmpBuffer, dst, fSampleDecodeY);
            result = ImgPostProc(tmpBuffer - addrOffset, fIonClientHnd,
                fIonBufferStorage->getFD(), (void*)((unsigned char*)dst - addrOffset),
                outputWidth / fSwizzler->sampleX(), (fSampleDecodeY + count),
                rowBytes, fIonBufferStorage->getColor(), fEnTdshp, NULL, fISOSpeedRatings);
            if (result)
            {
                fIsSampleDecode = false;
                fSampleDecodeY = 0;
            }
        }
        if(!result)
        {
            fFirstTileDone = true;
            SkCodecPrintf("ImgPostProc fail, use default solution, L:%d!!\n", __LINE__);
            if (fIsSampleDecode == false)
                memcpy(dst, tmpBuffer, rowBytes * count);
            else
            {
                memcpy((void*)((unsigned char*)dst - addrOffset), (void*)(tmpBuffer - addrOffset),
                       rowBytes * (fSampleDecodeY + count));
                fIsSampleDecode = false;
                fSampleDecodeY = 0;
            }
        }
        else
        {
            fFirstTileDone = true;
            fUseHWResizer = true;
            //SkCodecPrintf("ImgPostProc successfully, L:%d!!\n", __LINE__);
        }
    }
    // sampleDecode: 2.2 record each count size by fSampleDecodeY until reach the target height
    else if (fIsSampleDecode == true)
    {
        fSampleDecodeY = fSampleDecodeY + count;
    }

    return count;
}
#endif

/*
 * This is a bit tricky.  We only need the swizzler to do format conversion if the jpeg is
 * encoded as CMYK.
 * And even then we still may not need it.  If the jpeg has a CMYK color space and a color
 * xform, the color xform will handle the CMYK->RGB conversion.
 */
static inline bool needs_swizzler_to_convert_from_cmyk(J_COLOR_SPACE_ALPHA jpegColorType,
        const SkImageInfo& srcInfo, bool hasColorSpaceXform) {
    if (JCS_CMYK != jpegColorType) {
        return false;
    }

    bool hasCMYKColorSpace = SkColorSpace::kCMYK_Type ==  srcInfo.colorSpace()->type();
    return !hasCMYKColorSpace || !hasColorSpaceXform;
}

/*
 * Performs the jpeg decode
 */
SkCodec::Result SkJpegCodec::onGetPixels(const SkImageInfo& dstInfo,
                                         void* dst, size_t dstRowBytes,
                                         const Options& options,
                                         int* rowsDecoded) {
    if (options.fSubset) {
        // Subsets are not supported.
        return kUnimplemented;
    }

	if (dstInfo.width() >= 400 && dstInfo.height() >= 400)
        SkCodecPrintf("SkJpegCodec::onGetPixels + (%d, %d)", dstInfo.width(), dstInfo.height());

#if defined(MTK_JPEG_HW_DECODER)
    char value[PROPERTY_VALUE_MAX];
    unsigned long u4PQOpt;
    unsigned int enTdshp = 0x0;

    // property control for PQ flag
    property_get("jpegDecode.forceEnable.PQ", value, "-1");
    u4PQOpt = atol(value);
    if (-1 == u4PQOpt)
        enTdshp = (this->getPostProcFlag()) & 0x1;
    else if (0 == u4PQOpt)
        enTdshp = 0x0;
    else
        enTdshp = 0x1;

    if (enTdshp == 0x1)
    {
        bool initMhalJpeg = false;
        bool streamModified = false;
        void* jpegDecHandle = NULL;
        SkStream* stream = this->stream();
        size_t curStreamPosition = stream->getPosition();

        if (stream->rewind())
        {
            size_t streamLength = 0;
            streamModified = true;
            // stream support length, get streamLength directly
            if (stream->hasLength())
                streamLength= stream->getLength();

            if (!fIonBufferStorage)
                fIonBufferStorage = new SkIonMalloc(fIonClientHnd);

            if (fIonBufferStorage)
            {
                #define STREAM_BUFFER_SIZE 1024 * 1024
                size_t bytes_read = 0;
                if (streamLength)
                {
                    fIonBufferStorage->reset(TO_CEIL(streamLength, 512) + 512);
                    bytes_read = stream->read(fIonBufferStorage->getAddr(), streamLength);
                }
                // stream does not support length, need to use temp buffer for loading stream
                else
                {
                    SkIonMalloc tmpReadBuffer(fIonClientHnd);
                    SkIonMalloc tmpBuffer(fIonClientHnd);
                    tmpReadBuffer.reset(STREAM_BUFFER_SIZE);

                    bytes_read = stream->read(tmpReadBuffer.getAddr(), STREAM_BUFFER_SIZE);
                    while(0 != bytes_read)
                    {
                        if (streamLength != 0)
                        {
                            tmpBuffer.reset(streamLength);
                            // copy current bitstream content to a temp buffer
                            memcpy(tmpBuffer.getAddr(), fIonBufferStorage->getAddr(), streamLength);
                        }
                        fIonBufferStorage->reset(TO_CEIL(streamLength + bytes_read, 512) + 512);
                        if (streamLength != 0)
                            // copy content from temp buffer to target buffer
                            memcpy(fIonBufferStorage->getAddr(), tmpBuffer.getAddr(), streamLength);

                        // append read data to the end of target stream buffer
                        memcpy((void*)((unsigned char*)fIonBufferStorage->getAddr() + streamLength), tmpReadBuffer.getAddr(), bytes_read);
                        streamLength += bytes_read;
                        bytes_read = stream->read(tmpReadBuffer.getAddr(), STREAM_BUFFER_SIZE);
                    }
                    SkCodecPrintf("SkJpegCodec::onGetPixels bytes_read %d streamLength %d", bytes_read, streamLength);
                }

                // sanity check for stream data size
                if ((bytes_read == streamLength) || (!stream->hasLength() && streamLength))
                {
                    fIonBufferStorage->setStreamSize(streamLength);
                    fISOSpeedRatings = getISOSpeedRatings(fIonBufferStorage->getAddr(), streamLength);
                    unsigned int bsOffset;
                    if (onDecodeParser((unsigned char*)fIonBufferStorage->getAddr(), streamLength, &jpegDecHandle))
                    {
                        if (getEOImarker((unsigned char*)fIonBufferStorage->getAddr() + (streamLength - 1 - 128),
                                     (unsigned char*)fIonBufferStorage->getAddr() + streamLength, &bsOffset))
                            initMhalJpeg = true;
                        else
                        {
                            SkCodecPrintf("No EOI were found in picture! fallback to SW decoder");
                            if (MHAL_NO_ERROR != mHalJpeg(MHAL_IOCTL_JPEG_DEC_CANCEL, jpegDecHandle, 0, NULL, 0, NULL))
                            {
                                SkCodecPrintf("Can not release JPEG HW Decoder\n");
                            }
                        }
                    }
                    else
                        SkCodecPrintf("onDecodeParser fail! fallback to SW decoder");
                }
                else
                    SkCodecPrintf("bytes_read %d does not match with streamLength %d! fallback to SW decoder",
                        bytes_read, streamLength);
            }
            else
                SkCodecPrintf("fIonBufferStorage allocate fail! fallback to SW decoder");

            // init sucessfully, do mHalJpeg decode
            if (initMhalJpeg)
            {
                SkCodecPrintf("SkJpegCodec::onGetPixels img.info(%d, %d, %d, %d), enTdshp %d, isoSpeed %d",
                              dstInfo.width(), dstInfo.height(), dstRowBytes, dstInfo.colorType(),
                              enTdshp, fISOSpeedRatings);
                if (onDecodeHW(fIonBufferStorage->getAddr(), fIonClientHnd, fIonBufferStorage->getSize(),
                               fIonBufferStorage->getStreamSize(), fIonBufferStorage->getFD(),
                               dst, dstInfo.width(), dstInfo.height(), dstRowBytes, dstInfo.colorType(),
                               jpegDecHandle, enTdshp, NULL, fISOSpeedRatings))
                {
                    if (dstInfo.width() >= 200 && dstInfo.height() >= 200)
                        SkCodecPrintf("SkJpegCodec::onGetPixels -");
                    return kSuccess;
                }
                else
                {
                    SkCodecPrintf("onDecodeHW fail! Use SW decoder instead.");
                }
            }

            // restore stream position to ensure SW decoder can decode normally
            if (streamModified)
            {
                stream->rewind();
                if (curStreamPosition != 0 && !stream->seek(curStreamPosition))
                {
                    SkCodecPrintf("onDecodeHW fail! stream seek also fail! break!!");
                    return kCouldNotRewind;
                }
            }
        }
        else
            SkCodecPrintf("stream->rewind() does not support, fallback to SW decoder");
    }
#endif
    // Get a pointer to the decompress info since we will use it quite frequently
    jpeg_decompress_struct_ALPHA* dinfo = fDecoderMgr->dinfo_MTK();

#if defined(MTK_JPEG_HW_REGION_RESIZER)
    /* this gives about 30% performance improvement. In theory it may
       reduce the visual quality, in practice I'm not seeing a difference
     */
    dinfo->do_fancy_upsampling = 0;

    /* this gives another few percents */
    dinfo->do_block_smoothing = 0;

    if (fFirstTileDone == false)
    {
        long u4PQOpt;
        char value[PROPERTY_VALUE_MAX];

        // property control for PQ flag
        property_get("jpegDecode.forceEnable.PQ", value, "-1");
        u4PQOpt = atol(value);
        if (-1 == u4PQOpt)
            fEnTdshp = (this->getPostProcFlag()) & 0x1;
        else if (0 == u4PQOpt)
            fEnTdshp = 0x0;
        else
            fEnTdshp = 0x1;

        if (!fEnTdshp)
        {
            fFirstTileDone = true;
            fUseHWResizer = false;
        }

    }
    if (fEnTdshp && fISOSpeedRatings == -1)
    {
        SkStream* stream = this->stream();
        size_t curStreamPosition = stream->getPosition();
        SkAutoTMalloc<uint8_t> tmpStorage;
        // record the stream position in order to restore when we need to use SW decoder
        if (stream->hasPosition() && stream->rewind())
        {
            tmpStorage.reset(MAX_APP1_HEADER_SIZE);
            size_t bytes_read = stream->read(tmpStorage.get(), MAX_APP1_HEADER_SIZE);
            fISOSpeedRatings = getISOSpeedRatings(tmpStorage.get(), bytes_read);

            // restore stream position and use original SW decoder
            stream->rewind();
            if (curStreamPosition != 0 && !stream->seek(curStreamPosition))
            {
                SkCodecPrintf("onGetPixels stream seek fail!");
                return kCouldNotRewind;
            }
        }
    }
    SkCodecPrintf("SkJpegCodec::onGetPixels fEnTdshp %d fISOSpeedRatings %d!\n", fEnTdshp, fISOSpeedRatings);

    if(!fFirstTileDone || fUseHWResizer)
    {
        if (!fIonBufferStorage)
            fIonBufferStorage = new SkIonMalloc(fIonClientHnd);
        if (fIonBufferStorage)
            fIonBufferStorage->setColor(dstInfo.colorType());
    }
#endif

    // Set the jump location for libjpeg errors
    skjpeg_error_mgr_MTK::AutoPushJmpBuf jmp(fDecoderMgr->errorMgr_MTK());
    if (setjmp(jmp)) {
        return fDecoderMgr->returnFailure_MTK("setjmp", kInvalidInput);
    }

    // Check if we can decode to the requested destination and set the output color space
    if (!this->setOutputColorSpace(dstInfo)) {
        return fDecoderMgr->returnFailure_MTK("setOutputColorSpace", kInvalidConversion);
    }

    if (!jpeg_start_decompress_ALPHA(dinfo)) {
        return fDecoderMgr->returnFailure_MTK("startDecompress", kInvalidInput);
    }

    // The recommended output buffer height should always be 1 in high quality modes.
    // If it's not, we want to know because it means our strategy is not optimal.
    SkASSERT(1 == dinfo->rec_outbuf_height);

    if (needs_swizzler_to_convert_from_cmyk(dinfo->out_color_space, this->getInfo(),
            this->colorXform())) {
        this->initializeSwizzler(dstInfo, options, true);
    }

    this->allocateStorage(dstInfo);

    int rows = this->readRows_MTK(dstInfo, dst, dstRowBytes, dstInfo.height(), options);
    if (rows < dstInfo.height()) {
        *rowsDecoded = rows;
        return fDecoderMgr->returnFailure_MTK("Incomplete image data", kIncompleteInput);
    }

	if (dstInfo.width() >= 400 && dstInfo.height() >= 400)
        SkCodecPrintf("SkJpegCodec::onGetPixels -");

    return kSuccess;
}

void SkJpegCodec::allocateStorage(const SkImageInfo& dstInfo) {
    int dstWidth = dstInfo.width();

    size_t swizzleBytes = 0;
    if (fSwizzler) {
        swizzleBytes = get_row_bytes(fDecoderMgr->dinfo_MTK());
        dstWidth = fSwizzler->swizzleWidth();
        SkASSERT(!this->colorXform() || SkIsAlign4(swizzleBytes));
    }

    size_t xformBytes = 0;
    if (this->colorXform() && (kRGBA_F16_SkColorType == dstInfo.colorType() ||
                               kRGB_565_SkColorType == dstInfo.colorType())) {
        xformBytes = dstWidth * sizeof(uint32_t);
    }

    size_t totalBytes = swizzleBytes + xformBytes;
    if (totalBytes > 0) {
        fStorage.reset(totalBytes);
        fSwizzleSrcRow = (swizzleBytes > 0) ? fStorage.get() : nullptr;
        fColorXformSrcRow = (xformBytes > 0) ?
                SkTAddOffset<uint32_t>(fStorage.get(), swizzleBytes) : nullptr;
    }
}

void SkJpegCodec::initializeSwizzler(const SkImageInfo& dstInfo, const Options& options,
        bool needsCMYKToRGB) {
    Options swizzlerOptions = options;
    if (options.fSubset) {
        // Use fSwizzlerSubset if this is a subset decode.  This is necessary in the case
        // where libjpeg-turbo provides a subset and then we need to subset it further.
        // Also, verify that fSwizzlerSubset is initialized and valid.
        SkASSERT(!fSwizzlerSubset.isEmpty() && fSwizzlerSubset.x() <= options.fSubset->x() &&
                fSwizzlerSubset.width() == options.fSubset->width());
        swizzlerOptions.fSubset = &fSwizzlerSubset;
    }

    SkImageInfo swizzlerDstInfo = dstInfo;
    if (this->colorXform()) {
        // The color xform will be expecting RGBA 8888 input.
        swizzlerDstInfo = swizzlerDstInfo.makeColorType(kRGBA_8888_SkColorType);
    }

    if (needsCMYKToRGB) {
        // The swizzler is used to convert to from CMYK.
        auto swizzlerInfo = SkEncodedInfo::Make(SkEncodedInfo::kInvertedCMYK_Color,
                                                SkEncodedInfo::kOpaque_Alpha, 8);
        fSwizzler = SkSwizzler::Make(swizzlerInfo, nullptr, swizzlerDstInfo, swizzlerOptions);
    } else {
        int srcBPP = 0;
        switch (fDecoderMgr->dinfo_MTK()->out_color_space) {
            case JCS_EXT_RGBA:
            case JCS_EXT_BGRA:
            case JCS_CMYK:
                srcBPP = 4;
                break;
            case JCS_RGB565:
                srcBPP = 2;
                break;
            case JCS_GRAYSCALE:
                srcBPP = 1;
                break;
            default:
                SkASSERT(false);
                break;
        }
        fSwizzler = SkSwizzler::MakeSimple(srcBPP, swizzlerDstInfo, swizzlerOptions);
    }
    SkASSERT(fSwizzler);
}

SkSampler* SkJpegCodec::getSampler(bool createIfNecessary) {
    if (!createIfNecessary || fSwizzler) {
        SkASSERT(!fSwizzler || (fSwizzleSrcRow && fStorage.get() == fSwizzleSrcRow));
        return fSwizzler.get();
    }

    bool needsCMYKToRGB = needs_swizzler_to_convert_from_cmyk(
            fDecoderMgr->dinfo_MTK()->out_color_space, this->getInfo(), this->colorXform());
    this->initializeSwizzler(this->dstInfo(), this->options(), needsCMYKToRGB);
    this->allocateStorage(this->dstInfo());
    return fSwizzler.get();
}

SkCodec::Result SkJpegCodec::onStartScanlineDecode(const SkImageInfo& dstInfo,
        const Options& options) {
#if defined(MTK_JPEG_HW_REGION_RESIZER)
    if (fFirstTileDone == false)
    {
        long u4PQOpt;
        char value[PROPERTY_VALUE_MAX];

        // property control for PQ flag
        property_get("jpegDecode.forceEnable.PQ", value, "-1");
        u4PQOpt = atol(value);
        if (-1 == u4PQOpt)
            fEnTdshp = (this->getPostProcFlag()) & 0x1;
        else if (0 == u4PQOpt)
            fEnTdshp = 0x0;
        else
            fEnTdshp = 0x1;

        if (!fEnTdshp)
        {
            fFirstTileDone = true;
            fUseHWResizer = false;
        }

        if (fEnTdshp && fISOSpeedRatings == -1)
        {
            SkStream* stream = this->stream();
            size_t curStreamPosition = stream->getPosition();
            SkAutoTMalloc<uint8_t> tmpStorage;
            // record the stream position in order to restore when we need to use SW decoder
            if (stream->hasPosition() && stream->rewind())
            {
                tmpStorage.reset(MAX_APP1_HEADER_SIZE);
                size_t bytes_read = stream->read(tmpStorage.get(), MAX_APP1_HEADER_SIZE);
                fISOSpeedRatings = getISOSpeedRatings(tmpStorage.get(), bytes_read);

                // restore stream position and use original SW decoder
                stream->rewind();
                if (curStreamPosition != 0 && !stream->seek(curStreamPosition))
                {
                    SkCodecPrintf("onStartScanlineDecode stream seek fail!");
                    return kCouldNotRewind;
                }
            }
        }
        SkCodecPrintf("SkJpegCodec::onStartScanlineDecode fEnTdshp %d fISOSpeedRatings %d!\n", fEnTdshp, fISOSpeedRatings);

    }

    if(!fFirstTileDone || fUseHWResizer)
    {
        // create new region which is padding 40 pixels for each boundary
        //SkIRect rectHW;
        //rectHW.set((subset.left() >= 40)? subset.left() - 40 : 0,
        //           (subset.top() >= 40)? subset.top() - 40: 0,
        //           (subset.right() + 40 <= fCodec->getInfo().width())? subset.right() + 40: fCodec->getInfo().width(),
        //           (subset.bottom() + 40 <= fCodec->getInfo().height())? subset.bottom() + 40: fCodec->getInfo().height());

        if (!fIonBufferStorage)
            fIonBufferStorage = new SkIonMalloc(fIonClientHnd);
        if (fIonBufferStorage)
            fIonBufferStorage->setColor(dstInfo.colorType());
    }
#endif

    // Set the jump location for libjpeg errors
    skjpeg_error_mgr_MTK::AutoPushJmpBuf jmp(fDecoderMgr->errorMgr_MTK());
    if (setjmp(jmp)) {
        SkCodecPrintf("setjmp: Error from libjpeg\n");
        return kInvalidInput;
    }

    // Check if we can decode to the requested destination and set the output color space
    if (!this->setOutputColorSpace(dstInfo)) {
        return fDecoderMgr->returnFailure_MTK("setOutputColorSpace", kInvalidConversion);
    }

    if (!jpeg_start_decompress_ALPHA(fDecoderMgr->dinfo_MTK())) {
        SkCodecPrintf("start decompress failed\n");
        return kInvalidInput;
    }

    bool needsCMYKToRGB = needs_swizzler_to_convert_from_cmyk(
            fDecoderMgr->dinfo_MTK()->out_color_space, this->getInfo(), this->colorXform());
    if (options.fSubset) {
        uint32_t startX = options.fSubset->x();
        uint32_t width = options.fSubset->width();

        // libjpeg-turbo may need to align startX to a multiple of the IDCT
        // block size.  If this is the case, it will decrease the value of
        // startX to the appropriate alignment and also increase the value
        // of width so that the right edge of the requested subset remains
        // the same.
        jpeg_crop_scanline_ALPHA(fDecoderMgr->dinfo_MTK(), &startX, &width);

        SkASSERT(startX <= (uint32_t) options.fSubset->x());
        SkASSERT(width >= (uint32_t) options.fSubset->width());
        SkASSERT(startX + width >= (uint32_t) options.fSubset->right());

        // Instruct the swizzler (if it is necessary) to further subset the
        // output provided by libjpeg-turbo.
        //
        // We set this here (rather than in the if statement below), so that
        // if (1) we don't need a swizzler for the subset, and (2) we need a
        // swizzler for CMYK, the swizzler will still use the proper subset
        // dimensions.
        //
        // Note that the swizzler will ignore the y and height parameters of
        // the subset.  Since the scanline decoder (and the swizzler) handle
        // one row at a time, only the subsetting in the x-dimension matters.
        fSwizzlerSubset.setXYWH(options.fSubset->x() - startX, 0,
                options.fSubset->width(), options.fSubset->height());

#if defined(MTK_JPEG_HW_REGION_RESIZER)
        fRegionHeight = options.fRegionHeight;
#endif

        // We will need a swizzler if libjpeg-turbo cannot provide the exact
        // subset that we request.
        if (startX != (uint32_t) options.fSubset->x() ||
                width != (uint32_t) options.fSubset->width()) {
            this->initializeSwizzler(dstInfo, options, needsCMYKToRGB);
        }
    }

    // Make sure we have a swizzler if we are converting from CMYK.
    if (!fSwizzler && needsCMYKToRGB) {
        this->initializeSwizzler(dstInfo, options, true);
    }

    this->allocateStorage(dstInfo);

    return kSuccess;
}

int SkJpegCodec::onGetScanlines(void* dst, int count, size_t dstRowBytes) {
#if defined(MTK_JPEG_HW_REGION_RESIZER)
    int rows = this->readRows_MTK(this->dstInfo(), dst, dstRowBytes, count, this->options());
#else
    int rows = this->readRows(this->dstInfo(), dst, dstRowBytes, count, this->options());
#endif
    if (rows < count) {
        // This allows us to skip calling jpeg_finish_decompress().
        fDecoderMgr->dinfo_MTK()->output_scanline = this->dstInfo().height();
    }

    return rows;
}

bool SkJpegCodec::onSkipScanlines(int count) {
    // Set the jump location for libjpeg errors
    skjpeg_error_mgr_MTK::AutoPushJmpBuf jmp(fDecoderMgr->errorMgr_MTK());
    if (setjmp(jmp)) {
        return fDecoderMgr->returnFalse_MTK("onSkipScanlines");
    }

    return (uint32_t) count == jpeg_skip_scanlines_ALPHA(fDecoderMgr->dinfo_MTK(), count);
}

static bool is_yuv_supported(jpeg_decompress_struct_ALPHA* dinfo) {
    // Scaling is not supported in raw data mode.
    SkASSERT(dinfo->scale_num == dinfo->scale_denom);

    // I can't imagine that this would ever change, but we do depend on it.
    static_assert(8 == DCTSIZE_ALPHA, "DCTSIZE_ALPHA (defined in jpeg library) should always be 8.");

    if (JCS_YCbCr_ALPHA != dinfo->jpeg_color_space) {
        return false;
    }

    SkASSERT(3 == dinfo->num_components);
    SkASSERT(dinfo->comp_info);

    // It is possible to perform a YUV decode for any combination of
    // horizontal and vertical sampling that is supported by
    // libjpeg/libjpeg-turbo.  However, we will start by supporting only the
    // common cases (where U and V have samp_factors of one).
    //
    // The definition of samp_factor is kind of the opposite of what SkCodec
    // thinks of as a sampling factor.  samp_factor is essentially a
    // multiplier, and the larger the samp_factor is, the more samples that
    // there will be.  Ex:
    //     U_plane_width = image_width * (U_h_samp_factor / max_h_samp_factor)
    //
    // Supporting cases where the samp_factors for U or V were larger than
    // that of Y would be an extremely difficult change, given that clients
    // allocate memory as if the size of the Y plane is always the size of the
    // image.  However, this case is very, very rare.
    if  ((1 != dinfo->comp_info[1].h_samp_factor) ||
         (1 != dinfo->comp_info[1].v_samp_factor) ||
         (1 != dinfo->comp_info[2].h_samp_factor) ||
         (1 != dinfo->comp_info[2].v_samp_factor))
    {
        return false;
    }

    // Support all common cases of Y samp_factors.
    // TODO (msarett): As mentioned above, it would be possible to support
    //                 more combinations of samp_factors.  The issues are:
    //                 (1) Are there actually any images that are not covered
    //                     by these cases?
    //                 (2) How much complexity would be added to the
    //                     implementation in order to support these rare
    //                     cases?
    int hSampY = dinfo->comp_info[0].h_samp_factor;
    int vSampY = dinfo->comp_info[0].v_samp_factor;
    return (1 == hSampY && 1 == vSampY) ||
           (2 == hSampY && 1 == vSampY) ||
           (2 == hSampY && 2 == vSampY) ||
           (1 == hSampY && 2 == vSampY) ||
           (4 == hSampY && 1 == vSampY) ||
           (4 == hSampY && 2 == vSampY);
}

bool SkJpegCodec::onQueryYUV8(SkYUVSizeInfo* sizeInfo, SkYUVColorSpace* colorSpace) const {
    jpeg_decompress_struct_ALPHA* dinfo = fDecoderMgr->dinfo_MTK();
    if (!is_yuv_supported(dinfo)) {
        return false;
    }

    jpeg_component_info_ALPHA * comp_info = dinfo->comp_info;
    for (auto i : { SkYUVSizeInfo::kY, SkYUVSizeInfo::kU, SkYUVSizeInfo::kV }) {
        sizeInfo->fSizes[i].set(comp_info[i].downsampled_width, comp_info[i].downsampled_height);
        sizeInfo->fWidthBytes[i] = comp_info[i].width_in_blocks * DCTSIZE_ALPHA;
    }

    if (colorSpace) {
        *colorSpace = kJPEG_SkYUVColorSpace;
    }

    return true;
}

SkCodec::Result SkJpegCodec::onGetYUV8Planes(const SkYUVSizeInfo& sizeInfo, void* planes[3]) {
    SkYUVSizeInfo defaultInfo;

    // This will check is_yuv_supported(), so we don't need to here.
    bool supportsYUV = this->onQueryYUV8(&defaultInfo, nullptr);
    if (!supportsYUV ||
            sizeInfo.fSizes[SkYUVSizeInfo::kY] != defaultInfo.fSizes[SkYUVSizeInfo::kY] ||
            sizeInfo.fSizes[SkYUVSizeInfo::kU] != defaultInfo.fSizes[SkYUVSizeInfo::kU] ||
            sizeInfo.fSizes[SkYUVSizeInfo::kV] != defaultInfo.fSizes[SkYUVSizeInfo::kV] ||
            sizeInfo.fWidthBytes[SkYUVSizeInfo::kY] < defaultInfo.fWidthBytes[SkYUVSizeInfo::kY] ||
            sizeInfo.fWidthBytes[SkYUVSizeInfo::kU] < defaultInfo.fWidthBytes[SkYUVSizeInfo::kU] ||
            sizeInfo.fWidthBytes[SkYUVSizeInfo::kV] < defaultInfo.fWidthBytes[SkYUVSizeInfo::kV]) {
        return fDecoderMgr->returnFailure_MTK("onGetYUV8Planes", kInvalidInput);
    }

    // Set the jump location for libjpeg errors
    skjpeg_error_mgr_MTK::AutoPushJmpBuf jmp(fDecoderMgr->errorMgr_MTK());
    if (setjmp(jmp)) {
        return fDecoderMgr->returnFailure_MTK("setjmp", kInvalidInput);
    }

    // Get a pointer to the decompress info since we will use it quite frequently
    jpeg_decompress_struct_ALPHA* dinfo = fDecoderMgr->dinfo_MTK();

    dinfo->raw_data_out = TRUE;
    if (!jpeg_start_decompress_ALPHA(dinfo)) {
        return fDecoderMgr->returnFailure_MTK("startDecompress", kInvalidInput);
    }

    // A previous implementation claims that the return value of is_yuv_supported()
    // may change after calling jpeg_start_decompress().  It looks to me like this
    // was caused by a bug in the old code, but we'll be safe and check here.
    SkASSERT(is_yuv_supported(dinfo));

    // Currently, we require that the Y plane dimensions match the image dimensions
    // and that the U and V planes are the same dimensions.
    SkASSERT(sizeInfo.fSizes[SkYUVSizeInfo::kU] == sizeInfo.fSizes[SkYUVSizeInfo::kV]);
    SkASSERT((uint32_t) sizeInfo.fSizes[SkYUVSizeInfo::kY].width() == dinfo->output_width &&
            (uint32_t) sizeInfo.fSizes[SkYUVSizeInfo::kY].height() == dinfo->output_height);

    // Build a JSAMPIMAGE to handle output from libjpeg-turbo.  A JSAMPIMAGE has
    // a 2-D array of pixels for each of the components (Y, U, V) in the image.
    // Cheat Sheet:
    //     JSAMPIMAGE == JSAMPLEARRAY* == JSAMPROW** == JSAMPLE***
    JSAMPARRAY_ALPHA yuv[3];

    // Set aside enough space for pointers to rows of Y, U, and V.
    JSAMPROW_ALPHA rowptrs[2 * DCTSIZE_ALPHA + DCTSIZE_ALPHA + DCTSIZE_ALPHA];
    yuv[0] = &rowptrs[0];           // Y rows (DCTSIZE or 2 * DCTSIZE)
    yuv[1] = &rowptrs[2 * DCTSIZE_ALPHA]; // U rows (DCTSIZE)
    yuv[2] = &rowptrs[3 * DCTSIZE_ALPHA]; // V rows (DCTSIZE)

    // Initialize rowptrs.
    int numYRowsPerBlock = DCTSIZE_ALPHA * dinfo->comp_info[0].v_samp_factor;
    for (int i = 0; i < numYRowsPerBlock; i++) {
        rowptrs[i] = SkTAddOffset<JSAMPLE>(planes[SkYUVSizeInfo::kY],
                i * sizeInfo.fWidthBytes[SkYUVSizeInfo::kY]);
    }
    for (int i = 0; i < DCTSIZE_ALPHA; i++) {
        rowptrs[i + 2 * DCTSIZE_ALPHA] = SkTAddOffset<JSAMPLE>(planes[SkYUVSizeInfo::kU],
                i * sizeInfo.fWidthBytes[SkYUVSizeInfo::kU]);
        rowptrs[i + 3 * DCTSIZE_ALPHA] = SkTAddOffset<JSAMPLE>(planes[SkYUVSizeInfo::kV],
                i * sizeInfo.fWidthBytes[SkYUVSizeInfo::kV]);
    }

    // After each loop iteration, we will increment pointers to Y, U, and V.
    size_t blockIncrementY = numYRowsPerBlock * sizeInfo.fWidthBytes[SkYUVSizeInfo::kY];
    size_t blockIncrementU = DCTSIZE_ALPHA * sizeInfo.fWidthBytes[SkYUVSizeInfo::kU];
    size_t blockIncrementV = DCTSIZE_ALPHA * sizeInfo.fWidthBytes[SkYUVSizeInfo::kV];

    uint32_t numRowsPerBlock = numYRowsPerBlock;

    // We intentionally round down here, as this first loop will only handle
    // full block rows.  As a special case at the end, we will handle any
    // remaining rows that do not make up a full block.
    const int numIters = dinfo->output_height / numRowsPerBlock;
    for (int i = 0; i < numIters; i++) {
        JDIMENSION linesRead = jpeg_read_raw_data_ALPHA(dinfo, yuv, numRowsPerBlock);
        if (linesRead < numRowsPerBlock) {
            // FIXME: Handle incomplete YUV decodes without signalling an error.
            return kInvalidInput;
        }

        // Update rowptrs.
        for (int i = 0; i < numYRowsPerBlock; i++) {
            rowptrs[i] += blockIncrementY;
        }
        for (int i = 0; i < DCTSIZE_ALPHA; i++) {
            rowptrs[i + 2 * DCTSIZE_ALPHA] += blockIncrementU;
            rowptrs[i + 3 * DCTSIZE_ALPHA] += blockIncrementV;
        }
    }

    uint32_t remainingRows = dinfo->output_height - dinfo->output_scanline;
    SkASSERT(remainingRows == dinfo->output_height % numRowsPerBlock);
    SkASSERT(dinfo->output_scanline == numIters * numRowsPerBlock);
    if (remainingRows > 0) {
        // libjpeg-turbo needs memory to be padded by the block sizes.  We will fulfill
        // this requirement using a dummy row buffer.
        // FIXME: Should SkCodec have an extra memory buffer that can be shared among
        //        all of the implementations that use temporary/garbage memory?
        SkAutoTMalloc<JSAMPLE> dummyRow(sizeInfo.fWidthBytes[SkYUVSizeInfo::kY]);
        for (int i = remainingRows; i < numYRowsPerBlock; i++) {
            rowptrs[i] = dummyRow.get();
        }
        int remainingUVRows = dinfo->comp_info[1].downsampled_height - DCTSIZE_ALPHA * numIters;
        for (int i = remainingUVRows; i < DCTSIZE_ALPHA; i++) {
            rowptrs[i + 2 * DCTSIZE_ALPHA] = dummyRow.get();
            rowptrs[i + 3 * DCTSIZE_ALPHA] = dummyRow.get();
        }

        JDIMENSION linesRead = jpeg_read_raw_data_ALPHA(dinfo, yuv, numRowsPerBlock);
        if (linesRead < remainingRows) {
            // FIXME: Handle incomplete YUV decodes without signalling an error.
            return kInvalidInput;
        }
    }

    return kSuccess;
}
