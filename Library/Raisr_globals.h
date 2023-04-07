/**
 * Intel Library for Video Super Resolution
 *
 * Copyright (c) 2022 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once  

#include <ipp.h>
#include "ThreadPool.h"
#include "RaisrDefaults.h"

#ifdef ENABLE_RAISR_OPENCL
#include "Raisr_OpenCL.h"
#endif

/************************************************************
 *   const variables
 ************************************************************/
#define MAX8BIT_FULL  0xff
#define MAX10BIT_FULL 0x3ff
#define MAX16BIT_FULL 0xffff
#define MIN_FULL 0

#define MAX8BIT_VIDEO 235
#define MIN8BIT_VIDEO 16
#define MAX10BIT_VIDEO 940
#define MIN10BIT_VIDEO 64

const float PI = 3.141592653;
// the sigma value of the Gaussian filter
const float sigma = 2.0f;
// CT blending parameters
const int CTwindowSize = 3;
const int CTnumberofPixel = CTwindowSize * CTwindowSize - 1;
const int CTmargin = CTwindowSize >> 1;
const int gHashingExpand = CTmargin + 1; // Segment is again expanded by CTmargin so that all the rows in the segment can be processed by CTCountOfBitsChanged(). "+1" is to make sure the resize zone is even.
static unsigned int gRatio;
static ASMType gAsmType;
static MachineVendorType gMachineVendorType;
static unsigned int gBitDepth;

// Process multiple columns in each pass of the loop
// This is a tunable, may depend on cache size of a platform
// This also results in additional memory requirements
const int unrollSizeImageBased = 4;
// unrollSizePatchBased should be at least 2
const int unrollSizePatchBased = 8;

/************************************************************
 *   preprocessor directives
 ************************************************************/
//#define USE_ATAN2_APPROX
#define ENABLE_PREFETCH
// Split memcpy of a column into the for loop in RNLProcess
// This should result in lower working set for memory
#define SPLIT_MEMCPY

#define BYTES_16BITS 2
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// Default method is bilinear upscaling. To enable other, enable the option below
// If both are options are commented out, bilinear method is used for upscaling
//#define USE_BICUBIC
//#define USE_LANCZOS
#ifdef USE_BICUBIC
#define IPP_RESIZE_TYPE ippCubic
#define IPPRInit(depth) ippiResizeCubicInit_##depth##u
#define IPPResize(depth) ippiResizeCubic_##depth##u_C1R
#else
#ifdef USE_LANCZOS
#define IPP_RESIZE_TYPE ippLanczos
#define IPPRInit(depth) ippiResizeLanczosInit_##depth##u
#define IPPResize(depth) ippiResizeLanczos_##depth##u_C1R
#else
#define IPP_RESIZE_TYPE ippLinear
#define IPPRInit(depth) ippiResizeLinearInit_##depth##u
#define IPPResize(depth) ippiResizeLinear_##depth##u_C1R
#endif
#endif

/************************************************************
 *   data structures
 ************************************************************/
// row range in HR [startRow, endRow)
/** processing zones:                                     cols
 Cheap upscale zone             |    .....................................................
                                |    ..................gResizeExpand......................
                                |    .....................................................
 Raisr hashing zone       |     |    ******************gHashingExpand*********************
 Blending zone      |     |     |    #####################################################
                    |     |     |    #####################################################
                    |     |     |    #####################################################
                          |     |    *****************************************************
                                |    .....................................................
                                |    .....................................................
                                |    .....................................................
*/
struct segZone
{
    // zone to perform cheap up scale
    int scaleStartRow;
    int scaleEndRow;
    // zone to perform RAISR refine
    int raisrStartRow;
    int raisrEndRow;
    // zone to perform CT-Blending ==> composed final output
    int blendingStartRow;
    int blendingEndRow;
    // cheap upscaled segment, hold 8/10/16bit data
    Ipp8u *inYUpscaled;
    // cheap upscaled segment in 32f
    float *inYUpscaled32f;
    // raiser hashing output in 32f, first copy inYUpscaled32f, then refine pixel value
    float *raisr32f;
};

struct ippContext
{
    IppiResizeSpec_32f **specY;
    IppiResizeSpec_32f *specUV;

    segZone *segZones[2]; // need 2d segZones for the resize is in the 2nd pass
    Ipp8u **pbufferY;     // working buffer is always 8u
    Ipp8u *pbufferUV;     // working buffer is always 8u
};

enum class CHANNEL
{
    NONE = 0,
    Y,
    UV
};

/************************************************************
 *   global variables
 ************************************************************/
// IPP context
ippContext gIppCtx;

#ifdef ENABLE_RAISR_OPENCL
RaisrOpenCLContext gOpenCLContext = { 0 };
#endif

// Quantization values
static unsigned int gQuantizationAngle;
static unsigned int gQuantizationStrength;
static unsigned int gQuantizationCoherence;
static float gQAngle;

// patch size related globals
static unsigned int gPatchSize;
static unsigned int gPatchMargin;
static unsigned int gLoopMargin;
static unsigned int gResizeExpand; // Segment is expanded by gLoopMargin so that the whole patch area is covered. Expand by 2 to avoid border that ipp resize modified.
static unsigned int g64AlinedgPatchAreaSize;

// vectors to hold trained data
std::vector<float> gQStr;
std::vector<float> gQCoh;
std::vector<std::vector<float *>> gFilterBuckets;
std::vector<float> gQStr2;
std::vector<float> gQCoh2;
std::vector<std::vector<float *>> gFilterBuckets2;

// contiguous memory to hold all filters
float *gFilterBuffer;
float *gFilterBuffer2;
VideoDataType *gIntermediateY; // Buffer to hold intermediate result for two pass
volatile int threadStatus[120];

// threading related used in patch-based approach
static int gThreadCount = 0;
ThreadPool *gPool = nullptr;

// pointer to gaussian filter allocated dynamiclly
static float *gPGaussian = nullptr;

// gPasses = 1 means one pass processing, gPasses = 2 means two pass processing.
static int gPasses = 1;
static int gTwoPassMode = 1;

// color range
static unsigned char gMin8bit;
static unsigned char gMax8bit;
static unsigned short gMin16bit;
static unsigned short gMax16bit;

// pre-caculated gaussian filter.
// gaussian kernel (arrary with size gPatchSize * gPatchSize).
// normalization factor for 8/10/16 bits. 2.0 is from gradient compute.
#define NF_8  (1.0f / (255.0f   * 255.0f   * 2.0f * 2.0f))
#define NF_10 (1.0f / (1023.0f  * 1023.0f  * 2.0f * 2.0f))
#define NF_16 (1.0f / (65535.0f * 65535.0f * 2.0f * 2.0f))

// createGaussianKernel()
static float gGaussian2DOriginal[11][16] = {
    {0.0, 7.76554e-05,  0.000239195, 0.0005738, 0.001072,  0.00155975,0.00176743,0.00155975,0.001072,  0.0005738, 0.000239195,7.76554e-05, 0.0, 0.0, 0.0, 0.0 },
    {0.0, 0.000239195,  0.000736774, 0.00176743,0.00330199,0.00480437,0.00544406,0.00480437,0.00330199,0.00176743,0.000736774,0.000239195, 0.0, 0.0, 0.0, 0.0 },
    {0.0, 0.0005738,    0.00176743,  0.00423984,0.00792107,0.0115251, 0.0130596, 0.0115251, 0.00792107,0.00423984,0.00176743, 0.0005738,   0.0, 0.0, 0.0, 0.0 },
    {0.0, 0.001072,     0.00330199,  0.00792107,0.0147985, 0.0215317, 0.0243986, 0.0215317, 0.0147985, 0.00792107,0.00330199, 0.001072,    0.0, 0.0, 0.0, 0.0 },
    {0.0, 0.00155975,   0.00480437,  0.0115251, 0.0215317, 0.0313284, 0.0354998, 0.0313284, 0.0215317, 0.0115251, 0.00480437, 0.00155975,  0.0, 0.0, 0.0, 0.0 },
    {0.0, 0.00176743,   0.00544406,  0.0130596, 0.0243986, 0.0354998, 0.0402265, 0.0354998, 0.0243986, 0.0130596, 0.00544406, 0.00176743,  0.0, 0.0, 0.0, 0.0 },
    {0.0, 0.00155975,   0.00480437,  0.0115251, 0.0215317, 0.0313284, 0.0354998, 0.0313284, 0.0215317, 0.0115251, 0.00480437, 0.00155975,  0.0, 0.0, 0.0, 0.0 },
    {0.0, 0.001072,     0.00330199,  0.00792107,0.0147985, 0.0215317, 0.0243986, 0.0215317, 0.0147985, 0.00792107,0.00330199, 0.001072,    0.0, 0.0, 0.0, 0.0 },
    {0.0, 0.0005738,    0.00176743,  0.00423984,0.00792107,0.0115251, 0.0130596, 0.0115251, 0.00792107,0.00423984,0.00176743, 0.0005738,   0.0, 0.0, 0.0, 0.0 },
    {0.0, 0.000239195,  0.000736774, 0.00176743,0.00330199,0.00480437,0.00544406,0.00480437,0.00330199,0.00176743,0.000736774,0.000239195, 0.0, 0.0, 0.0, 0.0 },
    {0.0, 7.76554e-05,  0.000239195, 0.0005738, 0.001072,  0.00155975,0.00176743,0.00155975,0.001072,  0.0005738, 0.000239195,7.76554e-05, 0.0, 0.0, 0.0, 0.0 }};

// createGaussianKernel() * (1.0/255.0*2.0) * (1.0/255.0*2.0)
static float gGaussian2D8bit[11][16] = {
    {0.0, NF_8*7.76554e-05,  NF_8*0.000239195, NF_8*0.0005738, NF_8*0.001072,  NF_8*0.00155975,NF_8*0.00176743,NF_8*0.00155975,NF_8*0.001072,  NF_8*0.0005738, NF_8*0.000239195,NF_8*7.76554e-05, 0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_8*0.000239195,  NF_8*0.000736774, NF_8*0.00176743,NF_8*0.00330199,NF_8*0.00480437,NF_8*0.00544406,NF_8*0.00480437,NF_8*0.00330199,NF_8*0.00176743,NF_8*0.000736774,NF_8*0.000239195, 0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_8*0.0005738,    NF_8*0.00176743,  NF_8*0.00423984,NF_8*0.00792107,NF_8*0.0115251, NF_8*0.0130596, NF_8*0.0115251, NF_8*0.00792107,NF_8*0.00423984,NF_8*0.00176743, NF_8*0.0005738,   0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_8*0.001072,     NF_8*0.00330199,  NF_8*0.00792107,NF_8*0.0147985, NF_8*0.0215317, NF_8*0.0243986, NF_8*0.0215317, NF_8*0.0147985, NF_8*0.00792107,NF_8*0.00330199, NF_8*0.001072,    0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_8*0.00155975,   NF_8*0.00480437,  NF_8*0.0115251, NF_8*0.0215317, NF_8*0.0313284, NF_8*0.0354998, NF_8*0.0313284, NF_8*0.0215317, NF_8*0.0115251, NF_8*0.00480437, NF_8*0.00155975,  0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_8*0.00176743,   NF_8*0.00544406,  NF_8*0.0130596, NF_8*0.0243986, NF_8*0.0354998, NF_8*0.0402265, NF_8*0.0354998, NF_8*0.0243986, NF_8*0.0130596, NF_8*0.00544406, NF_8*0.00176743,  0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_8*0.00155975,   NF_8*0.00480437,  NF_8*0.0115251, NF_8*0.0215317, NF_8*0.0313284, NF_8*0.0354998, NF_8*0.0313284, NF_8*0.0215317, NF_8*0.0115251, NF_8*0.00480437, NF_8*0.00155975,  0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_8*0.001072,     NF_8*0.00330199,  NF_8*0.00792107,NF_8*0.0147985, NF_8*0.0215317, NF_8*0.0243986, NF_8*0.0215317, NF_8*0.0147985, NF_8*0.00792107,NF_8*0.00330199, NF_8*0.001072,    0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_8*0.0005738,    NF_8*0.00176743,  NF_8*0.00423984,NF_8*0.00792107,NF_8*0.0115251, NF_8*0.0130596, NF_8*0.0115251, NF_8*0.00792107,NF_8*0.00423984,NF_8*0.00176743, NF_8*0.0005738,   0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_8*0.000239195,  NF_8*0.000736774, NF_8*0.00176743,NF_8*0.00330199,NF_8*0.00480437,NF_8*0.00544406,NF_8*0.00480437,NF_8*0.00330199,NF_8*0.00176743,NF_8*0.000736774,NF_8*0.000239195, 0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_8*7.76554e-05,  NF_8*0.000239195, NF_8*0.0005738, NF_8*0.001072,  NF_8*0.00155975,NF_8*0.00176743,NF_8*0.00155975,NF_8*0.001072,  NF_8*0.0005738, NF_8*0.000239195,NF_8*7.76554e-05, 0.0, 0.0, 0.0, 0.0 }};

static float gGaussian2D10bit[11][16] = {
    {0.0, NF_10*7.76554e-05,  NF_10*0.000239195, NF_10*0.0005738, NF_10*0.001072,  NF_10*0.00155975,NF_10*0.00176743,NF_10*0.00155975,NF_10*0.001072,  NF_10*0.0005738, NF_10*0.000239195,NF_10*7.76554e-05, 0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_10*0.000239195,  NF_10*0.000736774, NF_10*0.00176743,NF_10*0.00330199,NF_10*0.00480437,NF_10*0.00544406,NF_10*0.00480437,NF_10*0.00330199,NF_10*0.00176743,NF_10*0.000736774,NF_10*0.000239195, 0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_10*0.0005738,    NF_10*0.00176743,  NF_10*0.00423984,NF_10*0.00792107,NF_10*0.0115251, NF_10*0.0130596, NF_10*0.0115251, NF_10*0.00792107,NF_10*0.00423984,NF_10*0.00176743, NF_10*0.0005738,   0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_10*0.001072,     NF_10*0.00330199,  NF_10*0.00792107,NF_10*0.0147985, NF_10*0.0215317, NF_10*0.0243986, NF_10*0.0215317, NF_10*0.0147985, NF_10*0.00792107,NF_10*0.00330199, NF_10*0.001072,    0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_10*0.00155975,   NF_10*0.00480437,  NF_10*0.0115251, NF_10*0.0215317, NF_10*0.0313284, NF_10*0.0354998, NF_10*0.0313284, NF_10*0.0215317, NF_10*0.0115251, NF_10*0.00480437, NF_10*0.00155975,  0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_10*0.00176743,   NF_10*0.00544406,  NF_10*0.0130596, NF_10*0.0243986, NF_10*0.0354998, NF_10*0.0402265, NF_10*0.0354998, NF_10*0.0243986, NF_10*0.0130596, NF_10*0.00544406, NF_10*0.00176743,  0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_10*0.00155975,   NF_10*0.00480437,  NF_10*0.0115251, NF_10*0.0215317, NF_10*0.0313284, NF_10*0.0354998, NF_10*0.0313284, NF_10*0.0215317, NF_10*0.0115251, NF_10*0.00480437, NF_10*0.00155975,  0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_10*0.001072,     NF_10*0.00330199,  NF_10*0.00792107,NF_10*0.0147985, NF_10*0.0215317, NF_10*0.0243986, NF_10*0.0215317, NF_10*0.0147985, NF_10*0.00792107,NF_10*0.00330199, NF_10*0.001072,    0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_10*0.0005738,    NF_10*0.00176743,  NF_10*0.00423984,NF_10*0.00792107,NF_10*0.0115251, NF_10*0.0130596, NF_10*0.0115251, NF_10*0.00792107,NF_10*0.00423984,NF_10*0.00176743, NF_10*0.0005738,   0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_10*0.000239195,  NF_10*0.000736774, NF_10*0.00176743,NF_10*0.00330199,NF_10*0.00480437,NF_10*0.00544406,NF_10*0.00480437,NF_10*0.00330199,NF_10*0.00176743,NF_10*0.000736774,NF_10*0.000239195, 0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_10*7.76554e-05,  NF_10*0.000239195, NF_10*0.0005738, NF_10*0.001072,  NF_10*0.00155975,NF_10*0.00176743,NF_10*0.00155975,NF_10*0.001072,  NF_10*0.0005738, NF_10*0.000239195,NF_10*7.76554e-05, 0.0, 0.0, 0.0, 0.0 }};

static float gGaussian2D16bit[11][16] = {
    {0.0, NF_16*7.76554e-05,  NF_16*0.000239195, NF_16*0.0005738, NF_16*0.001072,  NF_16*0.00155975,NF_16*0.00176743,NF_16*0.00155975,NF_16*0.001072,  NF_16*0.0005738, NF_16*0.000239195,NF_16*7.76554e-05, 0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_16*0.000239195,  NF_16*0.000736774, NF_16*0.00176743,NF_16*0.00330199,NF_16*0.00480437,NF_16*0.00544406,NF_16*0.00480437,NF_16*0.00330199,NF_16*0.00176743,NF_16*0.000736774,NF_16*0.000239195, 0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_16*0.0005738,    NF_16*0.00176743,  NF_16*0.00423984,NF_16*0.00792107,NF_16*0.0115251, NF_16*0.0130596, NF_16*0.0115251, NF_16*0.00792107,NF_16*0.00423984,NF_16*0.00176743, NF_16*0.0005738,   0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_16*0.001072,     NF_16*0.00330199,  NF_16*0.00792107,NF_16*0.0147985, NF_16*0.0215317, NF_16*0.0243986, NF_16*0.0215317, NF_16*0.0147985, NF_16*0.00792107,NF_16*0.00330199, NF_16*0.001072,    0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_16*0.00155975,   NF_16*0.00480437,  NF_16*0.0115251, NF_16*0.0215317, NF_16*0.0313284, NF_16*0.0354998, NF_16*0.0313284, NF_16*0.0215317, NF_16*0.0115251, NF_16*0.00480437, NF_16*0.00155975,  0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_16*0.00176743,   NF_16*0.00544406,  NF_16*0.0130596, NF_16*0.0243986, NF_16*0.0354998, NF_16*0.0402265, NF_16*0.0354998, NF_16*0.0243986, NF_16*0.0130596, NF_16*0.00544406, NF_16*0.00176743,  0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_16*0.00155975,   NF_16*0.00480437,  NF_16*0.0115251, NF_16*0.0215317, NF_16*0.0313284, NF_16*0.0354998, NF_16*0.0313284, NF_16*0.0215317, NF_16*0.0115251, NF_16*0.00480437, NF_16*0.00155975,  0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_16*0.001072,     NF_16*0.00330199,  NF_16*0.00792107,NF_16*0.0147985, NF_16*0.0215317, NF_16*0.0243986, NF_16*0.0215317, NF_16*0.0147985, NF_16*0.00792107,NF_16*0.00330199, NF_16*0.001072,    0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_16*0.0005738,    NF_16*0.00176743,  NF_16*0.00423984,NF_16*0.00792107,NF_16*0.0115251, NF_16*0.0130596, NF_16*0.0115251, NF_16*0.00792107,NF_16*0.00423984,NF_16*0.00176743, NF_16*0.0005738,   0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_16*0.000239195,  NF_16*0.000736774, NF_16*0.00176743,NF_16*0.00330199,NF_16*0.00480437,NF_16*0.00544406,NF_16*0.00480437,NF_16*0.00330199,NF_16*0.00176743,NF_16*0.000736774,NF_16*0.000239195, 0.0, 0.0, 0.0, 0.0 },
    {0.0, NF_16*7.76554e-05,  NF_16*0.000239195, NF_16*0.0005738, NF_16*0.001072,  NF_16*0.00155975,NF_16*0.00176743,NF_16*0.00155975,NF_16*0.001072,  NF_16*0.0005738, NF_16*0.000239195,NF_16*7.76554e-05, 0.0, 0.0, 0.0, 0.0 }};

 
