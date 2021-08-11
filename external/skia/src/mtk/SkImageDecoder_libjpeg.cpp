/*
 * Copyright (C) 2014 MediaTek Inc.
 * Modification based on code covered by the mentioned copyright
 * and/or permission notice(s).
 */
 /*
 * Copyright 2007 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "SkImageDecoder.h"
#include "SkImageEncoder.h"
#include "SkJpegUtility_MTK.h"
#include "SkJPEGWriteUtility.h"
#include "SkColorPriv.h"
#include "SkDither.h"
#include "SkScaledBitmapSampler.h"
#include "SkStream.h"
#include "SkTemplates.h"
#include "SkTime.h"
#include "SkUtils.h"
#include "SkRTConf.h"
#include "SkRect.h"
#include "SkCanvas.h"
#include "SkMath.h"

#include <sys/mman.h>
#include <cutils/ashmem.h>

#if defined(MTK_JPEG_HW_REGION_RESIZER) || defined(MTK_JPEG_HW_DECODER)
  #include "DpBlitStream.h" 
//  #define ATRACE_TAG ATRACE_TAG_GRAPHICS
  #include "Trace.h"

//  #include <ion/ion.h>
  #include <linux/ion.h>
//  #include <linux/mtk_ion.h>
#endif

#ifdef SK_BUILD_FOR_ANDROID
  #include <cutils/properties.h>
  #include <cutils/log.h>
  #include <stdlib.h>

  #undef LOG_TAG
  #define LOG_TAG "skia" 
  // Key to lookup the size of memory buffer set in system property
  //static const char KEY_MEM_CAP[] = "ro.media.dec.jpeg.memcap";
#endif
#include "SkMutex.h"

// the avoid the reset error when switch hardware to software codec
#define MAX_HEADER_SIZE 192 * 1024
// the limitation of memory to use hardware resize
#define HW_RESIZE_MAX_PIXELS 25 * 1024 * 1024
#define CHECK_LARGE_JPEG_PROG
#define JPEG_PROG_LIMITATION_SIZE MTK_MAX_SRC_JPEG_PROG_PIXELS
#define USE_SKJPGSTREAM 

static SkMutex  gAutoTileInitMutex;
static SkMutex  gAutoTileResizeMutex;

#include <stdio.h>
extern "C" {
    #include "jpeglib_alpha.h"
    #include "jerror_alpha.h"
}

// These enable timing code that report milliseconds for an encoding/decoding
//#define TIME_ENCODE
//#define TIME_DECODE

// this enables our rgb->yuv code, which is faster than libjpeg on ARM
//#define WE_CONVERT_TO_YUV

// If ANDROID_RGB is defined by in the jpeg headers it indicates that jpeg offers
// support for two additional formats (1) JCS_EXT_RGBA and (2) JCS_RGB565.

#if defined(SK_DEBUG)
#define DEFAULT_FOR_SUPPRESS_JPEG_IMAGE_DECODER_WARNINGS false
#define DEFAULT_FOR_SUPPRESS_JPEG_IMAGE_DECODER_ERRORS false
#else  // !defined(SK_DEBUG)
#define DEFAULT_FOR_SUPPRESS_JPEG_IMAGE_DECODER_WARNINGS true
#define DEFAULT_FOR_SUPPRESS_JPEG_IMAGE_DECODER_ERRORS true
#endif  // defined(SK_DEBUG)
SK_CONF_DECLARE(bool, c_suppressJPEGImageDecoderWarnings,
                "images.jpeg.suppressDecoderWarnings",
                DEFAULT_FOR_SUPPRESS_JPEG_IMAGE_DECODER_WARNINGS,
                "Suppress most JPG warnings when calling decode functions.");
SK_CONF_DECLARE(bool, c_suppressJPEGImageDecoderErrors,
                "images.jpeg.suppressDecoderErrors",
                DEFAULT_FOR_SUPPRESS_JPEG_IMAGE_DECODER_ERRORS,
                "Suppress most JPG error messages when decode "
                "function fails.");

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void overwrite_mem_buffer_size(jpeg_decompress_struct_ALPHA* cinfo) {
#ifdef SK_BUILD_FOR_ANDROID
    /* Check if the device indicates that it has a large amount of system memory
     * if so, increase the memory allocation to 30MB instead of the default 5MB.
     */
#ifdef ANDROID_LARGE_MEMORY_DEVICE
    cinfo->mem->max_memory_to_use = 30 * 1024 * 1024;
#else
    cinfo->mem->max_memory_to_use = 5 * 1024 * 1024;
#endif
#endif // SK_BUILD_FOR_ANDROID
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void do_nothing_emit_message(jpeg_common_struct_ALPHA*, int) {
    /* do nothing */
}
static void do_nothing_output_message(j_common_ptr_ALPHA) {
    /* do nothing */
}

static void initialize_info(jpeg_decompress_struct_ALPHA* cinfo, skjpeg_source_mgr_MTK* src_mgr) {
    SkASSERT(cinfo != nullptr);
    SkASSERT(src_mgr != nullptr);
    jpeg_create_decompress_ALPHA(cinfo);
    overwrite_mem_buffer_size(cinfo);
    cinfo->src = src_mgr;
    /* To suppress warnings with a SK_DEBUG binary, set the
     * environment variable "skia_images_jpeg_suppressDecoderWarnings"
     * to "true".  Inside a program that links to skia:
     * SK_CONF_SET("images.jpeg.suppressDecoderWarnings", true); */
    if (c_suppressJPEGImageDecoderWarnings) {
        cinfo->err->emit_message = &do_nothing_emit_message;
    }
    /* To suppress error messages with a SK_DEBUG binary, set the
     * environment variable "skia_images_jpeg_suppressDecoderErrors"
     * to "true".  Inside a program that links to skia:
     * SK_CONF_SET("images.jpeg.suppressDecoderErrors", true); */
    if (c_suppressJPEGImageDecoderErrors) {
        cinfo->err->output_message = &do_nothing_output_message;
    }
}


#ifdef USE_SKJPGSTREAM
class SkJpgStream : public SkStream {

public:
    SkJpgStream(void *hw_buffer, size_t hw_buffer_size, SkStream* Src){
        //SkDebugf("SkJpgStream::SkJpgStream %x, %x, %x!!\n", (unsigned int) hw_buffer, hw_buffer_size, (unsigned int)Src);
        srcStream = Src ;
        hwInputBuf = hw_buffer;
        hwInputBufSize = hw_buffer_size;
        total_read_size = 0;
        fSize = 0;
    }

    virtual ~SkJpgStream(){
        //SkDebugf("SkJpgStream::~SkJpgStream!!\n");		
    }

    virtual bool rewind(){
        //SkDebugf("SkJpgStream::rewind, readSize %x, hwBuffSize %x!!\n",   total_read_size, hwInputBufSize);
        if(total_read_size >= hwInputBufSize)
        {
            return false;
        }
        else if (total_read_size < hwInputBufSize)
        {
            total_read_size = 0;
        }
        return true ;
    }

    virtual bool isAtEnd() const {
        return false;
    }

    virtual size_t read(void* buffer, size_t size){
    size_t read_start = total_read_size;
    size_t read_end = total_read_size + size ;
    size_t size_1 = 0;
    size_t size_2 = 0;
    size_t real_size_2 = 0;

    if (buffer == nullptr && size == 0){
      fSize = hwInputBufSize ;
      if(srcStream) fSize += srcStream->getLength();
      return fSize;
    }else if(size == 0){
      return 0;
    }

    // if buffer is NULL, seek ahead by size

    if( read_start <= hwInputBufSize && read_end <= hwInputBufSize)
    {
        if (buffer) 
        {
            memcpy(buffer, (const char*)hwInputBuf + read_start, size);
        }
        total_read_size += size ;
        //SkDebugf("SkJpgStream::read(HW), size %x, total_size %x!!\n", size, total_read_size);
        return size ;
    }
    else if ( read_start >= hwInputBufSize  )
    {
        if(srcStream) real_size_2 += srcStream->read(buffer, size);
        total_read_size += real_size_2 ;
        //SkDebugf("SkJpgStream::read(Stream), size_2 %x, real_size %x(%x), total_size %x!!\n", size, real_size_2, srcStream->getLength(),total_read_size);
        return real_size_2;
    }
    else
    {
        size_1 = hwInputBufSize - read_start ;
        size_2 = read_end - hwInputBufSize  ;	
        if (buffer) {
            memcpy(buffer, (const char*)hwInputBuf + read_start, size_1);
        }
        total_read_size += size_1 ;
        if(srcStream && buffer) real_size_2 += srcStream->read((void *)((unsigned char *)buffer+size_1), size_2);
        total_read_size += real_size_2 ;
        //SkDebugf("SkJpgStream::read(HW+Stream), buf %x, size_2 %x, real_size %x(%x), total_size %x!!\n", buffer+size_1, size_2, real_size_2, srcStream->getLength(),total_read_size);  
        return size_1+ real_size_2 ;
    }
    }

    bool seek(size_t position){
        //SkDebugf("SkJpgStream::seek size %x!!\n", offset);
        return false;
    }
    size_t skip(size_t size)
    {
       /*Check for size == 0, and just return 0. If we passed that
           to read(), it would interpret it as a request for the entire
           size of the stream.
           */
        //SkDebugf("SkJpgStream::skip %x!!\n", size);
        return size ? this->read(nullptr, size) : 0;
    }

private:
    size_t total_read_size ;
    SkStream* srcStream;
    void *hwInputBuf ;
    size_t hwInputBufSize ; 
    size_t fSize;

};

#define DUMP_DEC_SKIA_LVL_MCU 1
#define DUMP_DEC_SKIA_LVL_IDCT 2
#define DUMP_DEC_SKIA_LVL_COLOR 3

int mtkDumpBuf2file(unsigned int level, const char filename[], unsigned int index, unsigned char *SrcAddr, unsigned int size, unsigned int width, unsigned int height)
{
   
   FILE *fp = nullptr;
   FILE *fpEn = nullptr;
   unsigned char* cptr ;
   const char tag[64] = "DUMP_LIBJPEG";
   char filepath[128];
   char dumpEn[128] ;
   struct timeval t1;

#if 0 //ndef ENABLE_IMG_CODEC_DUMP_RAW
   return false ;
#endif
   
   gettimeofday(&t1, nullptr);
   sprintf(  dumpEn, "//data//otis//%s_%d", tag, level);   
   //if(level == DUMP_DEC_SKIA_LVL_SRC)
   //  sprintf(filepath, "//data//otis//%s_%04d_%u_%d_%d.jpg", filename, index, (unsigned int)t1.tv_usec, width, height );   
   //else
     sprintf(filepath, "//data//otis//%s_%04d_%u_%d_%d.raw", filename, index, (unsigned int)t1.tv_usec, width, height );   
     
     
   fpEn = fopen(dumpEn, "r");
   if(fpEn == nullptr)
   {
       //ALOGW("Check Dump En is zero!!\n");
       return false;
   }
   fclose(fpEn);
      
   fp = fopen(filepath, "w+");
   if (fp == nullptr)
   {
       ALOGW("open Dump file fail: %s\n", filepath);
       return false;
   }

   //ALOGW("DumpRaw -> %s, En %s, addr %x, size %x !!\n", filepath,dumpEn,(unsigned int)SrcAddr, size);                     
   cptr = (unsigned char*)SrcAddr ;
   for( unsigned int i=0;i<size;i++){  /* total size in comp */
     fprintf(fp,"%c", *cptr );  
     cptr++;
   }          
   
   fclose(fp); 
   //*index++;
   
   return true ;       
}

#define MAX_LIBJPEG_AUTO_NUM 32
class JpgLibAutoClean {
public:
    JpgLibAutoClean(): idx(-1) {}
    ~JpgLibAutoClean() {
      int i ;
        for( i = idx ; i>=0 ; i-- ){
          if (ptr[i]) {
              //ALOGW("JpgLibAutoClean: idx %d, clear %x!!\n", i, ptr[i]);
              if(dump[i]) mtkDumpBuf2file(dump[i], "mtkLibJpegRegionIDCT", dump_type[i], (unsigned char *)ptr[i], dump_size[i], dump_w[i], dump_h[i]) ;
              free(ptr[i]);
          }
        }
    }
    void set(void* s) {
        idx ++ ;
        ptr[idx] = s;
        dump[idx] = 0;
        //ALOGW("JpgLibAutoClean: set idx %d, ptr %x!!\n", idx, ptr[idx]);
        
    }
    void setDump(unsigned int dumpStage, unsigned int type,unsigned int size, unsigned int w, unsigned int h){
        dump[idx] = dumpStage ;
        dump_type[idx] = type ;
        dump_size[idx] = size ;
        dump_w[idx] = w ;
        dump_h[idx] = h ;
    }
private:
    void* ptr[MAX_LIBJPEG_AUTO_NUM];
    int idx ;
    unsigned int dump[MAX_LIBJPEG_AUTO_NUM] ;
    unsigned int dump_type[MAX_LIBJPEG_AUTO_NUM] ;
    unsigned int dump_size[MAX_LIBJPEG_AUTO_NUM] ;
    unsigned int dump_w[MAX_LIBJPEG_AUTO_NUM] ;
    unsigned int dump_h[MAX_LIBJPEG_AUTO_NUM] ;
    
};


class JpgStreamAutoClean {
public:
    JpgStreamAutoClean(): ptr(nullptr) {}
    ~JpgStreamAutoClean() {
        if (ptr) {
            delete ptr;
        }
    }
    void set(SkStream* s) {
        ptr = s;
    }
private:
    SkStream* ptr;
};

#endif


#ifdef SK_BUILD_FOR_ANDROID
class SkJPEGImageIndex {
public:
    // Takes ownership of stream.
    SkJPEGImageIndex(SkStreamRewindable* stream, SkImageDecoder* decoder)
        :
#ifdef MTK_SKIA_MULTI_THREAD_JPEG_REGION
        mtkStream(nullptr),
#endif
          fSrcMgr(stream, decoder)
        , fStream(stream)
        , fInfoInitialized(false)
        , fHuffmanCreated(false)
        , fDecompressStarted(false)
        {
            SkDEBUGCODE(fReadHeaderSucceeded = false;)
        }

    ~SkJPEGImageIndex() {
        if (fHuffmanCreated) {
            // Set to false before calling the libjpeg function, in case
            // the libjpeg function calls longjmp. Our setjmp handler may
            // attempt to delete this SkJPEGImageIndex, thus entering this
            // destructor again. Setting fHuffmanCreated to false first
            // prevents an infinite loop.
            fHuffmanCreated = false;
            jpeg_destroy_huffman_index_ALPHA(&fHuffmanIndex);
        }
        if (fDecompressStarted) {
            // Like fHuffmanCreated, set to false before calling libjpeg
            // function to prevent potential infinite loop.
            fDecompressStarted = false;
            jpeg_finish_decompress_ALPHA(&fCInfo);
        }
        if (fInfoInitialized) {
            this->destroyInfo();
        }
#ifdef MTK_SKIA_MULTI_THREAD_JPEG_REGION
        if(mtkStream) delete mtkStream;
#endif
    }

    /**
     *  Destroy the cinfo struct.
     *  After this call, if a huffman index was already built, it
     *  can be used after calling initializeInfoAndReadHeader
     *  again. Must not be called after startTileDecompress except
     *  in the destructor.
     */
    void destroyInfo() {
        SkASSERT(fInfoInitialized);
        SkASSERT(!fDecompressStarted);
        // Like fHuffmanCreated, set to false before calling libjpeg
        // function to prevent potential infinite loop.
        fInfoInitialized = false;
        jpeg_destroy_decompress_ALPHA(&fCInfo);
        SkDEBUGCODE(fReadHeaderSucceeded = false;)
    }

    /**
     *  Initialize the cinfo struct.
     *  Calls jpeg_create_decompress, makes customizations, and
     *  finally calls jpeg_read_header. Returns true if jpeg_read_header
     *  returns JPEG_HEADER_OK.
     *  If cinfo was already initialized, destroyInfo must be called to
     *  destroy the old one. Must not be called after startTileDecompress.
     */
    bool initializeInfoAndReadHeader() {
        SkASSERT(!fInfoInitialized && !fDecompressStarted);
        initialize_info(&fCInfo, &fSrcMgr);
        fInfoInitialized = true;
        const bool success = (JPEG_HEADER_OK == jpeg_read_header_ALPHA(&fCInfo, true));
        SkDEBUGCODE(fReadHeaderSucceeded = success;)
        return success;
    }
      /**
     *  reset stream offset in fSrcMgr
     */

     void resetStream(){
		//SkDebugf("resetStream. reset bytes_in_buffer from %d to %d, and move next_input_byte pointer from %p to %p", fSrcMgr.bytes_in_buffer, fSrcMgr.current_offset, fSrcMgr.next_input_byte, fSrcMgr.start_input_byte);
		fSrcMgr.bytes_in_buffer = fSrcMgr.current_offset;
		fSrcMgr.next_input_byte = fSrcMgr.start_input_byte;
	}

    jpeg_decompress_struct_ALPHA* cinfo() { return &fCInfo; }

    huffman_index_ALPHA* huffmanIndex() { return &fHuffmanIndex; }

    /**
     *  Build the index to be used for tile based decoding.
     *  Must only be called after a successful call to
     *  initializeInfoAndReadHeader and must not be called more
     *  than once.
     */
    bool buildHuffmanIndex() {
        SkASSERT(fReadHeaderSucceeded);
        SkASSERT(!fHuffmanCreated);
        jpeg_create_huffman_index_ALPHA(&fCInfo, &fHuffmanIndex);
        SkASSERT(1 == fCInfo.scale_num && 1 == fCInfo.scale_denom);
        fHuffmanCreated = jpeg_build_huffman_index_ALPHA(&fCInfo, &fHuffmanIndex);
        return fHuffmanCreated;
    }

    /**
     *  Start tile based decoding. Must only be called after a
     *  successful call to buildHuffmanIndex, and must only be
     *  called once.
     */
    bool startTileDecompress() {
        SkASSERT(fHuffmanCreated);
        SkASSERT(fReadHeaderSucceeded);
        SkASSERT(!fDecompressStarted);
        if (jpeg_start_tile_decompress_ALPHA(&fCInfo)) {
            fDecompressStarted = true;
            return true;
        }
        return false;
    }

#ifdef MTK_SKIA_MULTI_THREAD_JPEG_REGION
    SkMemoryStream *mtkStream ;
#endif

private:
    skjpeg_source_mgr_MTK  fSrcMgr;
    SkAutoTDelete<SkStream> fStream;
    jpeg_decompress_struct_ALPHA fCInfo;
    huffman_index_ALPHA fHuffmanIndex;
    bool fInfoInitialized;
    bool fHuffmanCreated;
    bool fDecompressStarted;
    SkDEBUGCODE(bool fReadHeaderSucceeded;)
};
#endif

class SkJPEGImageDecoder : public SkImageDecoder {
public:
#ifdef SK_BUILD_FOR_ANDROID
    SkJPEGImageDecoder() {
        fImageIndex = nullptr;
        fImageWidth = 0;
        fImageHeight = 0;

    #ifdef MTK_SKIA_USE_ION
        fIonClientHnd = ion_open();
        if (fIonClientHnd < 0)
        {
            SkDebugf("ion_open failed\n");
            fIonClientHnd = -1;
        }
    #endif
    }

    virtual ~SkJPEGImageDecoder() override {
        delete fImageIndex;

    #ifdef MTK_SKIA_USE_ION
        if (fIonClientHnd != -1)
            ion_close(fIonClientHnd);
    #endif
    }
#endif

    Format getFormat() const override {
        return kJPEG_Format;
    }

    SkImageInfo getImageInfo(sk_sp<SkColorSpace> prefColorSpace) const override {
        return fEncodedInfo.makeImageInfo(fImageWidth , fImageHeight, std::move(prefColorSpace));
    }

protected:
#ifdef SK_BUILD_FOR_ANDROID
    bool onBuildTileIndex(SkStreamRewindable *stream, int *width, int *height) override;
    bool onDecodeSubset(SkBitmap* bitmap, const SkIRect& rect) override;
#endif
    Result onDecode(SkStream* stream, SkBitmap* bm, Mode) override;
    bool onDecodeYUV8Planes(SkStream* stream, SkISize componentSizes[3],
                            void* planes[3], size_t rowBytes[3],
                            SkYUVColorSpace* colorSpace) override;

private:
#ifdef SK_BUILD_FOR_ANDROID
    SkJPEGImageIndex* fImageIndex;
    int fImageWidth;
    int fImageHeight;
#endif

    /**
     *  Determine the appropriate bitmap colortype and out_color_space based on
     *  both the preference of the caller and the jpeg_color_space on the
     *  jpeg_decompress_struct passed in.
     *  Must be called after jpeg_read_header.
     */
    SkColorType getBitmapColorType(jpeg_decompress_struct_ALPHA*);

    typedef SkImageDecoder INHERITED;

#if defined(MTK_JPEG_HW_DECODER) || defined(MTK_JPEG_HW_REGION_RESIZER)
    bool fFirstTileDone;
    bool fUseHWResizer;
    //int fIonClientHnd = 0;
#endif

    double g_mt_start;
    double g_mt_end;
    double g_mt_end_duration_2;
    double g_mt_hw_sum1;
    double g_mt_hw_sum2;
    int base_thread_id;

#ifdef SK_BUILD_FOR_ANDROID
#ifdef MTK_SKIA_MULTI_THREAD_JPEG_REGION
protected:
    virtual bool onDecodeSubset(SkBitmap* bitmap, const SkIRect& rect, int isampleSize, void* pParam = nullptr) override;
#endif 
#endif
};

//////////////////////////////////////////////////////////////////////////

/* Automatically clean up after throwing an exception */
class JPEGAutoClean {
public:
    JPEGAutoClean(): cinfo_ptr(nullptr) {}
    ~JPEGAutoClean() {
        if (cinfo_ptr) {
            jpeg_destroy_decompress_ALPHA(cinfo_ptr);
        }
    }
    void set(jpeg_decompress_struct_ALPHA* info) {
        cinfo_ptr = info;
    }
private:
    jpeg_decompress_struct_ALPHA* cinfo_ptr;
};

///////////////////////////////////////////////////////////////////////////////

/*  If we need to better match the request, we might examine the image and
     output dimensions, and determine if the downsampling jpeg provided is
     not sufficient. If so, we can recompute a modified sampleSize value to
     make up the difference.

     To skip this additional scaling, just set sampleSize = 1; below.
 */
static int recompute_sampleSize(int sampleSize,
                                const jpeg_decompress_struct_ALPHA& cinfo) {
    return sampleSize * cinfo.output_width / cinfo.image_width;
}

static bool valid_output_dimensions(const jpeg_decompress_struct_ALPHA& cinfo) {
    /* These are initialized to 0, so if they have non-zero values, we assume
       they are "valid" (i.e. have been computed by libjpeg)
     */
    return 0 != cinfo.output_width && 0 != cinfo.output_height;
}

static bool skip_src_rows(jpeg_decompress_struct_ALPHA* cinfo, void* buffer, int count) {
    for (int i = 0; i < count; i++) {
        JSAMPLE_ALPHA* rowptr = (JSAMPLE_ALPHA*)buffer;
        int row_count = jpeg_read_scanlines_ALPHA(cinfo, &rowptr, 1);
        if (1 != row_count) {
            return false;
        }
    }
    return true;
}

#ifdef SK_BUILD_FOR_ANDROID
static bool skip_src_rows_tile(jpeg_decompress_struct_ALPHA* cinfo,
                               huffman_index_ALPHA *index, void* buffer, int count) {
    for (int i = 0; i < count; i++) {
        JSAMPLE_ALPHA* rowptr = (JSAMPLE_ALPHA*)buffer;
        int row_count = jpeg_read_tile_scanline_ALPHA(cinfo, index, &rowptr);
        if (1 != row_count) {
            return false;
        }
    }
    return true;
}
#endif

///////////////////////////////////////////////////////////////////////////////
#ifdef MTK_JPEG_HW_DECODER

typedef struct
{
    ion_user_handle_t ionAllocHnd;
    int               ionClientHnd;
    void*             addr;
    size_t            size;
}IonBufferInfo;

void onReleaseIONBuffer(void* addr, void* context)
{
    IonBufferInfo *info = (IonBufferInfo*)context;
    if(addr != nullptr)
    {
        int ret = ion_munmap(info->ionClientHnd, addr, info->size);
        if (ret < 0)
            SkDebugf("onReleaseIONBuffer: ion_munmap failed (%d, %p, %d)\n", info->ionClientHnd, addr, info->size);
    }

    if (ion_free(info->ionClientHnd, info->ionAllocHnd))
    {
        SkDebugf("onReleaseIONBuffer: ion_free failed (%d, %d)\n", info->ionClientHnd, info->ionAllocHnd);
    }
    free((void*)info);
}

int index_file = 0;
bool store_raw_data(SkBitmap* bm, SkColorType colorType)
{
    FILE *fp;

    char name[150];

    unsigned long u4PQOpt;
    char value[PROPERTY_VALUE_MAX];
    property_get("decode.hw.dump", value, "0");

    u4PQOpt = atol(value);

	skBitmapSize_MTK = bm->height() * bm->rowBytes();

    if( u4PQOpt == 0) return false;

    if(colorType == kRGBA_8888_SkColorType)
        sprintf(name, "/sdcard/dump_%d_%d_%d.888", bm->width(), bm->height(), index_file++);
    else if(colorType == kRGB_565_SkColorType)
        sprintf(name, "/sdcard/dump_%d_%d_%d.565", bm->width(), bm->height(), index_file++);
    else
        return false;

    SkDebugf("store file : %s ", name);


    fp = fopen(name, "wb");
    if(fp == nullptr)
    {
        SkDebugf(" open file error ");
        return false;
    }
    if(colorType == kRGB_565_SkColorType)
    {
        fwrite(bm->getPixels(), 1 , skBitmapSize_MTK, fp);
        fclose(fp);
        return true;
    }

    unsigned char* addr = (unsigned char*)bm->getPixels();
    SkDebugf("bitmap addr : 0x%x, size : %d ", addr, skBitmapSize_MTK);
    for(unsigned int i = 0 ; i < skBitmapSize_MTK ; i += 4)
    {
        fprintf(fp, "%c", addr[i]);
        fprintf(fp, "%c", addr[i+1]);
        fprintf(fp, "%c", addr[i+2]);
    }
    fclose(fp);
    return true;
        
}

#ifdef MTK_SKIA_USE_ION
class SkIonMalloc
{
public:
    SkIonMalloc(int ionClientHnd): fIonAllocHnd(-1), fAddr(nullptr), fShareFD(-1), fSize(0)
    {
        if (ionClientHnd < 0)
        {
            SkDebugf("invalid ionClientHnd(%d)\n", ionClientHnd);
            fIonClientHnd = -1;
        }
        else
            fIonClientHnd = ionClientHnd;
    }
    ~SkIonMalloc()
    {
        free();
    }
    
    void* reset(size_t size) 
    {
        int ret;

        if (fIonClientHnd >= 0)
        {
            if(fAddr != nullptr) 
                free();
    
            fSize = size;
            ret = ion_alloc(fIonClientHnd, size, 0, ION_HEAP_MULTIMEDIA_MASK, ION_FLAG_CACHED | ION_FLAG_CACHED_NEEDS_SYNC, &fIonAllocHnd);
            if (ret)
            {
                SkDebugf("SkIonMalloc::ion_alloc failed (%d, %d, %d)\n", fIonClientHnd, size, fIonAllocHnd);
                return nullptr;
            }
    
            ret = ion_share(fIonClientHnd, fIonAllocHnd, &fShareFD);
            if (ret)
            {
                SkDebugf("SkIonMalloc::ion_share failed (%d, %d, %d)\n", fIonClientHnd, fIonAllocHnd, fShareFD);
                free();
                return nullptr;
            }
    
            fAddr = ion_mmap(fIonClientHnd, 0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fShareFD, 0);
            if (fAddr == MAP_FAILED)
            {
                SkDebugf("SkIonMalloc::ion_mmap failed (%d, %d, %d)\n", fIonClientHnd, size, fShareFD);
                free();
                return nullptr;
            }
            //SkDebugf("SkIonMalloc::reset: %d %d %d %p %x\n", fIonClientHnd, fIonAllocHnd, fShareFD, fAddr, fSize);

            return fAddr;
        }
        else
            return 0;
    }

    void free()
    {
        //SkDebugf("SkIonMalloc::free: %d %d %d %p %x\n", fIonClientHnd, fIonAllocHnd, fShareFD, fAddr, fSize);
        if (fIonClientHnd >= 0)
        {
            if(fAddr != nullptr)
            {
                int ret = ion_munmap(fIonClientHnd, fAddr, fSize);
                if (ret < 0)
                    SkDebugf("SkIonMalloc::ion_munmap failed (%d, %p, %d)\n", fIonClientHnd, fAddr, fSize);
                else
                    fAddr = nullptr;
            }
    
            if (fShareFD != -1)
            {
                if (ion_share_close(fIonClientHnd, fShareFD))
                {
                    SkDebugf("SkIonMalloc::ion_share_close failed (%d, %d)\n", fIonClientHnd, fShareFD);
                }
            }

            if (fIonAllocHnd != -1)
            {
                if (ion_free(fIonClientHnd, fIonAllocHnd))
                {
                    SkDebugf("SkIonMalloc::ion_free failed (fIonClientHnd, fIonAllocHnd)\n");
                }
            }
        }
    }

    void* getAddr() { return fAddr; }
    int getFD() { return fShareFD; }
    
private:
    ion_user_handle_t fIonAllocHnd;
    int     fIonClientHnd;
    void*   fAddr;
    int     fShareFD;
    size_t  fSize;

};

#else
class SkAshmemMalloc
{
public:
    SkAshmemMalloc(): fAddr(nullptr), fFD(-1), fSize(0), fPinned(false) {}
    ~SkAshmemMalloc() { free(); }
    void* reset(size_t size) 
    {
        if(fAddr != nullptr) 
            free();

        fSize = size;
        fFD = ashmem_create_region("decodeSrc", size);
        if (-1 == fFD)
        {
            SkDebugf("------- ashmem create failed %d\n", size);
            return nullptr;
        }

        int err = ashmem_set_prot_region(fFD, PROT_READ | PROT_WRITE);
        if (err) 
        {
            SkDebugf("------ ashmem_set_prot_region(%d) failed %d\n", fFD, err);
            close(fFD);
            return nullptr;
        }

        fAddr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fFD, 0);

        if (-1 == (long)fAddr) 
        {
            fAddr = nullptr;
            SkDebugf("------- mmap failed for ashmem size=%d \n", size);
            close(fFD);
            return nullptr;
        }

        if (ashmem_pin_region(fFD, 0, 0) == ASHMEM_WAS_PURGED)
        {
            fAddr = nullptr;
            SkDebugf("------- ashmem_pin_region failed for ashmem fd=%d \n", fFD);
            close(fFD);
            return nullptr;
        }

        return fAddr;
    }

    void free()
    {
        if(fAddr != nullptr)
        {
            ashmem_unpin_region(fFD, 0, 0);
            munmap(fAddr, fSize);
            close(fFD);
            fAddr = nullptr;
        }
    }

    void* getAddr() { return fAddr; }
    int getFD() { return fFD; }
    
private:
    void*   fAddr;
    int     fFD;
    size_t  fSize;
    bool    fPinned;

};
#endif

extern unsigned int getISOSpeedRatings(void *buffer, unsigned int size);

#endif

/* return current time in milliseconds */
static double now_ms(void) {

    struct timespec res;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID , &res); //CLOCK_REALTIME
    return 1000.0 * res.tv_sec + (double) res.tv_nsec / 1e6;    
}


// This guy exists just to aid in debugging, as it allows debuggers to just
// set a break-point in one place to see all error exists.
static void print_jpeg_decoder_errors(const jpeg_decompress_struct_ALPHA& cinfo,
                         int width, int height, const char caller[]) {
    if (!(c_suppressJPEGImageDecoderErrors)) {
        char buffer[JMSG_LENGTH_MAX];
        cinfo.err->format_message((const j_common_ptr_ALPHA)&cinfo, buffer);
        SkDebugf("libjpeg error %d <%s> from %s [%d %d]\n",
                 cinfo.err->msg_code, buffer, caller, width, height);
    }
}

static bool return_false(const jpeg_decompress_struct_ALPHA& cinfo,
                         const char caller[]) {
    print_jpeg_decoder_errors(cinfo, 0, 0, caller);
    return false;
}

#ifdef SK_BUILD_FOR_ANDROID
static bool return_false(const jpeg_decompress_struct_ALPHA& cinfo,
                         const SkBitmap& bm, const char caller[]) {
    print_jpeg_decoder_errors(cinfo, bm.width(), bm.height(), caller);
    return false;
}
#endif

static SkImageDecoder::Result return_failure(const jpeg_decompress_struct_ALPHA& cinfo,
                                             const SkBitmap& bm, const char caller[]) {
    print_jpeg_decoder_errors(cinfo, bm.width(), bm.height(), caller);
    return SkImageDecoder::kFailure;
}

///////////////////////////////////////////////////////////////////////////////

// Convert a scanline of CMYK samples to RGBX in place. Note that this
// method moves the "scanline" pointer in its processing
static void convert_CMYK_to_RGB(uint8_t* scanline, unsigned int width) {
    // At this point we've received CMYK pixels from libjpeg. We
    // perform a crude conversion to RGB (based on the formulae
    // from easyrgb.com):
    //  CMYK -> CMY
    //    C = ( C * (1 - K) + K )      // for each CMY component
    //  CMY -> RGB
    //    R = ( 1 - C ) * 255          // for each RGB component
    // Unfortunately we are seeing inverted CMYK so all the original terms
    // are 1-. This yields:
    //  CMYK -> CMY
    //    C = ( (1-C) * (1 - (1-K) + (1-K) ) -> C = 1 - C*K
    // The conversion from CMY->RGB remains the same
    for (unsigned int x = 0; x < width; ++x, scanline += 4) {
        scanline[0] = SkMulDiv255Round(scanline[0], scanline[3]);
        scanline[1] = SkMulDiv255Round(scanline[1], scanline[3]);
        scanline[2] = SkMulDiv255Round(scanline[2], scanline[3]);
        scanline[3] = 255;
    }
}

/**
 *  Common code for setting the error manager.
 */
static void set_error_mgr(jpeg_decompress_struct_ALPHA* cinfo, skjpeg_error_mgr_MTK* errorManager) {
    SkASSERT(cinfo != nullptr);
    SkASSERT(errorManager != nullptr);
    cinfo->err = jpeg_std_error_ALPHA(errorManager);
    errorManager->error_exit = skjpeg_err_exit_MTK;
}

/**
 *  Common code for turning off upsampling and smoothing. Turning these
 *  off helps performance without showing noticable differences in the
 *  resulting bitmap.
 */
static void turn_off_visual_optimizations(jpeg_decompress_struct_ALPHA* cinfo) {
    SkASSERT(cinfo != nullptr);
    /* this gives about 30% performance improvement. In theory it may
       reduce the visual quality, in practice I'm not seeing a difference
     */
    cinfo->do_fancy_upsampling = 0;

    /* this gives another few percents */
    cinfo->do_block_smoothing = 0;
}

/**
 * Common code for setting the dct method.
 */
static void set_dct_method(const SkImageDecoder& decoder, jpeg_decompress_struct_ALPHA* cinfo) {
    SkASSERT(cinfo != nullptr);
#ifdef DCT_IFAST_SUPPORTED
    if (decoder.getPreferQualityOverSpeed()) {
        cinfo->dct_method = JDCT_ISLOW_ALPHA;
    } else {
        cinfo->dct_method = JDCT_IFAST_ALPHA;
    }
#else
    cinfo->dct_method = JDCT_ISLOW_ALPHA;
#endif
}

SkColorType SkJPEGImageDecoder::getBitmapColorType(jpeg_decompress_struct_ALPHA* cinfo) {
    SkASSERT(cinfo != nullptr);

    SrcDepth srcDepth = k32Bit_SrcDepth;
    if (JCS_GRAYSCALE_ALPHA == cinfo->jpeg_color_space) {
        srcDepth = k8BitGray_SrcDepth;
    }

    SkColorType colorType = this->getPrefColorType(srcDepth, /*hasAlpha*/ false);
    switch (colorType) {
        case kAlpha_8_SkColorType:
            // Only respect A8 colortype if the original is grayscale,
            // in which case we will treat the grayscale as alpha
            // values.
            if (cinfo->jpeg_color_space != JCS_GRAYSCALE_ALPHA) {
                colorType = kN32_SkColorType;
            }
            break;
        case kN32_SkColorType:
            // Fall through.
        case kARGB_4444_SkColorType:
            // Fall through.
        case kRGB_565_SkColorType:
            // These are acceptable destination colortypes.
            break;
        default:
            // Force all other colortypes to 8888.
            colorType = kN32_SkColorType;
            break;
    }

    switch (cinfo->jpeg_color_space) {
        case JCS_CMYK_ALPHA:
            // Fall through.
        case JCS_YCCK_ALPHA:
            // libjpeg cannot convert from CMYK or YCCK to RGB - here we set up
            // so libjpeg will give us CMYK samples back and we will later
            // manually convert them to RGB
            cinfo->out_color_space = JCS_CMYK_ALPHA;
            break;
        case JCS_GRAYSCALE_ALPHA:
            if (kAlpha_8_SkColorType == colorType) {
                cinfo->out_color_space = JCS_GRAYSCALE_ALPHA;
                break;
            }
            // The data is JCS_GRAYSCALE, but the caller wants some sort of RGB
            // colortype. Fall through to set to the default.
        default:
            cinfo->out_color_space = JCS_RGB_ALPHA;
            break;
    }
    return colorType;
}

/**
 *  Based on the colortype and dither mode, adjust out_color_space and
 *  dither_mode of cinfo. Only does work in ANDROID_RGB
 */
static void adjust_out_color_space_and_dither(jpeg_decompress_struct_ALPHA* cinfo,
                                              SkColorType colorType,
                                              const SkImageDecoder& decoder) {
    SkASSERT(cinfo != nullptr);
#ifdef ANDROID_RGB
    cinfo->dither_mode = JDITHER_NONE;
    if (JCS_CMYK_ALPHA == cinfo->out_color_space) {
        return;
    }
    switch (colorType) {
        case kN32_SkColorType:
            cinfo->out_color_space = JCS_EXT_RGBA_ALPHA;
            break;
        case kRGB_565_SkColorType:
            cinfo->out_color_space = JCS_RGB565_ALPHA;
            if (decoder.getDitherImage()) {
                cinfo->dither_mode = JDITHER_ORDERED_ALPHA;
            }
            break;
        default:
            break;
    }
#endif
}

/**
   Sets all pixels in given bitmap to SK_ColorWHITE for all rows >= y.
   Used when decoding fails partway through reading scanlines to fill
   remaining lines. */
static void fill_below_level(int y, SkBitmap* bitmap) {
    SkIRect rect = SkIRect::MakeLTRB(0, y, bitmap->width(), bitmap->height());
    SkCanvas canvas(*bitmap);
    canvas.clipRect(SkRect::Make(rect));
    canvas.drawColor(SK_ColorWHITE);
}

/**
 *  Get the config and bytes per pixel of the source data. Return
 *  whether the data is supported.
 */
static bool get_src_config(const jpeg_decompress_struct_ALPHA& cinfo,
                           SkScaledBitmapSampler::SrcConfig* sc,
                           int* srcBytesPerPixel) {
    SkASSERT(sc != nullptr && srcBytesPerPixel != nullptr);
    if (JCS_CMYK_ALPHA == cinfo.out_color_space) {
        // In this case we will manually convert the CMYK values to RGB
        *sc = SkScaledBitmapSampler::kRGBX;
        // The CMYK work-around relies on 4 components per pixel here
        *srcBytesPerPixel = 4;
    } else if (3 == cinfo.out_color_components && JCS_RGB_ALPHA == cinfo.out_color_space) {
        *sc = SkScaledBitmapSampler::kRGB;
        *srcBytesPerPixel = 3;
#ifdef ANDROID_RGB
    } else if (JCS_EXT_RGBA_ALPHA == cinfo.out_color_space) {
        *sc = SkScaledBitmapSampler::kRGBX;
        *srcBytesPerPixel = 4;
    } else if (JCS_RGB565_ALPHA == cinfo.out_color_space) {
        *sc = SkScaledBitmapSampler::kRGB_565;
        *srcBytesPerPixel = 2;
#endif
    } else if (1 == cinfo.out_color_components &&
               JCS_GRAYSCALE_ALPHA == cinfo.out_color_space) {
        *sc = SkScaledBitmapSampler::kGray;
        *srcBytesPerPixel = 1;
    } else {
        return false;
    }
    return true;
}

SkImageDecoder::Result SkJPEGImageDecoder::onDecode(SkStream* stream, SkBitmap* bm, Mode mode) {
#ifdef TIME_DECODE
    SkAutoTime atm("JPEG Decode");
#endif

    JPEGAutoClean autoClean;

    jpeg_decompress_struct_ALPHA cinfo;
    skjpeg_source_mgr_MTK srcManager(stream, this);

    skjpeg_error_mgr_MTK errorManager;
    set_error_mgr(&cinfo, &errorManager);

    // All objects need to be instantiated before this setjmp call so that
    // they will be cleaned up properly if an error occurs.
    skjpeg_error_mgr_MTK::AutoPushJmpBuf jmp(&errorManager);
    if (setjmp(jmp)) {
        return return_failure(cinfo, *bm, "setjmp");
    }

    initialize_info(&cinfo, &srcManager);
    autoClean.set(&cinfo);

    int status = jpeg_read_header_ALPHA(&cinfo, true);
    if (status != JPEG_HEADER_OK) {
        return return_failure(cinfo, *bm, "read_header");
    }

#ifdef CHECK_LARGE_JPEG_PROG 
    if (SkImageDecoder::kDecodeBounds_Mode != mode)
    {
        if(cinfo.progressive_mode && (cinfo.image_width * cinfo.image_height > JPEG_PROG_LIMITATION_SIZE) )
        {
            SkDebugf("too Large Progressive Image (%d, %d x %d)> limit(%d)", cinfo.progressive_mode,cinfo.image_width, cinfo.image_height, JPEG_PROG_LIMITATION_SIZE);
            return return_failure(cinfo, *bm, "Not support too Large Progressive Image!!");
        }
    }
#endif

    /*  Try to fulfill the requested sampleSize. Since jpeg can do it (when it
        can) much faster that we, just use their num/denom api to approximate
        the size.
    */
    int sampleSize = this->getSampleSize();

    set_dct_method(*this, &cinfo);

    SkASSERT(1 == cinfo.scale_num);
    cinfo.scale_denom = sampleSize;

    turn_off_visual_optimizations(&cinfo);

    const SkColorType colorType = this->getBitmapColorType(&cinfo);
    const SkAlphaType alphaType = kAlpha_8_SkColorType == colorType ?
                                      kPremul_SkAlphaType : kOpaque_SkAlphaType;

    adjust_out_color_space_and_dither(&cinfo, colorType, *this);
    SkDebugf("jpeg_decoder mode %d, colorType %d, w %d, h %d, sample %d, bsLength %x!!\n",mode,colorType,cinfo.image_width, cinfo.image_height, sampleSize, stream->getLength());

    if (1 == sampleSize && SkImageDecoder::kDecodeBounds_Mode == mode) {
        // Assume an A8 bitmap is not opaque to avoid the check of each
        // individual pixel. It is very unlikely to be opaque, since
        // an opaque A8 bitmap would not be very interesting.
        // Otherwise, a jpeg image is opaque.
        bool success = bm->setInfo(SkImageInfo::Make(cinfo.image_width, cinfo.image_height,
                                                     colorType, alphaType));
        return success ? kSuccess : kFailure;
    }

    /*  image_width and image_height are the original dimensions, available
        after jpeg_read_header(). To see the scaled dimensions, we have to call
        jpeg_start_decompress(), and then read output_width and output_height.
    */
    if (!jpeg_start_decompress_ALPHA((jpeg_decompress_struct_ALPHA*)(&cinfo))) {
        /*  If we failed here, we may still have enough information to return
            to the caller if they just wanted (subsampled bounds). If sampleSize
            was 1, then we would have already returned. Thus we just check if
            we're in kDecodeBounds_Mode, and that we have valid output sizes.

            One reason to fail here is that we have insufficient stream data
            to complete the setup. However, output dimensions seem to get
            computed very early, which is why this special check can pay off.
         */
        if (SkImageDecoder::kDecodeBounds_Mode == mode && valid_output_dimensions(cinfo)) {
            SkScaledBitmapSampler smpl(cinfo.output_width, cinfo.output_height,
                                       recompute_sampleSize(sampleSize, cinfo));
            // Assume an A8 bitmap is not opaque to avoid the check of each
            // individual pixel. It is very unlikely to be opaque, since
            // an opaque A8 bitmap would not be very interesting.
            // Otherwise, a jpeg image is opaque.
            bool success = bm->setInfo(SkImageInfo::Make(smpl.scaledWidth(), smpl.scaledHeight(),
                                                         colorType, alphaType));
            return success ? kSuccess : kFailure;
        } else {
            return return_failure(cinfo, *bm, "start_decompress");
        }
    }
    sampleSize = recompute_sampleSize(sampleSize, cinfo);

    SkScaledBitmapSampler sampler(cinfo.output_width, cinfo.output_height, sampleSize);
    // Assume an A8 bitmap is not opaque to avoid the check of each
    // individual pixel. It is very unlikely to be opaque, since
    // an opaque A8 bitmap would not be very interesting.
    // Otherwise, a jpeg image is opaque.
    bm->setInfo(SkImageInfo::Make(sampler.scaledWidth(), sampler.scaledHeight(),
                                  colorType, alphaType));
    if (SkImageDecoder::kDecodeBounds_Mode == mode) {
        return kSuccess;
    }
    if (!this->allocPixelRef(bm, nullptr)) {
        return return_failure(cinfo, *bm, "allocPixelRef");
    }

    //SkAutoLockPixels alp(*bm);

#ifdef ANDROID_RGB
    /* short-circuit the SkScaledBitmapSampler when possible, as this gives
       a significant performance boost.
    */
    if (sampleSize == 1 &&
        ((kN32_SkColorType == colorType && cinfo.out_color_space == JCS_EXT_RGBA_ALPHA) ||
         (kRGB_565_SkColorType == colorType && cinfo.out_color_space == JCS_RGB565_ALPHA)))
    {
        JSAMPLE_ALPHA* rowptr = (JSAMPLE_ALPHA*)bm->getPixels();
        INT32 const bpr =  bm->rowBytes();

        while (cinfo.output_scanline < cinfo.output_height) {
            int row_count = jpeg_read_scanlines_ALPHA(&cinfo, &rowptr, 1);
            if (0 == row_count) {
                // if row_count == 0, then we didn't get a scanline,
                // so return early.  We will return a partial image.
                fill_below_level(cinfo.output_scanline, bm);
                cinfo.output_scanline = cinfo.output_height;
                jpeg_finish_decompress_ALPHA(&cinfo);
                return kPartialSuccess;
            }
            if (this->shouldCancelDecode()) {
                return return_failure(cinfo, *bm, "shouldCancelDecode");
            }
            rowptr += bpr;
        }
        jpeg_finish_decompress_ALPHA(&cinfo);
        SkDebugf("jpeg_decoder finish successfully, L:%d!!!\n",__LINE__);
        return kSuccess;
    }
#endif

    // check for supported formats
    SkScaledBitmapSampler::SrcConfig sc;
    int srcBytesPerPixel;

    if (!get_src_config(cinfo, &sc, &srcBytesPerPixel)) {
        return return_failure(cinfo, *bm, "jpeg colorspace");
    }

    if (!sampler.begin(bm, sc, *this)) {
        return return_failure(cinfo, *bm, "sampler.begin");
    }

    SkAutoTMalloc<uint8_t> srcStorage(cinfo.output_width * srcBytesPerPixel);
    uint8_t* srcRow = (uint8_t*)srcStorage.get();

    //  Possibly skip initial rows [sampler.srcY0]
    if (!skip_src_rows(&cinfo, srcRow, sampler.srcY0())) {
        return return_failure(cinfo, *bm, "skip rows");
    }

    // now loop through scanlines until y == bm->height() - 1
    for (int y = 0;; y++) {
        JSAMPLE_ALPHA* rowptr = (JSAMPLE_ALPHA*)srcRow;
        int row_count = jpeg_read_scanlines_ALPHA(&cinfo, &rowptr, 1);
        if (0 == row_count) {
            // if row_count == 0, then we didn't get a scanline,
            // so return early.  We will return a partial image.
            fill_below_level(y, bm);
            cinfo.output_scanline = cinfo.output_height;
            jpeg_finish_decompress_ALPHA(&cinfo);
            return kPartialSuccess;
        }
        if (this->shouldCancelDecode()) {
            return return_failure(cinfo, *bm, "shouldCancelDecode");
        }

        if (JCS_CMYK_ALPHA == cinfo.out_color_space) {
            convert_CMYK_to_RGB(srcRow, cinfo.output_width);
        }

        sampler.next(srcRow);
        if (bm->height() - 1 == y) {
            // we're done
            break;
        }

        if (!skip_src_rows(&cinfo, srcRow, sampler.srcDY() - 1)) {
            return return_failure(cinfo, *bm, "skip rows");
        }
    }

    // we formally skip the rest, so we don't get a complaint from libjpeg
    if (!skip_src_rows(&cinfo, srcRow,
                       cinfo.output_height - cinfo.output_scanline)) {
        return return_failure(cinfo, *bm, "skip rows");
    }
    jpeg_finish_decompress_ALPHA(&cinfo);
    SkDebugf("jpeg_decoder finish successfully, L:%d!!!\n",__LINE__);

    return kSuccess;
}

///////////////////////////////////////////////////////////////////////////////

enum SizeType {
    kSizeForMemoryAllocation_SizeType,
    kActualSize_SizeType
};

static SkISize compute_yuv_size(const jpeg_decompress_struct_ALPHA& info, int component,
                                SizeType sizeType) {
    if (sizeType == kSizeForMemoryAllocation_SizeType) {
        return SkISize::Make(info.cur_comp_info[component]->width_in_blocks * DCTSIZE_ALPHA,
                             info.cur_comp_info[component]->height_in_blocks * DCTSIZE_ALPHA);
    }
    return SkISize::Make(info.cur_comp_info[component]->downsampled_width,
                         info.cur_comp_info[component]->downsampled_height);
}

static bool appears_to_be_yuv(const jpeg_decompress_struct_ALPHA& info) {
    return (info.jpeg_color_space == JCS_YCbCr_ALPHA)
        && (DCTSIZE_ALPHA == 8)
        && (info.num_components == 3)
        && (info.comps_in_scan >= info.num_components)
        && (info.scale_denom <= 8)
        && (info.cur_comp_info[0])
        && (info.cur_comp_info[1])
        && (info.cur_comp_info[2])
        && (info.cur_comp_info[1]->h_samp_factor == 1)
        && (info.cur_comp_info[1]->v_samp_factor == 1)
        && (info.cur_comp_info[2]->h_samp_factor == 1)
        && (info.cur_comp_info[2]->v_samp_factor == 1);
}

static void update_components_sizes(const jpeg_decompress_struct_ALPHA& cinfo, SkISize componentSizes[3],
                                    SizeType sizeType) {
    SkASSERT(appears_to_be_yuv(cinfo));
    for (int i = 0; i < 3; ++i) {
        componentSizes[i] = compute_yuv_size(cinfo, i, sizeType);
    }
}

static bool output_raw_data(jpeg_decompress_struct_ALPHA& cinfo, void* planes[3], size_t rowBytes[3]) {
    SkASSERT(appears_to_be_yuv(cinfo));
    // U size and V size have to be the same if we're calling output_raw_data()
    SkISize uvSize = compute_yuv_size(cinfo, 1, kSizeForMemoryAllocation_SizeType);
    SkASSERT(uvSize == compute_yuv_size(cinfo, 2, kSizeForMemoryAllocation_SizeType));

    JSAMPARRAY_ALPHA bufferraw[3];
    JSAMPROW_ALPHA bufferraw2[32];
    bufferraw[0] = &bufferraw2[0]; // Y channel rows (8 or 16)
    bufferraw[1] = &bufferraw2[16]; // U channel rows (8)
    bufferraw[2] = &bufferraw2[24]; // V channel rows (8)
    int yWidth = cinfo.output_width;
    int yHeight = cinfo.output_height;
    int yMaxH = yHeight - 1;
    int v = cinfo.cur_comp_info[0]->v_samp_factor;
    int uvMaxH = uvSize.height() - 1;
    JSAMPROW_ALPHA outputY = static_cast<JSAMPROW_ALPHA>(planes[0]);
    JSAMPROW_ALPHA outputU = static_cast<JSAMPROW_ALPHA>(planes[1]);
    JSAMPROW_ALPHA outputV = static_cast<JSAMPROW_ALPHA>(planes[2]);
    size_t rowBytesY = rowBytes[0];
    size_t rowBytesU = rowBytes[1];
    size_t rowBytesV = rowBytes[2];

    int yScanlinesToRead = DCTSIZE_ALPHA * v;
    SkAutoTMalloc<uint8_t> lastRowStorage(rowBytesY * 4);
    JSAMPROW_ALPHA yLastRow = (JSAMPROW_ALPHA)lastRowStorage.get();
    JSAMPROW_ALPHA uLastRow = yLastRow + rowBytesY;
    JSAMPROW_ALPHA vLastRow = uLastRow + rowBytesY;
    JSAMPROW_ALPHA dummyRow = vLastRow + rowBytesY;

    while (cinfo.output_scanline < cinfo.output_height) {
        // Request 8 or 16 scanlines: returns 0 or more scanlines.
        bool hasYLastRow(false), hasUVLastRow(false);
        // Assign 8 or 16 rows of memory to read the Y channel.
        for (int i = 0; i < yScanlinesToRead; ++i) {
            int scanline = (cinfo.output_scanline + i);
            if (scanline < yMaxH) {
                bufferraw2[i] = &outputY[scanline * rowBytesY];
            } else if (scanline == yMaxH) {
                bufferraw2[i] = yLastRow;
                hasYLastRow = true;
            } else {
                bufferraw2[i] = dummyRow;
            }
        }
        int scaledScanline = cinfo.output_scanline / v;
        // Assign 8 rows of memory to read the U and V channels.
        for (int i = 0; i < 8; ++i) {
            int scanline = (scaledScanline + i);
            if (scanline < uvMaxH) {
                bufferraw2[16 + i] = &outputU[scanline * rowBytesU];
                bufferraw2[24 + i] = &outputV[scanline * rowBytesV];
            } else if (scanline == uvMaxH) {
                bufferraw2[16 + i] = uLastRow;
                bufferraw2[24 + i] = vLastRow;
                hasUVLastRow = true;
            } else {
                bufferraw2[16 + i] = dummyRow;
                bufferraw2[24 + i] = dummyRow;
            }
        }
        JDIMENSION scanlinesRead = jpeg_read_raw_data_ALPHA(&cinfo, bufferraw, yScanlinesToRead);

        if (scanlinesRead == 0) {
            return false;
        }

        if (hasYLastRow) {
            memcpy(&outputY[yMaxH * rowBytesY], yLastRow, yWidth);
        }
        if (hasUVLastRow) {
            memcpy(&outputU[uvMaxH * rowBytesU], uLastRow, uvSize.width());
            memcpy(&outputV[uvMaxH * rowBytesV], vLastRow, uvSize.width());
        }
    }

    cinfo.output_scanline = SkMin32(cinfo.output_scanline, cinfo.output_height);

    return true;
}

bool SkJPEGImageDecoder::onDecodeYUV8Planes(SkStream* stream, SkISize componentSizes[3],
                                            void* planes[3], size_t rowBytes[3],
                                            SkYUVColorSpace* colorSpace) {
#ifdef TIME_DECODE
    SkAutoTime atm("JPEG YUV8 Decode");
#endif
    if (this->getSampleSize() != 1) {
        return false; // Resizing not supported
    }

    JPEGAutoClean autoClean;

    jpeg_decompress_struct_ALPHA cinfo;
    skjpeg_source_mgr_MTK srcManager(stream, this);

    skjpeg_error_mgr_MTK errorManager;
    set_error_mgr(&cinfo, &errorManager);

    // All objects need to be instantiated before this setjmp call so that
    // they will be cleaned up properly if an error occurs.
    skjpeg_error_mgr_MTK::AutoPushJmpBuf jmp(&errorManager);
    if (setjmp(jmp)) {
        return return_false(cinfo, "setjmp YUV8");
    }

    initialize_info(&cinfo, &srcManager);
    autoClean.set(&cinfo);

    int status = jpeg_read_header_ALPHA(&cinfo, true);
    if (status != JPEG_HEADER_OK) {
        return return_false(cinfo, "read_header YUV8");
    }

    if (!appears_to_be_yuv(cinfo)) {
        // It's not an error to not be encoded in YUV, so no need to use return_false()
        return false;
    }

    cinfo.out_color_space = JCS_YCbCr_ALPHA;
    cinfo.raw_data_out = TRUE;

    if (!planes || !planes[0] || !rowBytes || !rowBytes[0]) { // Compute size only
        update_components_sizes(cinfo, componentSizes, kSizeForMemoryAllocation_SizeType);
        return true;
    }

    set_dct_method(*this, &cinfo);

    SkASSERT(1 == cinfo.scale_num);
    cinfo.scale_denom = 1;

    turn_off_visual_optimizations(&cinfo);

#ifdef ANDROID_RGB
    cinfo.dither_mode = JDITHER_NONE_ALPHA;
#endif

    /*  image_width and image_height are the original dimensions, available
        after jpeg_read_header(). To see the scaled dimensions, we have to call
        jpeg_start_decompress(), and then read output_width and output_height.
    */
    if (!jpeg_start_decompress_ALPHA(&cinfo)) {
        return return_false(cinfo, "start_decompress YUV8");
    }

    // Seems like jpeg_start_decompress is updating our opinion of whether cinfo represents YUV.
    // Again, not really an error.
    if (!appears_to_be_yuv(cinfo)) {
        return false;
    }

    if (!output_raw_data(cinfo, planes, rowBytes)) {
        return return_false(cinfo, "output_raw_data");
    }

    update_components_sizes(cinfo, componentSizes, kActualSize_SizeType);
    jpeg_finish_decompress_ALPHA(&cinfo);

    if (nullptr != colorSpace) {
        *colorSpace = kJPEG_SkYUVColorSpace;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////

#ifdef SK_BUILD_FOR_ANDROID
static bool getEncodedColor(jpeg_decompress_struct_ALPHA* cinfo, SkEncodedInfo::Color* outColor) {
    switch (cinfo->jpeg_color_space) {
        case JCS_GRAYSCALE_ALPHA:
            *outColor = SkEncodedInfo::kGray_Color;
            return true;
        case JCS_YCbCr_ALPHA:
            *outColor = SkEncodedInfo::kYUV_Color;
            return true;
        case JCS_RGB_ALPHA:
            *outColor = SkEncodedInfo::kRGB_Color;
            return true;
        case JCS_YCCK_ALPHA:
            *outColor = SkEncodedInfo::kYCCK_Color;
            return true;
        case JCS_CMYK_ALPHA:
            *outColor = SkEncodedInfo::kInvertedCMYK_Color;
            return true;
        default:
            return false;
    }
}

bool SkJPEGImageDecoder::onBuildTileIndex(SkStreamRewindable* stream, int *width, int *height) {

#ifdef MTK_SKIA_MULTI_THREAD_JPEG_REGION
    fFirstTileDone = false;
    fUseHWResizer = false;

    size_t length = stream->getLength();
    if (length <= 0 ) {
        return false;
    }

    SkAutoTMalloc<uint8_t> allocMemory(length);

//    stream->rewind();
    stream->read(allocMemory.get(), length) ;
#ifdef MTK_JPEG_HW_DECODER
    /* parsing and get ISOSpeedRatings */
    fISOSpeedRatings = getISOSpeedRatings(allocMemory.get(), length);
    SkDebugf("onBuildTileIndex parsed ISOSpeedRatings %d L:%d!! \n" ,fISOSpeedRatings, __LINE__);
#endif

    SkMemoryStream* mtkPxyStream = new SkMemoryStream(allocMemory ,  length,  true);

    SkAutoTDelete<SkJPEGImageIndex> imageIndex = new SkJPEGImageIndex(stream, this);

    if(mtkPxyStream){
        imageIndex->mtkStream = mtkPxyStream ;
    }
    
#else
    SkAutoTDelete<SkJPEGImageIndex> imageIndex = new SkJPEGImageIndex(stream, this);
#endif

    g_mt_start = 0;
    g_mt_end = 0;
    g_mt_end_duration_2 = 0;
    g_mt_hw_sum1 = 0;
    g_mt_hw_sum2 = 0;    

    jpeg_decompress_struct_ALPHA* cinfo = imageIndex->cinfo();

    skjpeg_error_mgr_MTK sk_err;
    set_error_mgr(imageIndex->cinfo(), &sk_err);

    // All objects need to be instantiated before this setjmp call so that
    // they will be cleaned up properly if an error occurs.
    skjpeg_error_mgr_MTK::AutoPushJmpBuf jmp(&sk_err);
    if (setjmp(jmp)) {
        return false;
    }

    // create the cinfo used to create/build the huffmanIndex
    if (!imageIndex->initializeInfoAndReadHeader()) {
        return false;
    }

    if (!imageIndex->buildHuffmanIndex()) {
        return false;
    }

    // destroy the cinfo used to create/build the huffman index
   imageIndex->destroyInfo();

   imageIndex->resetStream(); //add for reset stream offset

    // Init decoder to image decode mode
    if (!imageIndex->initializeInfoAndReadHeader()) {
        return false;
    }

    // FIXME: This sets cinfo->out_color_space, which we may change later
    // based on the config in onDecodeSubset. This should be fine, since
    // jpeg_init_read_tile_scanline will check out_color_space again after
    // that change (when it calls jinit_color_deconverter).
    (void) this->getBitmapColorType(cinfo);

    // Create image info object and the codec
    SkEncodedInfo::Color color;
    if (!getEncodedColor(cinfo, &color)) {
        return false;
    }
    fEncodedInfo = SkEncodedInfo::Make(color, SkEncodedInfo::kOpaque_Alpha, 8);
    //SkDebugf("onBuildTileIndex SkEncodedInfo::Make color %d\n", color);

    turn_off_visual_optimizations(cinfo);

    // instead of jpeg_start_decompress() we start a tiled decompress
    if (!imageIndex->startTileDecompress()) {
        return false;
    }

    SkASSERT(1 == cinfo->scale_num);
    fImageWidth = cinfo->output_width;
    fImageHeight = cinfo->output_height;

    if (width) {
        *width = fImageWidth;
    }
    if (height) {
        *height = fImageHeight;
    }

    delete fImageIndex;
    fImageIndex = imageIndex.detach();

    if ((cinfo->comps_in_scan < cinfo->num_components )&& !cinfo->progressive_mode){
      SkDebugf("buildTileIndex fail, region_decoder unsupported format : prog %d, comp %d, scan_comp %d!!\n"
      , cinfo->progressive_mode, cinfo->num_components, cinfo->comps_in_scan );
      return false;
    }

    return true;
}

#if defined(MTK_JPEG_HW_DECODER) || defined(MTK_JPEG_HW_REGION_RESIZER)

extern void* allocateIONBuffer(int ionClientHnd, ion_user_handle_t *ionAllocHnd, int *bufferFD, size_t size);
extern void freeIONBuffer(int ionClientHnd, ion_user_handle_t ionAllocHnd, void* bufferAddr, int bufferFD, size_t size);

bool MDPCrop(void* src, int ionClientHnd, int srcFD, int width, int height, SkBitmap* bm, SkColorType colorType, int tdsp, void* pPPParam, unsigned int ISOSpeed)
{
    if((nullptr == bm))
    {
        ALOGW("MDP_Crop : null bitmap");
        return false;
    }
    if(nullptr == bm->getPixels())
    {
        ALOGW("MDP_Crop : null pixels");
        return false;
    }
    if((colorType == kRGBA_8888_SkColorType) || 
       (colorType == kRGB_565_SkColorType))
    {
        DpBlitStream bltStream;  //mHalBltParam_t bltParam;
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
                dp_out_fmt = eRGBX8888; //eABGR8888;    //bltParam.dstFormat = MHAL_FORMAT_ABGR_8888;
                src_pByte = 4;
                break;
            case kRGB_565_SkColorType:                
                dp_out_fmt = eRGB565;    //bltParam.dstFormat = MHAL_FORMAT_RGB_565;
                src_pByte = 2;
                break;
            default :
                ALOGW("MDP_Crop : unvalid bitmap config %d!!\n", colorType);
                return false;
        }
        dp_in_fmt = dp_out_fmt ;

        src_size[0] = width * height * src_pByte ;
        SkDebugf("MDP_Crop: wh (%d %d)->(%d %d), fmt %d, size %d->%d, regionPQ %d!!\n", width, height, bm->width(), bm->height()
        , colorType, src_size[0], bm->rowBytes() * bm->height(), tdsp);
        
        {
            DpPqParam pqParam;
            uint32_t* pParam = &pqParam.u.image.info[0];
            
            pqParam.enable = (tdsp == 0)? false:true;
            pqParam.scenario = MEDIA_PICTURE;
            pqParam.u.image.iso = ISOSpeed;
            if (pPPParam)
            {
                SkDebugf("MDP_Crop: enable imgDc pParam %p", pPPParam);
                pqParam.u.image.withHist = true;
                memcpy((void*)pParam, pPPParam, 20 * sizeof(uint32_t));
            }
            else
                pqParam.u.image.withHist = false;
            
            bltStream.setPQParameter(pqParam);
        }

        //SkDebugf("MDP_Crop: CONFIG_SRC_BUF, go L:%d!!\n", __LINE__);
        if (srcFD >= 0)
            bltStream.setSrcBuffer(srcFD, src_size, plane_num);
        else
            bltStream.setSrcBuffer((void**)src_addr, src_size, plane_num);
        DpRect src_roi;
        src_roi.x = 0;
        src_roi.y = 0;
        src_roi.w = width;
        src_roi.h = height;
        //SkDebugf("MDP_Crop: CONFIG_SRC_SIZE, go L:%d!!\n", __LINE__);
        bltStream.setSrcConfig(width, height, width * src_pByte, 0, dp_in_fmt, DP_PROFILE_JPEG);

        // set dst buffer
        ///SkDebugf("MDP_Crop: CONFIG_DST_BUF, go L:%d!!\n", __LINE__);
        ion_user_handle_t ionAllocHnd = 0;
        int dstFD = 0;
        void* dstBuffer = nullptr;

		uint skBitmapSize_MTK = bm->height() * bm->rowBytes();
        // if srcFD >= 0, need to use ion for buffer allocation
        if (srcFD >= 0)
        {
            uint size[1];

			size[0] = skBitmapSize_MTK;
            dstBuffer = allocateIONBuffer(ionClientHnd, &ionAllocHnd, &dstFD, size[0]);
            SkDebugf("MDPCrop allocateIONBuffer src:(%d), dst:(%d, %d, %d, %d, %p)", 
                    srcFD, ionClientHnd, ionAllocHnd, dstFD, size[0], dstBuffer);
            bltStream.setDstBuffer(dstFD, size, 1 );  // bm->width() * bm->height() * dst_bitperpixel / 8);
        }
        else
            bltStream.setDstBuffer((void *)bm->getPixels(), bm->rowBytes() * bm->height() );  // bm->width() * bm->height() * dst_bitperpixel / 8);

        DpRect dst_roi;
        dst_roi.x = 0;
        dst_roi.y = 0;
        dst_roi.w = bm->width();
        dst_roi.h = bm->height();

        //SkDebugf("MDP_Crop: CONFIG_DST_SIZE, go L:%d!!\n", __LINE__);
        bltStream.setDstConfig(bm->width(), bm->height(), bm->rowBytes(), 0, dp_out_fmt, DP_PROFILE_JPEG);

        //SkDebugf("MDP_Crop: GO_BITBLIT, go L:%d!!\n", __LINE__);
        rst = bltStream.invalidate() ;

        // if dstBuffer is not nullptr, need to copy pixels to bitmap and free ION buffer
        if (dstBuffer != nullptr)
        {
            memcpy(bm->getPixels(), dstBuffer, skBitmapSize_MTK);
            freeIONBuffer(ionClientHnd, ionAllocHnd, dstBuffer, dstFD, skBitmapSize_MTK);
        }
        
        if ( rst < 0) {
            ALOGE("region Resizer: DpBlitStream invalidate failed, L:%d!!\n", __LINE__);
            return false;
        }else{
            return true ;
        }
       
    }
    return false;
}

bool MDPResizer(void* src, int ionClientHnd, int srcFD, int width, int height, SkScaledBitmapSampler::SrcConfig sc, SkBitmap* bm, SkColorType colorType, int tdsp, void* pPPParam, unsigned int ISOSpeed)
{
    if((nullptr == bm))
    {
        ALOGW("MDPResizer : null bitmap");
        return false;
    }
    if(nullptr == bm->getPixels())
    {
        ALOGW("MDPResizer : null pixels");
        return false;
    }
    if((colorType == kRGBA_8888_SkColorType) || 
       (colorType == kRGB_565_SkColorType))
    {
        DpBlitStream bltStream;  //mHalBltParam_t bltParam;
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
                dp_out_fmt = eRGBX8888; //eABGR8888;    //bltParam.dstFormat = MHAL_FORMAT_ABGR_8888;
                break;
            case kRGB_565_SkColorType:                
                dp_out_fmt = eRGB565;    //bltParam.dstFormat = MHAL_FORMAT_RGB_565;
                break;
            default :
                ALOGW("MDPResizer : invalid bitmap config %d", colorType);
                return false;
        }
        switch(sc)
        {
            case SkScaledBitmapSampler::kRGB:
                dp_in_fmt = eRGB888;         //bltParam.srcFormat = MHAL_FORMAT_BGR_888;
                src_pByte = 3;
                break;
            case SkScaledBitmapSampler::kRGBX:
                dp_in_fmt = eRGBX8888;//eABGR8888;         //bltParam.srcFormat = MHAL_FORMAT_ABGR_8888;
                src_pByte = 4;
                break;
            case SkScaledBitmapSampler::kRGB_565:
                dp_in_fmt = eRGB565;         //bltParam.srcFormat = MHAL_FORMAT_RGB_565;
                src_pByte = 2;
                break;
            case SkScaledBitmapSampler::kGray:
                dp_in_fmt = eGREY;           //bltParam.srcFormat = MHAL_FORMAT_Y800;
                src_pByte = 1;
                break;
            default :
                ALOGW("MDPResizer : invalid src format %d", sc);
                return false;
            break;
        }

        src_size[0] = width * height * src_pByte ;
        SkDebugf("MDPResizer: wh (%d %d)->(%d %d), fmt %d->%d, size %d->%d, regionPQ %d!!\n", width, height, bm->width(), bm->height()
        ,sc, colorType, src_size[0], bm->rowBytes() * bm->height(), tdsp);
        
        {
            DpPqParam pqParam;
            uint32_t* pParam = &pqParam.u.image.info[0];
            
            pqParam.enable = (tdsp == 0)? false:true;
            pqParam.scenario = MEDIA_PICTURE;
            pqParam.u.image.iso = ISOSpeed;
            if (pPPParam)
            {
                SkDebugf("MDPResizer: enable imgDc pParam %p", pPPParam);
                pqParam.u.image.withHist = true;
                memcpy((void*)pParam, pPPParam, 20 * sizeof(uint32_t));
            }
            else
                pqParam.u.image.withHist = false;
            
            bltStream.setPQParameter(pqParam);
        }
        
        //SkDebugf("MDPResizer: CONFIG_SRC_BUF, go L:%d!!\n", __LINE__);
        if (srcFD >= 0)
            bltStream.setSrcBuffer(srcFD, src_size, plane_num);
        else
            bltStream.setSrcBuffer((void**)src_addr, src_size, plane_num);

        DpRect src_roi;
        src_roi.x = 0;
        src_roi.y = 0;
        src_roi.w = width;
        src_roi.h = height;
        //SkDebugf("MDPResizer: CONFIG_SRC_SIZE, go L:%d!!\n", __LINE__);
        //bltStream.setSrcConfig(width, height, dp_in_fmt, eInterlace_None, &src_roi);
        bltStream.setSrcConfig(width, height, width * src_pByte, 0, dp_in_fmt, DP_PROFILE_JPEG);

        // set dst buffer
        ///SkDebugf("MDPResizer: CONFIG_DST_BUF, go L:%d!!\n", __LINE__);
        ion_user_handle_t ionAllocHnd = 0;
        int dstFD = 0;
        void* dstBuffer = nullptr;

		uint skBitmapSize_MTK = bm->height() * bm->rowBytes();
        // if srcFD >= 0, need to use ion for buffer allocation
        if (srcFD >= 0)
        {
            uint size[1];
    
            size[0] = skBitmapSize_MTK;
            dstBuffer = allocateIONBuffer(ionClientHnd, &ionAllocHnd, &dstFD, size[0]);
            SkDebugf("MDPResizer allocateIONBuffer src:(%d), dst:(%d, %d, %d, %d, %p)", 
                    srcFD, ionClientHnd, ionAllocHnd, dstFD, size[0], dstBuffer);
            bltStream.setDstBuffer(dstFD, size, 1 );  // bm->width() * bm->height() * dst_bitperpixel / 8);
        }
        else
            bltStream.setDstBuffer((void *)bm->getPixels(), bm->rowBytes() * bm->height() );  // bm->width() * bm->height() * dst_bitperpixel / 8);

        DpRect dst_roi;
        dst_roi.x = 0;
        dst_roi.y = 0;
        dst_roi.w = bm->width();
        dst_roi.h = bm->height();

        //SkDebugf("MDPResizer: CONFIG_DST_SIZE, go L:%d!!\n", __LINE__);
        //bltStream.setDstConfig(bm->width(), bm->height(), dp_out_fmt, eInterlace_None, &dst_roi);
        bltStream.setDstConfig(bm->width(), bm->height(), bm->rowBytes(), 0, dp_out_fmt, DP_PROFILE_JPEG);

        //SkDebugf("MDPResizer: GO_BITBLIT, go L:%d!!\n", __LINE__);
        rst = bltStream.invalidate() ;

        // if dstBuffer is not nullptr, need to copy pixels to bitmap and free ION buffer
        if (dstBuffer != nullptr)
        {
            memcpy(bm->getPixels(), dstBuffer, skBitmapSize_MTK);
            freeIONBuffer(ionClientHnd, ionAllocHnd, dstBuffer, dstFD, skBitmapSize_MTK);
        }
    
        if ( rst < 0) {
            ALOGE("region Resizer: DpBlitStream invalidate failed, L:%d!!\n", __LINE__);
            return false;
        }else{
            return true ;
        }
    }
    return false;
}
#endif


bool SkJPEGImageDecoder::onDecodeSubset(SkBitmap* bm, const SkIRect& region) {
    if (nullptr == fImageIndex) {
        return false;
    }

#ifdef MTK_SKIA_MULTI_THREAD_JPEG_REGION
    int sampleSize = this->getSampleSize();
    #ifdef MTK_IMAGE_DC_SUPPORT
        void* pParam = this->getDynamicCon();
        return this->onDecodeSubset(bm, region, sampleSize, pParam);
    #else
        return this->onDecodeSubset(bm, region, sampleSize, nullptr);
    #endif
}
bool SkJPEGImageDecoder::onDecodeSubset(SkBitmap* bm, const SkIRect& region, int isampleSize, void* pParam) {
#endif //MTK_SKIA_MULTI_THREAD_JPEG_REGION

    double mt_start = now_ms(); // start time
    if(g_mt_start == 0){
        g_mt_start = mt_start;
        base_thread_id = gettid();      
    }else if(mt_start < g_mt_start){
        g_mt_start = mt_start;
        base_thread_id = gettid();
    }
    //g_mt_end = 0;
    //SkDebugf("JPEG: debug_onDecodeSubset ++ , dur = %f,  id = %d,L:%d!!\n", mt_start - g_mt_start, gettid() , __LINE__);

#if defined(MTK_JPEG_HW_DECODER) || defined(MTK_JPEG_HW_REGION_RESIZER)
    unsigned int enTdshp = (this->getPostProcFlag()? 1 : 0);

    if (fFirstTileDone == false)
    {
        unsigned long u4PQOpt;
        char value[PROPERTY_VALUE_MAX];
    
        property_get("persist.PQ", value, "1");
        u4PQOpt = atol(value);
        if(0 != u4PQOpt)
        {
            if (!enTdshp)// && !pParam)
            {
                fFirstTileDone = true;
                fUseHWResizer = false;
            }
        }
    }
#endif

#ifdef MTK_SKIA_MULTI_THREAD_JPEG_REGION

    JpgLibAutoClean auto_clean_cinfo ;
    JpgStreamAutoClean auto_clean_stream;
    jpeg_decompress_struct_ALPHA *cinfo = nullptr;
    SkStream *stream ;
    skjpeg_source_mgr_MTK *sk_stream = nullptr;

    if(fImageIndex->mtkStream){
        //SkDebugf("MTR_JPEG: mtkStream  length = %d ,  L:%d!!\n", fImageIndex->mtkStream->getLength(),__LINE__);

        //stream = new SkMemoryStream(fImageIndex->mtkStream->getMemoryBase() ,fImageIndex->mtkStream->getLength() , true);
        stream = fImageIndex->mtkStream->duplicate().release();

        sk_stream = new skjpeg_source_mgr_MTK(stream, this);

        cinfo = (jpeg_decompress_struct_ALPHA *)malloc(sizeof(struct jpeg_decompress_struct_ALPHA));
        memset(cinfo, 0, sizeof(struct jpeg_decompress_struct_ALPHA));
        auto_clean_cinfo.set(cinfo);

        cinfo->src = (jpeg_source_mgr_ALPHA *)malloc(sizeof(struct jpeg_source_mgr_ALPHA));
        memset(cinfo->src, 0, sizeof(struct jpeg_source_mgr_ALPHA));
        auto_clean_cinfo.set(cinfo->src);

        skjpeg_error_mgr_MTK sk_err;
        set_error_mgr(cinfo, &sk_err);

        // All objects need to be instantiated before this setjmp call so that
        // they will be cleaned up properly if an error occurs.
        skjpeg_error_mgr_MTK::AutoPushJmpBuf jmp(&sk_err);
        if (setjmp(jmp)) {
           SkDebugf("MTR_JPEG: setjmp L:%d!!\n ", __LINE__ );
           delete sk_stream;
           return false;
        }
          
        // Init decoder to image decode mode
        //if (!localImageIndex->initializeInfoAndReadHeader()) 
        {
            initialize_info(cinfo, sk_stream);
            const bool success = (JPEG_HEADER_OK == jpeg_read_header_ALPHA(cinfo, true));
            if(!success){
               SkDebugf("MTR_JPEG: initializeInfoAndReadHeader error L:%d!!\n ", __LINE__ );
               return false;
            }
        }

        // FIXME: This sets cinfo->out_color_space, which we may change later
        // based on the config in onDecodeSubset. This should be fine, since
        // jpeg_init_read_tile_scanline will check out_color_space again after
        // that change (when it calls jinit_color_deconverter).
        (void) this->getBitmapColorType(cinfo);

        turn_off_visual_optimizations(cinfo);

        // instead of jpeg_start_decompress() we start a tiled decompress
        //if (!localImageIndex->startTileDecompress()) {
       
        if (!jpeg_start_tile_decompress_ALPHA(cinfo)) {
           SkDebugf("MTR_JPEG: startTileDecompress error L:%d!!\n ", __LINE__ );
           return false;
        }

        auto_clean_stream.set(stream);

    }
       
    //SkDebugf("MTR_JPEG: testmt_init -- ,mt_end_2 = %f , L:%d!!\n",now_ms() - g_mt_start  , __LINE__);
       

#else
    jpeg_decompress_struct_ALPHA* cinfo = fImageIndex->cinfo();
#endif

    SkIRect rect = SkIRect::MakeWH(fImageWidth, fImageHeight);
    if (!rect.intersect(region)) {
        // If the requested region is entirely outside the image return false
        return false;
    }


    SkAutoTMalloc<uint8_t> srcStorage;
    skjpeg_error_mgr_MTK errorManager;
    set_error_mgr(cinfo, &errorManager);

    skjpeg_error_mgr_MTK::AutoPushJmpBuf jmp(&errorManager);
    if (setjmp(jmp)) {
        return false;
    }

#ifdef MTK_SKIA_MULTI_THREAD_JPEG_REGION
    if(isampleSize == 0x0){
        isampleSize = this->getSampleSize();
        SkDebugf("JPEG: debug isampleSize = %d , L:%d!!\n",isampleSize ,__LINE__);
    }
    int requestedSampleSize = (isampleSize == 3)? 2: isampleSize; //this->getSampleSize();
#else
    int requestedSampleSize = this->getSampleSize();
#endif

#ifdef MTK_JPEG_HW_REGION_RESIZER
    if(!fFirstTileDone || fUseHWResizer)
    {
        SkIRect rectHWResz;
        // create new region which is padding 40 pixels for each boundary
        rectHWResz.set((region.left() >= 40)? region.left() - 40: region.left(),
                       (region.top() >= 40)? region.top() - 40: region.top(),
                       (region.right() + 40 <= fImageWidth)? region.right() + 40: fImageWidth,
                       (region.bottom() + 40 <= fImageHeight)? region.bottom() + 40: fImageHeight); 
        // set rect to enlarged size to fit HW resizer constraint
        rect.set(0,0,fImageWidth, fImageHeight);
        if (!rect.intersect(rectHWResz))
        {
            return false;
        }
    }
#endif
    cinfo->scale_denom = requestedSampleSize;

    set_dct_method(*this, cinfo);

    const SkColorType colorType = this->getBitmapColorType(cinfo);
    adjust_out_color_space_and_dither(cinfo, colorType, *this);

    int startX = rect.fLeft;
    int startY = rect.fTop;
    int width = rect.width();
    int height = rect.height();

    jpeg_init_read_tile_scanline_ALPHA(cinfo, fImageIndex->huffmanIndex(),
                                 &startX, &startY, &width, &height);
    int skiaSampleSize = recompute_sampleSize(requestedSampleSize, *cinfo);
    int actualSampleSize = skiaSampleSize * (DCTSIZE_ALPHA / cinfo->min_DCT_scaled_size);

    SkScaledBitmapSampler sampler(width, height, skiaSampleSize);

    SkBitmap bitmap;
    // Assume an A8 bitmap is not opaque to avoid the check of each
    // individual pixel. It is very unlikely to be opaque, since
    // an opaque A8 bitmap would not be very interesting.
    // Otherwise, a jpeg image is opaque.
    bitmap.setInfo(SkImageInfo::Make(sampler.scaledWidth(), sampler.scaledHeight(), colorType,
                                     kAlpha_8_SkColorType == colorType ?
                                         kPremul_SkAlphaType : kOpaque_SkAlphaType, fImageInfo.refColorSpace()));

    // Check ahead of time if the swap(dest, src) is possible or not.
    // If yes, then we will stick to AllocPixelRef since it's cheaper with the
    // swap happening. If no, then we will use alloc to allocate pixels to
    // prevent garbage collection.
    int w = rect.width() / actualSampleSize;
    int h = rect.height() / actualSampleSize;
    bool swapOnly = (rect == region) && bm->isNull() &&
                    (w == bitmap.width()) && (h == bitmap.height()) &&
                    ((startX - rect.x()) / actualSampleSize == 0) &&
                    ((startY - rect.y()) / actualSampleSize == 0);
    if (swapOnly) {
        if (!this->allocPixelRef(&bitmap, nullptr)) {
            return return_false(*cinfo, bitmap, "allocPixelRef");
        }
    } else {
        if (!bitmap.tryAllocPixels()) {
            return return_false(*cinfo, bitmap, "allocPixels");
        }
    }

    //SkAutoLockPixels alp(bitmap);

#ifdef ANDROID_RGB
    /* short-circuit the SkScaledBitmapSampler when possible, as this gives
       a significant performance boost.
    */
    if (skiaSampleSize == 1 &&
        ((kN32_SkColorType == colorType && cinfo->out_color_space == JCS_EXT_RGBA_ALPHA) ||
         (kRGB_565_SkColorType == colorType && cinfo->out_color_space == JCS_RGB565_ALPHA)))
    {
        JSAMPLE_ALPHA* rowptr = (JSAMPLE_ALPHA*)bitmap.getPixels();
        INT32 const bpr = bitmap.rowBytes();
        int rowTotalCount = 0;

    #ifdef MTK_JPEG_HW_REGION_RESIZER 
        uint8_t* hwBuffer ;
        #ifdef MTK_SKIA_USE_ION
        SkIonMalloc srcAllocator(fIonClientHnd);
        if(!fFirstTileDone || fUseHWResizer){
            hwBuffer = (uint8_t*)srcAllocator.reset(bitmap.height() * bitmap.rowBytes() );     
            rowptr = hwBuffer ;
        }
        #else
        if(!fFirstTileDone || fUseHWResizer){
            hwBuffer = (uint8_t*)srcStorage.reset(bitmap.height() * bitmap.rowBytes() );     
            rowptr = hwBuffer ;
        }
        #endif
    #endif                        

        while (rowTotalCount < height) {
            int rowCount = jpeg_read_tile_scanline_ALPHA(cinfo,
                                                   fImageIndex->huffmanIndex(),
                                                   &rowptr);
            // if rowCount == 0, then we didn't get a scanline, so abort.
            // onDecodeSubset() relies on onBuildTileIndex(), which
            // needs a complete image to succeed.
            if (0 == rowCount) {
                return return_false(*cinfo, bitmap, "read_scanlines");
            }
            if (this->shouldCancelDecode()) {
                return return_false(*cinfo, bitmap, "shouldCancelDecode");
            }
            
            if (JCS_CMYK_ALPHA == cinfo->out_color_space) {
                convert_CMYK_to_RGB(rowptr, bitmap.width());
            }
            rowTotalCount += rowCount;
            rowptr += bpr;
        }

#ifdef defined(MTK_JPEG_HW_REGION_RESIZER)

        double hw_resize = now_ms() ;
        //SkDebugf("MTR_JPEG: testmt_hw_resize ++ , time = %f , L:%d!!\n",hw_resize - g_mt_start  , __LINE__);
        
        if(!fFirstTileDone || fUseHWResizer)
        {
            //SkDebugf("use hw crop : width height (%d %d)-> (%d %d), L:%d!!\n", width, height, bitmap->width(), bitmap->height(), __LINE__);
            SkDebugf("SkRegionJPEG::region crop (%d %d)->(%d %d), region (%d %d %d %d), swap %d, L:%d!!\n", bitmap.width(), bitmap.height(), bm->width(), bm->height()
            ,region.x(), region.y(),region.width(), region.height(),swapOnly,__LINE__);	        
            
            int try_times = 5;
            bool result = false;
            do
            {
                #ifdef MTK_SKIA_USE_ION
                //result = MDPCrop(hwBuffer, fIonClientHnd, srcAllocator.getFD(), width, height, &bitmap, colorType, enTdshp, pParam, fISOSpeedRatings);
                result = MDPCrop(hwBuffer, fIonClientHnd, srcAllocator.getFD(), width, height, &bitmap, colorType, enTdshp, nullptr, fISOSpeedRatings);
                #else
                //result = MDPCrop(hwBuffer, 0, -1, width, height, &bitmap, colorType, enTdshp, pParam, fISOSpeedRatings);
                result = MDPCrop(hwBuffer, 0, -1, width, height, &bitmap, colorType, enTdshp, nullptr, fISOSpeedRatings);
                #endif
            
                if(!result && ++try_times < 5)
                {
                    SkDebugf("Hardware resize fail, sleep 100 us and then try again, L:%d!!\n", __LINE__);
                    usleep(100*1000);
                }
            }while(!result && try_times < 5);
            
            
            if(!result)
            {
                {
                  #ifdef MTK_SKIA_MULTI_THREAD_JPEG_REGION 
                    SkAutoMutexAcquire ac(gAutoTileResizeMutex);
                  #endif
                  fFirstTileDone = true;
                }
                ALOGW("Hardware resize fail, use sw crop, L:%d!!\n", __LINE__);
                rowptr = (JSAMPLE_ALPHA*)bitmap.getPixels();
                memcpy(rowptr, hwBuffer,bitmap.height() * bitmap.rowBytes());
            }
            else
            {
                {
                  #ifdef MTK_SKIA_MULTI_THREAD_JPEG_REGION
                    SkAutoMutexAcquire ac(gAutoTileResizeMutex);
                  #endif
                  fUseHWResizer = true;
                  fFirstTileDone = true;
                }
                //SkDebugf("Hardware resize successfully, L:%d!!\n", __LINE__);
            }
        } 
        
        if(base_thread_id == gettid()){
            g_mt_hw_sum1 = g_mt_hw_sum1 +(now_ms() - hw_resize);
            //SkDebugf("MTR_JPEG: testmt_hw_resize -- , time = %f ,sum1 = %f , L:%d!!\n",now_ms() - hw_resize, g_mt_hw_sum1  , __LINE__);        
        }else{
            g_mt_hw_sum2 = g_mt_hw_sum2 +(now_ms() - hw_resize);
            //SkDebugf("MTR_JPEG: testmt_hw_resize -- , time = %f ,sum2 = %f , L:%d!!\n",now_ms() - hw_resize, g_mt_hw_sum2  , __LINE__);        

        }
        
#endif //MTK_JPEG_HW_REGION_RESIZER 

        if (swapOnly) {
            bm->swap(bitmap);
        } else {
            cropBitmap(bm, &bitmap, actualSampleSize, region.x(), region.y(),
                       region.width(), region.height(), startX, startY);
            if (bm->pixelRef() == nullptr) {
              ALOGW("SkiaJPEG::cropBitmap allocPixelRef FAIL L:%d !!!!!!\n", __LINE__);
              return return_false(*cinfo, bitmap, "cropBitmap Allocate Pixel Fail!! ");
            }
        }

#ifdef MTK_SKIA_MULTI_THREAD_JPEG_REGION
        SkAutoTDelete<skjpeg_source_mgr_MTK> adjpg(sk_stream);
        //SkDebugf("MTR_JPEG: testmt_dinit ++ , time = %f , L:%d!!\n",now_ms() - g_mt_start  , __LINE__);
        jpeg_finish_decompress_ALPHA(cinfo);

        jpeg_destroy_decompress_ALPHA(cinfo);
        //SkDebugf("MTR_JPEG: testmt_dinit -- , time = %f , L:%d!!\n",now_ms() - g_mt_start  , __LINE__);
#endif        

        #ifdef JPEG_DRAW_RECT
        {
          //SkAutoLockPixels alp(*bm);
          unsigned char *drawptr = (unsigned char *)bm->getPixels();
          unsigned int width = bm->width();
          unsigned int height = bm->height();
          unsigned int line = 0;
          unsigned int draw_x=0, draw_y=0 ;
          
          for(draw_y = 0; draw_y < height ; draw_y++){
            
            for(draw_x = 0; draw_x < width ; draw_x++){
              //if(bm->bytesPerPixel() == 4)
              if( ( draw_y == 0 || draw_y == 1) || 
                  ( draw_y == height-1 || draw_y == height-2) || 
                  ( (draw_x == 0 || draw_x == 1) || (draw_x == width -1 || draw_x == width -2) ) )
                *drawptr = 0xFF ;
              drawptr += bm->bytesPerPixel();
            }
            
          }
        }
        #endif
        if(base_thread_id == gettid()){
            g_mt_end = now_ms() - g_mt_start;
        }else{
            g_mt_end_duration_2 = now_ms() - g_mt_start;
        }
        //SkDebugf("JPEG: debug_onDecodeSubset -- , dur = %f, dur = %f, all dur = %f , L:%d!!\n", g_mt_end , g_mt_end_duration_2, g_mt_end+g_mt_end_duration_2 ,  __LINE__);
        return true;
    }
#endif

    // check for supported formats
    SkScaledBitmapSampler::SrcConfig sc;
    int srcBytesPerPixel;

    if (!get_src_config(*cinfo, &sc, &srcBytesPerPixel)) {
        return return_false(*cinfo, *bm, "jpeg colorspace");
    }

    if (!sampler.begin(&bitmap, sc, *this)) {
        return return_false(*cinfo, bitmap, "sampler.begin");
    }

#if 0
    SkAutoTMalloc<uint8_t>  srcStorage(width * srcBytesPerPixel);
    uint8_t* srcRow = (uint8_t*)srcStorage.get();
#endif
    uint8_t* srcRow = (uint8_t*)srcStorage.reset(width * srcBytesPerPixel);

#ifdef MTK_JPEG_HW_REGION_RESIZER
if(!fFirstTileDone || fUseHWResizer)
{
    #ifdef MTK_SKIA_USE_ION
    SkIonMalloc srcAllocator(fIonClientHnd);
    uint8_t* hwBuffer = (uint8_t*)srcAllocator.reset(width * height * srcBytesPerPixel + 4);
    #else
    SkAutoTMalloc<uint8_t> hwStorage;
    uint8_t* hwBuffer = (uint8_t*)srcStorage.reset(width * height * srcBytesPerPixel + 4);
    #endif

    hwBuffer[width * height * srcBytesPerPixel + 4 - 1] = 0xF0;
    hwBuffer[width * height * srcBytesPerPixel + 4 - 2] = 0xF0;
    hwBuffer[width * height * srcBytesPerPixel + 4 - 3] = 0xF0;
    hwBuffer[width * height * srcBytesPerPixel + 4 - 4] = 0xF0;
    int row_total_count = 0;
    int bpr = width * srcBytesPerPixel;
    JSAMPLE_ALPHA* rowptr = (JSAMPLE_ALPHA*)hwBuffer;
    
    //SkDebugf("MTR_JPEG: jpeg_read_tile_scanline 2+ , time = %f , L:%d!!\n",now_ms() - g_mt_start  , __LINE__);
    
    while (row_total_count < height) {
        int row_count = jpeg_read_tile_scanline_ALPHA(cinfo, fImageIndex->huffmanIndex(), &rowptr);
        // if row_count == 0, then we didn't get a scanline, so abort.
        // if we supported partial images, we might return true in this case
        if (0 == row_count) {
            return return_false(*cinfo, bitmap, "read_scanlines");
        }
        if (this->shouldCancelDecode()) {
            return return_false(*cinfo, bitmap, "shouldCancelDecode");
        }
        
        if (JCS_CMYK_ALPHA == cinfo->out_color_space) {
            convert_CMYK_to_RGB(rowptr, width);
        }
            row_total_count += row_count;
            rowptr += bpr;
    }

    double hw_resize = now_ms() ;
    //SkDebugf("MTR_JPEG: testmt_hw_resize 2++ , time = %f , L:%d!!\n",hw_resize - g_mt_start  , __LINE__);

    int try_times = 5;
    bool result = false;
    do
    {
        #ifdef MTK_SKIA_USE_ION
        //result = MDPResizer(hwBuffer, fIonClientHnd, srcAllocator.getFD(), width, height, sc, &bitmap, colorType, enTdshp, pParam, fISOSpeedRatings);
        result = MDPResizer(hwBuffer, fIonClientHnd, srcAllocator.getFD(), width, height, sc, &bitmap, colorType, enTdshp, nullptr, fISOSpeedRatings);
        #else
        //result = MDPResizer(hwBuffer, 0, -1, width, height, sc, &bitmap, colorType, enTdshp, pParam, fISOSpeedRatings);
        result = MDPResizer(hwBuffer, 0, -1, width, height, sc, &bitmap, colorType, enTdshp, nullptr, fISOSpeedRatings);
        #endif

        if(!result && ++try_times < 5)
        {
            SkDebugf("Hardware resize fail, sleep 100 us and then try again ");
            usleep(100*1000);
        }
    }while(!result && try_times < 5);

    if(!result)
    {

        {
          #ifdef MTK_SKIA_MULTI_THREAD_JPEG_REGION
            SkAutoMutexAcquire ac(gAutoTileResizeMutex);
          #endif
          fFirstTileDone = true;
        }      
        ALOGW("Hardware resize fail, use sw sampler");
        row_total_count = 0;
        rowptr = (JSAMPLE_ALPHA*)hwBuffer;
        rowptr += (bpr * sampler.srcY0());
        row_total_count += sampler.srcY0();
        for (int y = 0;; y++) {

            if (this->shouldCancelDecode()) {
                return return_false(*cinfo, bitmap, "shouldCancelDecode");
            }

            sampler.next(rowptr);
            if (bitmap.height() - 1 == y) {
                // we're done
                SkDebugf("total row count %d\n", row_total_count);
                break;
            }
            rowptr += bpr;
            row_total_count ++;

            rowptr += (bpr * (sampler.srcDY() - 1));
            row_total_count += (sampler.srcDY() - 1);
        }
    }
    else
    {
        {
          #ifdef MTK_SKIA_MULTI_THREAD_JPEG_REGION
            SkAutoMutexAcquire ac(gAutoTileResizeMutex);
          #endif
          fUseHWResizer = true;
          fFirstTileDone = true;
        }
        //SkDebugf("Hardware resize successfully ");
    }

    if(base_thread_id == gettid()){
        g_mt_hw_sum1 = g_mt_hw_sum1 +(now_ms() - hw_resize);
        //SkDebugf("MTR_JPEG: testmt_hw_resize 2-- , time = %f ,sum1 = %f , L:%d!!\n",now_ms() - hw_resize, g_mt_hw_sum1  , __LINE__);        
    }else{
        g_mt_hw_sum2 = g_mt_hw_sum2 +(now_ms() - hw_resize);
        //SkDebugf("MTR_JPEG: testmt_hw_resize 2-- , time = %f ,sum2 = %f , L:%d!!\n",now_ms() - hw_resize, g_mt_hw_sum2  , __LINE__);        
    }
    
} else {
#endif
    //  Possibly skip initial rows [sampler.srcY0]
    if (!skip_src_rows_tile(cinfo, fImageIndex->huffmanIndex(), srcRow, sampler.srcY0())) {
        return return_false(*cinfo, bitmap, "skip rows");
    }

    // now loop through scanlines until y == bitmap->height() - 1
    for (int y = 0;; y++) {
        JSAMPLE_ALPHA* rowptr = (JSAMPLE_ALPHA*)srcRow;
        int row_count = jpeg_read_tile_scanline_ALPHA(cinfo, fImageIndex->huffmanIndex(), &rowptr);
        // if row_count == 0, then we didn't get a scanline, so abort.
        // onDecodeSubset() relies on onBuildTileIndex(), which
        // needs a complete image to succeed.
        if (0 == row_count) {
            return return_false(*cinfo, bitmap, "read_scanlines");
        }
        if (this->shouldCancelDecode()) {
            return return_false(*cinfo, bitmap, "shouldCancelDecode");
        }

        if (JCS_CMYK_ALPHA == cinfo->out_color_space) {
            convert_CMYK_to_RGB(srcRow, width);
        }

        sampler.next(srcRow);
        if (bitmap.height() - 1 == y) {
            // we're done
            break;
        }

        if (!skip_src_rows_tile(cinfo, fImageIndex->huffmanIndex(), srcRow,
                                sampler.srcDY() - 1)) {
            return return_false(*cinfo, bitmap, "skip rows");
        }
    }

#ifdef MTK_JPEG_HW_REGION_RESIZER
}
#endif


    if (swapOnly) {
        bm->swap(bitmap);
    } else {
        cropBitmap(bm, &bitmap, actualSampleSize, region.x(), region.y(),
                   region.width(), region.height(), startX, startY);
        if (bm->pixelRef() == nullptr) {
          ALOGW("SkiaJPEG::cropBitmap allocPixelRef FAIL L:%d !!!!!!\n", __LINE__);			
          return return_false(*cinfo, bitmap, "cropBitmap Allocate Pixel Fail!! ");
        }       
    }

#ifdef MTK_SKIA_MULTI_THREAD_JPEG_REGION
    SkAutoTDelete<skjpeg_source_mgr_MTK> adjpg(sk_stream);
    jpeg_finish_decompress_ALPHA(cinfo);
    
    jpeg_destroy_decompress_ALPHA(cinfo);
#endif

    #ifdef JPEG_DRAW_RECT
    {
      //SkAutoLockPixels alp(*bm);
      unsigned char *drawptr = (unsigned char *)bm->getPixels();
      unsigned int width = bm->width();
      unsigned int height = bm->height();
      unsigned int line = 0;
      unsigned int draw_x=0, draw_y=0 ;
      
      for(draw_y = 0; draw_y < height ; draw_y++){
        
        for(draw_x = 0; draw_x < width ; draw_x++){
          //if(bm->bytesPerPixel() == 4)
          if( ( draw_y == 0 || draw_y == 1) || 
              ( draw_y == height-1 || draw_y == height-2) || 
              ( (draw_x == 0 || draw_x == 1) || (draw_x == width -1 || draw_x == width -2) ) )
            *drawptr = 0xFF ;
          drawptr += bm->bytesPerPixel();
        }
        
      }
    }
    #endif
    
    
    if(base_thread_id == gettid()){
        g_mt_end = now_ms() - g_mt_start;
    }else{
        g_mt_end_duration_2 = now_ms() - g_mt_start;
    }
    //SkDebugf("JPEG: debug_onDecodeSubset 2 -- , dur = %f, dur = %f, all dur = %f , L:%d!!\n", g_mt_end , g_mt_end_duration_2, g_mt_end+g_mt_end_duration_2 ,  __LINE__);
    
    return true;
}
#endif

///////////////////////////////////////////////////////////////////////////////
DEFINE_DECODER_CREATOR(JPEGImageDecoder);
///////////////////////////////////////////////////////////////////////////////

static bool is_jpeg(SkStreamRewindable* stream) {
    static const unsigned char gHeader[] = { 0xFF, 0xD8, 0xFF };
    static const size_t HEADER_SIZE = sizeof(gHeader);

    char buffer[HEADER_SIZE];
    size_t len = stream->read(buffer, HEADER_SIZE);

    if (len != HEADER_SIZE) {
        return false;   // can't read enough
    }
    if (memcmp(buffer, gHeader, HEADER_SIZE)) {
        return false;
    }
    return true;
}


static SkImageDecoder* sk_libjpeg_dfactory(SkStreamRewindable* stream) {
    if (is_jpeg(stream)) {
        return new SkJPEGImageDecoder;
    }
    return nullptr;
}

static SkImageDecoder::Format get_format_jpeg(SkStreamRewindable* stream) {
    if (is_jpeg(stream)) {
        return SkImageDecoder::kJPEG_Format;
    }
    return SkImageDecoder::kUnknown_Format;
}

static SkImageDecoder_DecodeReg gDReg(sk_libjpeg_dfactory);
static SkImageDecoder_FormatReg gFormatReg(get_format_jpeg);

