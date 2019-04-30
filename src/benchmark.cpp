// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2017 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h> // portable: uint64_t   MSVC: __int64
#else // _WIN32
#include <sys/time.h>
#endif // _WIN32

#include "benchmark.h"

#if NCNN_BENCHMARK
#include <stdio.h>
#include "layer/convolution.h"
#include "layer/convolutiondepthwise.h"
#endif // NCNN_BENCHMARK

namespace ncnn {

#ifdef _WIN32
double get_current_time()
{
    // Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
    // This magic number is the number of 100 nanosecond intervals since January 1, 1601 (UTC)
    // until 00:00:00 January 1, 1970
    static const uint64_t EPOCH = ((uint64_t) 116444736000000000ULL);

    SYSTEMTIME  system_time;
    FILETIME    file_time;
    uint64_t    time;

    GetSystemTime( &system_time );
    SystemTimeToFileTime( &system_time, &file_time );
    time =  ((uint64_t)file_time.dwLowDateTime )      ;
    time += ((uint64_t)file_time.dwHighDateTime) << 32;

    long tv_sec  = (long) ((time - EPOCH) / 10000000L);
    long tv_usec = (long) (system_time.wMilliseconds * 1000);

    return tv_sec * 1000.0 + tv_usec / 1000.0;
}
#else // _WIN32
double get_current_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}
#endif // _WIN32

#if NCNN_BENCHMARK

float conv3x3s1 = 0;
float conv3x3s2 = 0;
float conv1x1s1 = 0;
float deconv = 0;
float relu = 0;
float eltwise = 0;

void benchmark(const Layer* layer, double start, double end)
{
    fprintf(stderr, "%-24s %-40s %8.2lfms", layer->type.c_str(), layer->name.c_str(), end - start);
    fprintf(stderr, "    |");

    if (layer->type == "Eltwise")
    {
        eltwise += end - start;
        fprintf(stderr, "   %8.2lfms", eltwise);
    }    

    fprintf(stderr, "\n");
}

void benchmark(const Layer* layer, const Mat& bottom_blob, Mat& top_blob, double start, double end)
{
    fprintf(stderr, "%-24s %-40s %8.2lfms", layer->type.c_str(), layer->name.c_str(), end - start);
    fprintf(stderr, "    |    feature_map: %4d x %-4d    inch: %4d    outch: %4d", top_blob.w, top_blob.h, bottom_blob.c, top_blob.c);

    if (layer->type == "DeconvolutionDepthWise")
    {
        deconv += end - start;
        fprintf(stderr, "   %8.2lfms", deconv);
    }
    if (layer->type == "ReLU")
    {
        relu += end - start;
        fprintf(stderr, "   %8.2lfms", relu);
    }    
    if (layer->type == "Convolution")
    {
        double op = (double)top_blob.w*top_blob.h*top_blob.c*bottom_blob.c*((Convolution*)layer)->kernel_w*((Convolution*)layer)->kernel_h*2;
        double Gops = op / ((end - start) * 1000000.0);
        fprintf(stderr, "     kernel: %1d x %1d     stride: %1d x %1d     op: %-5.2lf Gops",
                ((Convolution*)layer)->kernel_w,
                ((Convolution*)layer)->kernel_h,
                ((Convolution*)layer)->stride_w,
                ((Convolution*)layer)->stride_h,
                Gops
        );

        if (((Convolution*)layer)->kernel_w == 3 && ((Convolution*)layer)->stride_w == 1)
        {
            conv3x3s1 += end - start;
            fprintf(stderr, "   %8.2lfms", conv3x3s1);
        }  
        if (((Convolution*)layer)->kernel_w == 3 && ((Convolution*)layer)->stride_w == 2)
        {
            conv3x3s2 += end - start;
            fprintf(stderr, "   %8.2lfms", conv3x3s2);
        }
        if (((Convolution*)layer)->kernel_w == 1 && ((Convolution*)layer)->stride_w == 1)
        {
            conv1x1s1 += end - start;
            fprintf(stderr, "   %8.2lfms", conv1x1s1);
        }                    
    }
    if (layer->type == "ConvolutionDepthWise")
    {
        int group = ((ConvolutionDepthWise*)layer)->group;
        int channel_g = bottom_blob.c / group;
        int output_num = top_blob.c / group;
        double op = (double)top_blob.w*top_blob.h*channel_g*output_num*group*((ConvolutionDepthWise*)layer)->kernel_w*((ConvolutionDepthWise*)layer)->kernel_h*2;
        double Gops = op / ((end - start) * 1000000.0);
        fprintf(stderr, "     kernel: %1d x %1d     stride: %1d x %1d     op: %-5.2lf Gops",
                ((ConvolutionDepthWise*)layer)->kernel_w,
                ((ConvolutionDepthWise*)layer)->kernel_h,
                ((ConvolutionDepthWise*)layer)->stride_w,
                ((ConvolutionDepthWise*)layer)->stride_h,
                Gops
        );      
    }
    fprintf(stderr, "\n");  
}

#endif // NCNN_BENCHMARK

} // namespace ncnn
