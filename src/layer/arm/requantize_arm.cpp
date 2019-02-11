// SenseNets is pleased to support the open source community by supporting ncnn available.
//
// Copyright (C) 2019 SenseNets Technology Ltd. All rights reserved.
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

#include "requantize_arm.h"

#include <math.h>

#if __ARM_NEON
#include <arm_neon.h>
#endif // __ARM_NEON

namespace ncnn {

DEFINE_LAYER_CREATOR(Requantize_arm)

static inline signed char float2int8(float v)
{
    int int32 = round(v);
    if (int32 > 127) return 127;
    if (int32 < -128) return -128;
    return (signed char)int32;
}

int Requantize_arm::forward(const Mat& bottom_blob, Mat& top_blob, const Option& opt) const
{ 
    int dims = bottom_blob.dims;

    if (dims == 1)
    {
        int w = bottom_blob.w;

        const int* intptr = bottom_blob;
        signed char * ptr = top_blob;

        if (bias_term)
        {
            if (bias_data_size > 1)
            {
                #pragma omp parallel for num_threads(opt.num_threads)
                for (int i=0; i<w; i++)
                {
                    ptr[i] = float2int8(((intptr[i] * scale_in) + bias_data[i]) * scale_out);
                    if (fusion_relu && ptr[i] < 0)
                        ptr[i] = 0;
                }
            }
            else
            {
                float bias = bias_data[0];
                #pragma omp parallel for num_threads(opt.num_threads)
                for (int i=0; i<w; i++)
                {
                    ptr[i] = float2int8(((intptr[i] * scale_in) + bias) * scale_out);
                    if (fusion_relu && ptr[i] < 0)
                        ptr[i] = 0;
                }
            }
        }
        else
        {
            #pragma omp parallel for num_threads(opt.num_threads)
            for (int i=0; i<w; i++)
            {
                ptr[i] = float2int8(intptr[i] * scale_in * scale_out);
                if (fusion_relu && ptr[i] < 0)
                    ptr[i] = 0;
            }
        }
    }

    if (dims == 2)
    {
        int w = bottom_blob.w;
        int h = bottom_blob.h;

        if (bias_term)
        {
            #pragma omp parallel for num_threads(opt.num_threads)
            for (int i=0; i<h; i++)
            {
                const int* intptr = bottom_blob.row<const int>(i);
                signed char* ptr = top_blob.row<signed char>(i);

                float bias = bias_data_size > 1 ? bias_data[i] : bias_data[0];

                for (int j=0; j<w; j++)
                {
                    ptr[j] = float2int8(((intptr[j] * scale_in) + bias) * scale_out);
                    if (fusion_relu && ptr[j] < 0)
                        ptr[j] = 0;
                }
            }
        }
        else
        {
            #pragma omp parallel for num_threads(opt.num_threads)
            for (int i=0; i<h; i++)
            {
                const int* intptr = bottom_blob.row<const int>(i);
                signed char* ptr = top_blob.row<signed char>(i);

                for (int j=0; j<w; j++)
                {
                    ptr[j] = float2int8(intptr[j] * scale_in * scale_out);
                    if (fusion_relu && ptr[j] < 0)
                        ptr[j] = 0;
                }
            }
        }
    }

    if (dims == 3)
    {
        int w = bottom_blob.w;
        int h = bottom_blob.h;
        int channels = bottom_blob.c;
        int size = w * h;      

        if (bias_term)
        {
            #pragma omp parallel for num_threads(opt.num_threads)
            for (int q=0; q<channels; q++)
            {
                const int* intptr = bottom_blob.channel(q);
                signed char* ptr = top_blob.channel(q);

                float bias = bias_data_size > 1 ? bias_data[q] : bias_data[0];

#if 1 //__ARM_NEON
                int nn = size >> 3;
                int remain = size & 7;

                if (nn > 0)
                {
#if __aarch64__
                //TODO
#else
                asm volatile(
                    "pld        [%1, #256]          \n"
                    "vld1.s32   {d0-d3}, [%1:128]!  \n" //q0-q1 data
                    "vdup.f32   q10, %6             \n" //q10 scale_in
                    "vdup.f32   q11, %7             \n" //q11 scale_out
                    "vdup.f32   q12, %8             \n" //q12 bias
                    "0:                             \n"
                    // top_s32 -> top_f32
                    "vcvt.f32.s32 q0, q0            \n" 
                    "vcvt.f32.s32 q1, q1            \n"
                    // top_f32 = top_f32 * scale_int
                    "vmul.f32   q0, q0, q10         \n"
                    "vmul.f32   q1, q1, q10         \n"
                    // top_f32 = top_f32 + bias
                    "vadd.f32   q0, q0, q12         \n"
                    "vadd.f32   q1, q1, q12         \n"
                    // top_f32 = top_f32 * scale_out
                    "vmul.f32   q0, q0, q11         \n"
                    "vmul.f32   q1, q1, q11         \n"
                    // top_f32 -> top_s32
                    "vcvtr.s32.f32 s0, s0           \n"
                    "vcvtr.s32.f32 s1, s1           \n"
                    "vcvtr.s32.f32 s2, s2           \n"
                    "vcvtr.s32.f32 s3, s3           \n"
                    "vcvtr.s32.f32 s4, s4           \n"
                    "vcvtr.s32.f32 s5, s5           \n"
                    "vcvtr.s32.f32 s6, s6           \n"
                    "vcvtr.s32.f32 s7, s7           \n" 
                    // top_s32 -> top_s16
                    "vqmovn.s32 d4, q0              \n"
                    "vqmovn.s32 d5, q1              \n"
                    "pld        [%1, #256]          \n"
                    "vld1.s32   {d0-d3}, [%1:128]!  \n" //q0-q1 data
                    // top_s16 -> top_s8
                    "vqmovn.s16   d4, q2            \n"
                    // save top_s8
                    "vst1.8     {d4}, [%2:64]!      \n"
                    "subs       %0, #1              \n"
                    "bne        0b                  \n"
                    "sub        %1, #32             \n"
                    : "=r"(nn),         // %0
                      "=r"(intptr),     // %1
                      "=r"(ptr)         // %2
                    : "0"(nn),
                      "1"(intptr),
                      "2"(ptr),
                      "r"(scale_in),    // %6
                      "r"(scale_out),   // %7
                      "r"(bias)         // %8
                    : "cc", "memory", "q0", "q1", "q2", "q10", "q11", "q12"
                );
#endif                    
                }
#else
                int remain = size;
#endif // __ARM_NEON

                for (; remain > 0; remain--)
                {
                    *ptr = float2int8(((*intptr * scale_in) + bias) * scale_out);

                    intptr++;
                    ptr ++;                     
                }
            }
        }
        else
        {
            #pragma omp parallel for num_threads(opt.num_threads)
            for (int q=0; q<channels; q++)
            {
                const int* intptr = bottom_blob.channel(q);
                signed char* ptr = top_blob.channel(q);

#if 1 //__ARM_NEON
                int nn = size >> 3;
                int remain = size & 7;

                if (nn > 0)
                {
#if __aarch64__
                //TODO
#else
                asm volatile(
                    "pld        [%1, #256]          \n"
                    "vld1.s32   {d0-d3}, [%1:128]!  \n" //q0-q1 data
                    "vdup.f32   q10, %6             \n" //q10 scale_in
                    "vdup.f32   q11, %7             \n" //q11 scale_out
                    "0:                             \n"
                    // top_s32 -> top_f32
                    "vcvt.f32.s32 q0, q0            \n" 
                    "vcvt.f32.s32 q1, q1            \n"
                    // top_f32 = top_f32 * scale_int
                    "vmul.f32   q0, q0, q10         \n"
                    "vmul.f32   q1, q1, q10         \n"
                    // top_f32 = top_f32 * scale_out
                    "vmul.f32   q0, q0, q11         \n"
                    "vmul.f32   q1, q1, q11         \n"
                    // top_f32 -> top_s32
                    "vcvtr.s32.f32 s0, s0           \n"
                    "vcvtr.s32.f32 s1, s1           \n"
                    "vcvtr.s32.f32 s2, s2           \n"
                    "vcvtr.s32.f32 s3, s3           \n"
                    "vcvtr.s32.f32 s4, s4           \n"
                    "vcvtr.s32.f32 s5, s5           \n"
                    "vcvtr.s32.f32 s6, s6           \n"
                    "vcvtr.s32.f32 s7, s7           \n" 
                    // top_s32 -> top_s16
                    "vqmovn.s32 d4, q0              \n"
                    "vqmovn.s32 d5, q1              \n"
                    "pld        [%1, #256]          \n"
                    "vld1.s32   {d0-d3}, [%1:128]!  \n" //q0-q1 data
                    // top_s16 -> top_s8
                    "vqmovn.s16   d4, q2            \n"
                    // save top_s8
                    "vst1.8     {d4}, [%2:64]!      \n"
                    "subs       %0, #1              \n"
                    "bne        0b                  \n"
                    "sub        %1, #32             \n"
                    : "=r"(nn),         // %0
                      "=r"(intptr),     // %1
                      "=r"(ptr)         // %2
                    : "0"(nn),
                      "1"(intptr),
                      "2"(ptr),
                      "r"(scale_in),    // %6
                      "r"(scale_out)    // %7
                    : "cc", "memory", "q0", "q1", "q2", "q10", "q11"
                );
#endif                    
                }        
#else
                int remain = size;
#endif // __ARM_NEON

                for (; remain > 0; remain--)
                {
                    *ptr = float2int8(*intptr * scale_in * scale_out);

                    intptr++;
                    ptr ++;                     
                }
            }
        }    
    }

    return 0;
}

} // namespace ncnn