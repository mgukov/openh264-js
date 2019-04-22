#include "codec_api.h"
#include "codec_def.h"
#include "codec_app_def.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <inttypes.h>

#include <emscripten.h>
#include <time.h>

extern "C"
void openh264_log(void * ctx, int level, const char * msg)
{
    emscripten_log(EM_LOG_CONSOLE, "openh264 trace %d %s\n", level, msg);
}

extern "C"
void * open_decoder(void)
{
    ISVCDecoder* decoder_ = NULL;
    if ( WelsCreateDecoder (&decoder_) ) {
        emscripten_log(EM_LOG_CONSOLE, "Create Decoder failed\n");
        return NULL;
    }
    if (! decoder_) {
        emscripten_log(EM_LOG_CONSOLE, "Create Decoder failed (no handle)\n");
        return NULL;
    }

    WelsTraceCallback cb = openh264_log;
    // int32_t tracelevel = 2;//0x7fffffff;
    if ( decoder_->SetOption(DECODER_OPTION_TRACE_CALLBACK, (void*)&cb) )
    {
        emscripten_log(EM_LOG_CONSOLE, "SetOption failed\n");
    }
    // if ( decoder_->SetOption(DECODER_OPTION_TRACE_LEVEL, &tracelevel) )
    // {
    //     emscripten_log(EM_LOG_CONSOLE, "SetOption failed\n");
    // }

    SDecodingParam decParam = {0};
    decParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;

    // decParam.eOutputColorFormat  = videoFormatI420;
    // decParam.uiTargetDqLayer = UCHAR_MAX;
    // decParam.eEcActiveIdc = ERROR_CON_SLICE_COPY;

    if ( decoder_->Initialize (&decParam) ) {
        emscripten_log(EM_LOG_CONSOLE, "initialize failed\n");
        return NULL;
    }

    return decoder_;
}


extern "C"
void close_decoder(void * dec)
{
    ISVCDecoder* decoder_ = (ISVCDecoder*)dec;
    if (decoder_ != NULL) {
        decoder_->Uninitialize();
        WelsDestroyDecoder (decoder_);
    }
}

static char buf[256*1024] = { 0, 0, 0, 1 };


extern "C"
void on_frame_decoded(uint8_t frameData[], uint8_t* data[], int width, int height, int stride1, int stride2) {

  int y;
  for (y=0; y < height; y++) {
    memcpy(frameData + y * width, data[0] + y * stride1, width);
    // const ar = this.module.HEAP8.subarray(a + y*stride1, a+y*stride1+width);
    // data.set(ar, y * width);
  }

  int cbofs = width * height;
  for (y=0; y < height/2; y++) {
    memcpy(frameData + cbofs + y * width/2, data[1] + y * stride2, width/2);
    // const ar = this.module.HEAP8.subarray(b + y*stride2, b+y*stride2+width/2);
    // data.set(ar, cbofs + y * width/2);
  }

  int crofs=cbofs + width*height/4;
  for (y=0; y < height/2; y++) {
    memcpy(frameData + crofs + y * width/2, data[2] + y * stride2, width/2);
    // const ar = this.module.HEAP8.subarray(c + y*stride2, c+y*stride2+width/2);
    // data.set(ar, crofs + y * width/2);
  }


}


extern "C"
int decode_nal(int id, void * dec, unsigned char const * nal, size_t nalsz)
{
    ISVCDecoder* decoder_ = (ISVCDecoder*)dec;
    uint8_t* data[3];
    SBufferInfo bufInfo;
    memset (data, 0, sizeof (data));
    memset (&bufInfo, 0, sizeof (SBufferInfo));

    memcpy(buf + 4, nal, nalsz);

    if (nalsz <= 0) {
        int32_t iEndOfStreamFlag = 1;
        decoder_->SetOption (DECODER_OPTION_END_OF_STREAM, &iEndOfStreamFlag);
        nalsz = 0;
        nal = NULL;
    }


    DECODING_STATE rv = decoder_->DecodeFrameNoDelay((unsigned char *)buf, (int) (nalsz + 4), data, &bufInfo);
    if (rv == 0 && bufInfo.iBufferStatus == 1) {
        

            int width = bufInfo.UsrData.sSystemBuffer.iWidth;
            int height = bufInfo.UsrData.sSystemBuffer.iHeight;

          int len = width * height * 3/2;
          uint8_t frameData[len];

          on_frame_decoded(frameData, data, 
            width, 
            height, 
            bufInfo.UsrData.sSystemBuffer.iStride[0], 
            bufInfo.UsrData.sSystemBuffer.iStride[1]);


        EM_ASM_({
            frame_callback($0, $1, $2, $3);
        },
            id,
            frameData,
            width,
            height
        );

        // EM_ASM_({
        //     frame_callback($0, $1, $2, $3, $4, $5, $6, $7);
        // },
        //     id,
        //     data[0],
        //     data[1],
        //     data[2],
        //     bufInfo.UsrData.sSystemBuffer.iWidth,
        //     bufInfo.UsrData.sSystemBuffer.iHeight,
        //     bufInfo.UsrData.sSystemBuffer.iStride[0],
        //     bufInfo.UsrData.sSystemBuffer.iStride[1]
        // );


        return 1;
    } else if (rv != 0) {
        char statusstr[100] = {0};
        if (rv & dsFramePending) strcat(statusstr,",FramePending");
        if (rv & dsRefLost) strcat(statusstr,",RefLost");
        if (rv & dsBitstreamError) strcat(statusstr,",BitstreamError");
        if (rv & dsDepLayerLost) strcat(statusstr,",DepLayerLost");
        if (rv & dsNoParamSets) strcat(statusstr,",NoParamSets");
        if (rv & dsDataErrorConcealed) strcat(statusstr,",DataErrorConcealed");
        if (rv & dsInvalidArgument) strcat(statusstr,",InvalidArgument");
        if (rv & dsInitialOptExpected) strcat(statusstr,",InitialOptExcpected");
        if (rv & dsOutOfMemory) strcat(statusstr,",OutOfMemory");
        if (rv & dsDstBufNeedExpan) strcat(statusstr,",DstBufNeedExpan");
        emscripten_log(EM_LOG_CONSOLE, "Decode failed: %#x - %s\n", rv, statusstr);
        return -1;
    } else {
        return 0;
    }

//     emscripten_log(EM_LOG_CONSOLE, " frame ready:%d\n"
//            " BsTimeStamp:%llu\n"
//            " OutYuvTimeS:%llu\n"
//            "    Width   :%d\n"
//            "    Height  :%d\n"
//            "    Format  :%d\n"
//            "    Stride  :%d %d\n"
//             ,bufInfo.iBufferStatus
//             ,bufInfo.uiInBsTimeStamp
//             ,bufInfo.uiOutYuvTimeStamp
//             ,bufInfo.UsrData.sSystemBuffer.iWidth
//             ,bufInfo.UsrData.sSystemBuffer.iHeight
//             ,bufInfo.UsrData.sSystemBuffer.iFormat
//             ,bufInfo.UsrData.sSystemBuffer.iStride[0]
//             ,bufInfo.UsrData.sSystemBuffer.iStride[1]
//           );
}

#ifdef NATIVE
#include <sys/mman.h>
unsigned char * mmap_open(const char * fname, size_t * sz)
{
    FILE * f = fopen(fname, "rb");
    fseek(f, 0, SEEK_END);
    *sz = ftell(f);
    return (unsigned char*)mmap(NULL, *sz, PROT_READ,MAP_SHARED, fileno(f), 0);
}


int main(int argc, char * argv[])
{
    void * h = open_decoder();


    emscripten_log(EM_LOG_CONSOLE, "ready to decode\n");
    size_t sz;
    const unsigned char * nalbuf = mmap_open(argv[1], &sz);
    size_t ofs = 0;
    ssize_t nalsz;

    do {
        nalsz = getnal(nalbuf, ofs, sz);
        const unsigned char * nal = nalbuf + ofs;

        decode_nal(0, h, nal, nalsz);

        ofs += nalsz;
    } while (nalsz>0);


    close_decoder(h);

}
#endif
