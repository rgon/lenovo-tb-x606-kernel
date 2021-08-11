/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkBitmapRegionCodec_DEFINED
#define SkBitmapRegionCodec_DEFINED

#include "SkBitmap.h"
#include "SkBitmapRegionDecoder.h"
#include "SkAndroidCodec.h"

/*
 * This class implements SkBitmapRegionDecoder using an SkAndroidCodec.
 */
class SkBitmapRegionCodec : public SkBitmapRegionDecoder {
public:

    /*
     * Takes ownership of pointer to codec
     */
    SkBitmapRegionCodec(SkAndroidCodec* codec);

    bool decodeRegion(SkBitmap* bitmap, SkBRDAllocator* allocator,
                      const SkIRect& desiredSubset, int sampleSize,
                      SkColorType colorType, bool requireUnpremul,
                      sk_sp<SkColorSpace> prefColorSpace) override;

    SkEncodedImageFormat getEncodedFormat() override { return fCodec->getEncodedFormat(); }

    SkColorType computeOutputColorType(SkColorType requestedColorType) override {
        return fCodec->computeOutputColorType(requestedColorType);
    }

    sk_sp<SkColorSpace> computeOutputColorSpace(SkColorType outputColorType,
            sk_sp<SkColorSpace> prefColorSpace = nullptr) override {
        return fCodec->computeOutputColorSpace(outputColorType, prefColorSpace);
    }

#ifdef MTK_IMAGE_ENABLE_PQ_FOR_JPEG
    void setPostProcFlag(int flag) override { fCodec->setPostProcFlag(flag);}
#endif

private:

    std::unique_ptr<SkAndroidCodec> fCodec;

    typedef SkBitmapRegionDecoder INHERITED;

#ifdef MTK_IMAGE_ENABLE_PQ_FOR_JPEG
	//int                           fPostProc;		//unused
#endif

};
#endif  // SkBitmapRegionCodec_DEFINED
