// SenseNets is pleased to support the open source community by supporting ncnn available.
//
// Copyright (C) 2018 SenseNets Technology Ltd. All rights reserved.
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

#if __ARM_NEON
#include <arm_neon.h>
#endif // __ARM_NEON

static void convdw3x3s1_int8_neon(const Mat &bottom_blob, Mat &top_blob, const Mat &_kernel, const Option& opt)
{
    int w = bottom_blob.w;

    int outw = top_blob.w;
    int outh = top_blob.h;
    int outch = top_blob.c;

    #pragma omp parallel for num_threads(opt.num_threads)
    for (int p = 0; p < outch; p++)
    {
        Mat out = top_blob.channel(p);

        const signed char* kernel = (const signed char *)_kernel + p*9;
        
        int* outptr0 = out;
        int* outptr0n = outptr0 + outw;
    
        const signed char* img0 = bottom_blob.channel(p);
        
        const signed char* r0 = img0;
        const signed char* r1 = img0 + w;
        const signed char* r2 = img0 + w*2;
        const signed char* r3 = img0 + w*3;

        int i = 0;
        
#if __ARM_NEON
        int8x16_t _k0123456789x = vld1q_s8(kernel);
        int16x8_t _k_s16 = vmovl_s8(vget_low_s8(_k0123456789x));
        int16x8_t _kn_s16 = vmovl_s8(vget_high_s8(_k0123456789x));

        int16x4_t _k0123 = vget_low_s16(_k_s16);
        int16x4_t _k4567 = vget_high_s16(_k_s16);
        int16x4_t _k8xxx = vget_low_s16(_kn_s16);
#endif // __ARM_NEON 

        for (; i+1 < outh; i+=2)
        {
#if __ARM_NEON            
            int nn = outw >> 3;
            int remain = outw & 7;
#else
            int remain = outw;
#endif // __ARM_NEON            

#if __ARM_NEON
#if __aarch64__
            if (nn > 0)
            {
            asm volatile(
                "0:                                   \n"
                "ld1    {v4.8b, v5.8b}, [%3]          \n"
                "ld1    {v6.8b, v7.8b}, [%4]          \n"
                "ld1    {v8.8b, v9.8b}, [%5]          \n"
                "ld1    {v10.8b, v11.8b}, [%6]        \n"
                "add    %3, %3, #8                    \n"
                "add    %4, %4, #8                    \n"
                "add    %5, %5, #8                    \n"
                "add    %6, %6, #8                    \n"

                "ext    v12.8b, v4.8b, v5.8b, #1      \n"
                "ext    v13.8b, v4.8b, v5.8b, #2      \n"
                "ext    v14.8b, v6.8b, v7.8b, #1      \n"
                "ext    v15.8b, v6.8b, v7.8b, #2      \n"
                "ext    v16.8b, v8.8b, v9.8b, #1      \n"
                "ext    v17.8b, v8.8b, v9.8b, #2      \n"
                "ext    v18.8b, v10.8b, v11.8b, #1    \n"
                "ext    v19.8b, v10.8b, v11.8b, #2    \n"
                
                "sshll  v4.8h, v4.8b, #0              \n"// r00
                "sshll  v12.8h, v12.8b, #0            \n"// r01
                "sshll  v13.8h, v13.8b, #0            \n"// r02
                "sshll  v6.8h, v6.8b, #0              \n"// r10
                "sshll  v14.8h, v14.8b, #0            \n"// r11
                "sshll  v15.8h, v15.8b, #0            \n"// r12
                "sshll  v8.8h, v8.8b, #0              \n"// r20
                "sshll  v16.8h, v16.8b, #0            \n"// r21
                "sshll  v17.8h, v17.8b, #0            \n"// r22
                "sshll  v10.8h, v10.8b, #0            \n"// r30
                "sshll  v18.8h, v18.8b, #0            \n"// r31
                "sshll  v19.8h, v19.8b, #0            \n"// r32

                // r0
                "smull  v20.4s, v4.4h, %14.h[0]       \n"// (r00 - r07) * k00
                "smull2  v21.4s, v4.8h, %14.h[0]      \n"
                "smull  v22.4s, v12.4h, %14.h[1]      \n"// (r01 - r08) * k01
                "smull2  v23.4s, v12.8h, %14.h[1]     \n"
                "smull  v24.4s, v13.4h, %14.h[2]      \n"// (r02 - r09) * k02
                "smull2  v25.4s, v13.8h, %14.h[2]     \n"

                // r1
                "smull  v26.4s, v6.4h, %14.h[0]       \n"// (r10 - r17) * k00
                "smull2  v27.4s, v6.8h, %14.h[0]      \n"
                "smull  v28.4s, v14.4h, %14.h[1]      \n"// (r11 - r18) * k01
                "smull2  v29.4s, v14.8h, %14.h[1]     \n"
                "smull  v30.4s, v15.4h, %14.h[2]      \n"// (r12 - r19) * k02
                "smull2  v31.4s, v15.8h, %14.h[2]     \n"

                "smlal  v20.4s, v6.4h, %14.h[3]       \n"// (r10 - r17) * k03
                "smlal2  v21.4s, v6.8h, %14.h[3]      \n"
                "smlal  v22.4s, v14.4h, %15.h[0]      \n"// (r11 - r18) * k04
                "smlal2  v23.4s, v14.8h, %15.h[0]     \n"
                "smlal  v24.4s, v15.4h, %15.h[1]      \n"// (r12 - r19) * k05
                "smlal2  v25.4s, v15.8h, %15.h[1]     \n"

                // r2
                "smlal  v26.4s, v8.4h, %14.h[3]       \n"// (r20 - r27) * k03
                "smlal2  v27.4s, v8.8h, %14.h[3]      \n"
                "smlal  v28.4s, v16.4h, %15.h[0]      \n"// (r21 - r28) * k04
                "smlal2  v29.4s, v16.8h, %15.h[0]     \n"
                "smlal  v30.4s, v17.4h, %15.h[1]      \n"// (r22 - r29) * k05
                "smlal2  v31.4s, v17.8h, %15.h[1]     \n"

                "smlal  v20.4s, v8.4h, %15.h[2]       \n"// (r20 - r27) * k06
                "smlal2  v21.4s, v8.8h, %15.h[2]      \n"
                "smlal  v22.4s, v16.4h, %15.h[3]      \n"// (r21 - r28) * k07
                "smlal2  v23.4s, v16.8h, %15.h[3]     \n"
                "smlal  v24.4s, v17.4h, %16.h[0]      \n"// (r22 - r29) * k08
                "smlal2  v25.4s, v17.8h, %16.h[0]     \n"

                // r3
                "smlal  v26.4s, v10.4h, %15.h[2]      \n"// (r30 - r37) * k06
                "smlal2  v27.4s, v10.8h, %15.h[2]     \n"
                "smlal  v28.4s, v18.4h, %15.h[3]      \n"// (r31 - r38) * k07
                "smlal2  v29.4s, v18.8h, %15.h[3]     \n"
                "smlal  v30.4s, v19.4h, %16.h[0]      \n"// (r32 - r39) * k08
                "smlal2  v31.4s, v19.8h, %16.h[0]     \n"

                // add and save
                "add    v20.4s, v20.4s, v22.4s        \n"
                "add    v21.4s, v21.4s, v23.4s        \n"
                "add    v26.4s, v26.4s, v28.4s        \n"
                "add    v27.4s, v27.4s, v29.4s        \n"
                "add    v20.4s, v20.4s, v24.4s        \n"
                "add    v21.4s, v21.4s, v25.4s        \n"
                "add    v26.4s, v26.4s, v30.4s        \n"
                "add    v27.4s, v27.4s, v31.4s        \n"

                "st1    {v20.4s, v21.4s}, [%1], #32   \n"
                "st1    {v26.4s, v27.4s}, [%2], #32   \n"

                "subs   %w0, %w0, #1                  \n"
                "bne    0b                            \n"

                : "=r"(nn),       // %0
                  "=r"(outptr0),  // %1
                  "=r"(outptr0n), // %2
                  "=r"(r0),       // %3
                  "=r"(r1),       // %4
                  "=r"(r2),       // %5
                  "=r"(r3)        // %6
                : "0"(nn),
                  "1"(outptr0),
                  "2"(outptr0n),
                  "3"(r0),
                  "4"(r1),
                  "5"(r2),
                  "6"(r3),
                  "w"(_k0123),    // %14
                  "w"(_k4567),    // %15
                  "w"(_k8xxx)     // %16
                : "cc", "memory", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"               
            );
            }
#else
            for (; nn >0; nn--)
            {
                // r0
                int8x8_t _r0 = vld1_s8(r0);
                int8x8_t _r0n = vld1_s8(r0+8);
                int8x8_t _r01 = vext_s8(_r0, _r0n, 1);
                int8x8_t _r02 = vext_s8(_r0, _r0n, 2);
                int16x8_t _r0_s16 = vmovl_s8(_r0);   // r00 - r07
                int16x8_t _r01_s16 = vmovl_s8(_r01); // r01 - r08 
                int16x8_t _r02_s16 = vmovl_s8(_r02); // r02 - r09

                int32x4_t _sum0 = vmull_lane_s16(vget_low_s16(_r0_s16), _k0123, 0); // (r00 - r07) * k00
                int32x4_t _sum0n = vmull_lane_s16(vget_high_s16(_r0_s16), _k0123, 0);

                int32x4_t _sum1 = vmull_lane_s16(vget_low_s16(_r01_s16), _k0123, 1); // (r01 - r08) * k01
                int32x4_t _sum1n = vmull_lane_s16(vget_high_s16(_r01_s16), _k0123, 1);

                int32x4_t _sum2 = vmull_lane_s16(vget_low_s16(_r02_s16), _k0123, 2); // (r02 - r09) * k02
                int32x4_t _sum2n = vmull_lane_s16(vget_high_s16(_r02_s16), _k0123, 2);                

                // r1
                int8x8_t _r1 = vld1_s8(r1);
                int8x8_t _r1n = vld1_s8(r1+8);
                int8x8_t _r11 = vext_s8(_r1, _r1n, 1);
                int8x8_t _r12 = vext_s8(_r1, _r1n, 2);
                int16x8_t _r1_s16 = vmovl_s8(_r1);   // r10 - r17
                int16x8_t _r11_s16 = vmovl_s8(_r11); // r11 - r18
                int16x8_t _r12_s16 = vmovl_s8(_r12); // r12 - r19

                 _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_r1_s16), _k0123, 3); // (r10 - r17) * k03
                 _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_r1_s16), _k0123, 3);

                 _sum1 = vmlal_lane_s16(_sum1, vget_low_s16(_r11_s16), _k4567, 0); // (r11 - r18) * k04
                 _sum1n = vmlal_lane_s16(_sum1n, vget_high_s16(_r11_s16), _k4567, 0);

                 _sum2 = vmlal_lane_s16(_sum2, vget_low_s16(_r12_s16), _k4567, 1); // (r12 - r19) * k05
                 _sum2n = vmlal_lane_s16(_sum2n, vget_high_s16(_r12_s16), _k4567, 1); 

                int32x4_t _sum4 = vmull_lane_s16(vget_low_s16(_r1_s16), _k0123, 0); // (r10 - r17) * k00
                int32x4_t _sum4n = vmull_lane_s16(vget_high_s16(_r1_s16), _k0123, 0);

                int32x4_t _sum5 = vmull_lane_s16(vget_low_s16(_r11_s16), _k0123, 1); // (r11 - r18) * k01
                int32x4_t _sum5n = vmull_lane_s16(vget_high_s16(_r11_s16), _k0123, 1);

                int32x4_t _sum6 = vmull_lane_s16(vget_low_s16(_r12_s16), _k0123, 2); // (r12 - r19) * k02
                int32x4_t _sum6n = vmull_lane_s16(vget_high_s16(_r12_s16), _k0123, 2);  

                // r2
                int8x8_t _r2 = vld1_s8(r2);
                int8x8_t _r2n = vld1_s8(r2+8);
                int8x8_t _r21 = vext_s8(_r2, _r2n, 1);
                int8x8_t _r22 = vext_s8(_r2, _r2n, 2);
                int16x8_t _r2_s16 = vmovl_s8(_r2);   // r20 - r27
                int16x8_t _r21_s16 = vmovl_s8(_r21); // r21 - r28
                int16x8_t _r22_s16 = vmovl_s8(_r22); // r22 - r29

                 _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_r2_s16), _k4567, 2); // (r20 - r27) * k06
                 _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_r2_s16), _k4567, 2);

                 _sum1 = vmlal_lane_s16(_sum1, vget_low_s16(_r21_s16), _k4567, 3); // (r21 - r28) * k07
                 _sum1n = vmlal_lane_s16(_sum1n, vget_high_s16(_r21_s16), _k4567, 3);

                 _sum2 = vmlal_lane_s16(_sum2, vget_low_s16(_r22_s16), _k8xxx, 0); // (r22 - r29) * k08
                 _sum2n = vmlal_lane_s16(_sum2n, vget_high_s16(_r22_s16), _k8xxx, 0); 

                 _sum4 = vmlal_lane_s16(_sum4, vget_low_s16(_r2_s16), _k0123, 3); // (r20 - r27) * k03
                 _sum4n = vmlal_lane_s16(_sum4n, vget_high_s16(_r2_s16), _k0123, 3);

                 _sum5 = vmlal_lane_s16(_sum5, vget_low_s16(_r21_s16), _k4567, 0); // (r21 - r28) * k04
                 _sum5n = vmlal_lane_s16(_sum5n, vget_high_s16(_r21_s16), _k4567, 0);

                 _sum6 = vmlal_lane_s16(_sum6, vget_low_s16(_r22_s16), _k4567, 1); // (r22 - r29) * k05
                 _sum6n = vmlal_lane_s16(_sum6n, vget_high_s16(_r22_s16), _k4567, 1); 

                // r3
                int8x8_t _r3 = vld1_s8(r3);
                int8x8_t _r3n = vld1_s8(r3+8);
                int8x8_t _r31 = vext_s8(_r3, _r3n, 1);
                int8x8_t _r32 = vext_s8(_r3, _r3n, 2);
                int16x8_t _r3_s16 = vmovl_s8(_r3);   // r30 - r37
                int16x8_t _r31_s16 = vmovl_s8(_r31); // r31 - r38
                int16x8_t _r32_s16 = vmovl_s8(_r32); // r32 - r39

                _sum0 = vaddq_s32(_sum0, _sum1);
                _sum0n = vaddq_s32(_sum0n, _sum1n);
                _sum2 = vaddq_s32(_sum2, _sum0);
                _sum2n = vaddq_s32(_sum2n, _sum0n);

                vst1q_s32(outptr0, _sum2);
                vst1q_s32(outptr0+4, _sum2n);                

                 _sum4 = vmlal_lane_s16(_sum4, vget_low_s16(_r3_s16), _k4567, 2); // (r30 - r37) * k06
                 _sum4n = vmlal_lane_s16(_sum4n, vget_high_s16(_r3_s16), _k4567, 2);

                 _sum5 = vmlal_lane_s16(_sum5, vget_low_s16(_r31_s16), _k4567, 3); // (r31 - r38) * k07
                 _sum5n = vmlal_lane_s16(_sum5n, vget_high_s16(_r31_s16), _k4567, 3);

                 _sum6 = vmlal_lane_s16(_sum6, vget_low_s16(_r32_s16), _k8xxx, 0); // (r32 - r39) * k08
                 _sum6n = vmlal_lane_s16(_sum6n, vget_high_s16(_r32_s16), _k8xxx, 0); 

                _sum4 = vaddq_s32(_sum4, _sum5);
                _sum4n = vaddq_s32(_sum4n, _sum5n);
                _sum6 = vaddq_s32(_sum6, _sum4);
                _sum6n = vaddq_s32(_sum6n, _sum4n);

                vst1q_s32(outptr0n, _sum6);
                vst1q_s32(outptr0n+4, _sum6n);

                r0 += 8;
                r1 += 8;
                r2 += 8;
                r3 += 8;
                outptr0 += 8;
                outptr0n += 8;                
            }
#endif // __aarch64__            
#endif // __ARM_NEON
            for (; remain>0; remain--)
            {
                // TODO NEON
                int sum0 = 0;
                int sum0n = 0;

                sum0 += (int)r0[0] * kernel[0];
                sum0 += (int)r0[1] * kernel[1];
                sum0 += (int)r0[2] * kernel[2];
                sum0 += (int)r1[0] * kernel[3];
                sum0 += (int)r1[1] * kernel[4];
                sum0 += (int)r1[2] * kernel[5];
                sum0 += (int)r2[0] * kernel[6];
                sum0 += (int)r2[1] * kernel[7];
                sum0 += (int)r2[2] * kernel[8];

                sum0n += (int)r1[0] * kernel[0];
                sum0n += (int)r1[1] * kernel[1];
                sum0n += (int)r1[2] * kernel[2];
                sum0n += (int)r2[0] * kernel[3];
                sum0n += (int)r2[1] * kernel[4];
                sum0n += (int)r2[2] * kernel[5];
                sum0n += (int)r3[0] * kernel[6];
                sum0n += (int)r3[1] * kernel[7];
                sum0n += (int)r3[2] * kernel[8];

                *outptr0 = sum0;
                *outptr0n = sum0n;

                r0++;
                r1++;
                r2++;
                r3++;
                outptr0++;
                outptr0n++;
            }

            r0 += 2 + w;
            r1 += 2 + w;
            r2 += 2 + w;
            r3 += 2 + w;

            outptr0 += outw;
            outptr0n += outw;
        }

        for (; i < outh; i++)
        {
#if __ARM_NEON            
            int nn = outw >> 3;
            int remain = outw & 7;
#else
            int remain = outw;
#endif // __ARM_NEON

#if __ARM_NEON
#if __aarch64__
            if (nn > 0)
            {
            asm volatile(
                "0:                                   \n"
                "ld1    {v4.8b, v5.8b}, [%2]          \n"
                "ld1    {v6.8b, v7.8b}, [%3]          \n"
                "ld1    {v8.8b, v9.8b}, [%4]          \n"
                "add    %2, %2, #8                    \n"
                "add    %3, %3, #8                    \n"
                "add    %4, %4, #8                    \n"

                "ext    v12.8b, v4.8b, v5.8b, #1      \n"
                "ext    v13.8b, v4.8b, v5.8b, #2      \n"
                "ext    v14.8b, v6.8b, v7.8b, #1      \n"
                "ext    v15.8b, v6.8b, v7.8b, #2      \n"
                "ext    v16.8b, v8.8b, v9.8b, #1      \n"
                "ext    v17.8b, v8.8b, v9.8b, #2      \n"
                
                "sshll  v4.8h, v4.8b, #0              \n"// r00
                "sshll  v12.8h, v12.8b, #0            \n"// r01
                "sshll  v13.8h, v13.8b, #0            \n"// r02
                "sshll  v6.8h, v6.8b, #0              \n"// r10
                "sshll  v14.8h, v14.8b, #0            \n"// r11
                "sshll  v15.8h, v15.8b, #0            \n"// r12
                "sshll  v8.8h, v8.8b, #0              \n"// r20
                "sshll  v16.8h, v16.8b, #0            \n"// r21
                "sshll  v17.8h, v17.8b, #0            \n"// r22

                // r0
                "smull  v20.4s, v4.4h, %10.h[0]       \n"// (r00 - r07) * k00
                "smull2  v21.4s, v4.8h, %10.h[0]      \n"
                "smull  v22.4s, v12.4h, %10.h[1]      \n"// (r01 - r08) * k01
                "smull2  v23.4s, v12.8h, %10.h[1]     \n"
                "smull  v24.4s, v13.4h, %10.h[2]      \n"// (r02 - r09) * k02
                "smull2  v25.4s, v13.8h, %10.h[2]     \n"

                // r1
                "smlal  v20.4s, v6.4h, %10.h[3]       \n"// (r10 - r17) * k03
                "smlal2  v21.4s, v6.8h, %10.h[3]      \n"
                "smlal  v22.4s, v14.4h, %11.h[0]      \n"// (r11 - r18) * k04
                "smlal2  v23.4s, v14.8h, %11.h[0]     \n"
                "smlal  v24.4s, v15.4h, %11.h[1]      \n"// (r12 - r19) * k05
                "smlal2  v25.4s, v15.8h, %11.h[1]     \n"

                // r2
                "smlal  v20.4s, v8.4h, %11.h[2]       \n"// (r20 - r27) * k06
                "smlal2  v21.4s, v8.8h, %11.h[2]      \n"
                "smlal  v22.4s, v16.4h, %11.h[3]      \n"// (r21 - r28) * k07
                "smlal2  v23.4s, v16.8h, %11.h[3]     \n"
                "smlal  v24.4s, v17.4h, %12.h[0]      \n"// (r22 - r29) * k08
                "smlal2  v25.4s, v17.8h, %12.h[0]     \n"

                // add and save
                "add    v20.4s, v20.4s, v22.4s        \n"
                "add    v21.4s, v21.4s, v23.4s        \n"
                "add    v20.4s, v20.4s, v24.4s        \n"
                "add    v21.4s, v21.4s, v25.4s        \n"

                "st1    {v20.4s, v21.4s}, [%1], #32   \n"

                "subs   %w0, %w0, #1                  \n"
                "bne    0b                            \n"

                : "=r"(nn),       // %0
                  "=r"(outptr0),  // %1
                  "=r"(r0),       // %2
                  "=r"(r1),       // %3
                  "=r"(r2)        // %4
                : "0"(nn),
                  "1"(outptr0),
                  "2"(r0),
                  "3"(r1),
                  "4"(r2),
                  "w"(_k0123),    // %10
                  "w"(_k4567),    // %11
                  "w"(_k8xxx)     // %12
                : "cc", "memory", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25"              
            );
            }
#else
            for (; nn >0; nn--)
            {
                // r0
                int8x8_t _r0 = vld1_s8(r0);
                int8x8_t _r0n = vld1_s8(r0+8);
                int8x8_t _r01 = vext_s8(_r0, _r0n, 1);
                int8x8_t _r02 = vext_s8(_r0, _r0n, 2);
                int16x8_t _r0_s16 = vmovl_s8(_r0);   // r00 - r07
                int16x8_t _r01_s16 = vmovl_s8(_r01); // r01 - r08 
                int16x8_t _r02_s16 = vmovl_s8(_r02); // r02 - r09

                int32x4_t _sum0 = vmull_lane_s16(vget_low_s16(_r0_s16), _k0123, 0); // (r00 - r07) * k00
                int32x4_t _sum0n = vmull_lane_s16(vget_high_s16(_r0_s16), _k0123, 0);

                int32x4_t _sum1 = vmull_lane_s16(vget_low_s16(_r01_s16), _k0123, 1); // (r01 - r08) * k01
                int32x4_t _sum1n = vmull_lane_s16(vget_high_s16(_r01_s16), _k0123, 1);

                int32x4_t _sum2 = vmull_lane_s16(vget_low_s16(_r02_s16), _k0123, 2); // (r02 - r09) * k02
                int32x4_t _sum2n = vmull_lane_s16(vget_high_s16(_r02_s16), _k0123, 2);                

                // r1
                int8x8_t _r1 = vld1_s8(r1);
                int8x8_t _r1n = vld1_s8(r1+8);
                int8x8_t _r11 = vext_s8(_r1, _r1n, 1);
                int8x8_t _r12 = vext_s8(_r1, _r1n, 2);
                int16x8_t _r1_s16 = vmovl_s8(_r1);   // r10 - r17
                int16x8_t _r11_s16 = vmovl_s8(_r11); // r11 - r18
                int16x8_t _r12_s16 = vmovl_s8(_r12); // r12 - r19

                 _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_r1_s16), _k0123, 3); // (r10 - r17) * k03
                 _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_r1_s16), _k0123, 3);

                 _sum1 = vmlal_lane_s16(_sum1, vget_low_s16(_r11_s16), _k4567, 0); // (r11 - r18) * k04
                 _sum1n = vmlal_lane_s16(_sum1n, vget_high_s16(_r11_s16), _k4567, 0);

                 _sum2 = vmlal_lane_s16(_sum2, vget_low_s16(_r12_s16), _k4567, 1); // (r12 - r19) * k05
                 _sum2n = vmlal_lane_s16(_sum2n, vget_high_s16(_r12_s16), _k4567, 1); 

                // r2
                int8x8_t _r2 = vld1_s8(r2);
                int8x8_t _r2n = vld1_s8(r2+8);
                int8x8_t _r21 = vext_s8(_r2, _r2n, 1);
                int8x8_t _r22 = vext_s8(_r2, _r2n, 2);
                int16x8_t _r2_s16 = vmovl_s8(_r2);   // r20 - r27
                int16x8_t _r21_s16 = vmovl_s8(_r21); // r21 - r28
                int16x8_t _r22_s16 = vmovl_s8(_r22); // r22 - r29

                 _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_r2_s16), _k4567, 2); // (r20 - r27) * k06
                 _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_r2_s16), _k4567, 2);

                 _sum1 = vmlal_lane_s16(_sum1, vget_low_s16(_r21_s16), _k4567, 3); // (r21 - r28) * k07
                 _sum1n = vmlal_lane_s16(_sum1n, vget_high_s16(_r21_s16), _k4567, 3);

                 _sum2 = vmlal_lane_s16(_sum2, vget_low_s16(_r22_s16), _k8xxx, 0); // (r22 - r29) * k08
                 _sum2n = vmlal_lane_s16(_sum2n, vget_high_s16(_r22_s16), _k8xxx, 0); 

                _sum0 = vaddq_s32(_sum0, _sum1);
                _sum0n = vaddq_s32(_sum0n, _sum1n);
                _sum2 = vaddq_s32(_sum2, _sum0);
                _sum2n = vaddq_s32(_sum2n, _sum0n);

                vst1q_s32(outptr0, _sum2);
                vst1q_s32(outptr0+4, _sum2n);                  

                r0 += 8;
                r1 += 8;
                r2 += 8;
                outptr0 += 8;
            }
#endif // __aarch64__            
#endif // __ARM_NEON
            for (; remain>0; remain--)
            {
                int sum = 0;

                sum += (int)r0[0] * kernel[0];
                sum += (int)r0[1] * kernel[1];
                sum += (int)r0[2] * kernel[2];
                sum += (int)r1[0] * kernel[3];
                sum += (int)r1[1] * kernel[4];
                sum += (int)r1[2] * kernel[5];
                sum += (int)r2[0] * kernel[6];
                sum += (int)r2[1] * kernel[7];
                sum += (int)r2[2] * kernel[8];

                *outptr0 = sum;

                r0++;
                r1++;
                r2++;
                outptr0++;
            }   

            r0 += 2;
            r1 += 2;
            r2 += 2;
        }
    }
}

static void convdw3x3s2_int8_neon(const Mat &bottom_blob, Mat &top_blob, const Mat &_kernel, const Option& opt)
{
    int w = bottom_blob.w;

    int outw = top_blob.w;
    int outh = top_blob.h;
    int outch = top_blob.c;

    const int tailstep = w - 2*outw + w;

    #pragma omp parallel for num_threads(opt.num_threads)
    for (int p=0; p<outch; p++)
    {
        Mat out = top_blob.channel(p);

        const signed char* kernel = (const signed char*)_kernel + p*9;

        int* outptr = out;

        const signed char* img = bottom_blob.channel(p);

        const signed char* r0 = img;
        const signed char* r1 = img + w;
        const signed char* r2 = img + w*2;

        int i = 0;
#if __ARM_NEON 
        int8x16_t _k0123456789x = vld1q_s8(kernel);
        int16x8_t _k_s16 = vmovl_s8(vget_low_s8(_k0123456789x));
        int16x8_t _kn_s16 = vmovl_s8(vget_high_s8(_k0123456789x));

        int16x4_t _k0123 = vget_low_s16(_k_s16);
        int16x4_t _k4567 = vget_high_s16(_k_s16);
        int16x4_t _k8xxx = vget_low_s16(_kn_s16);
#endif // __ARM_NEON 
        for (; i < outh; i++)
        {     
#if __ARM_NEON                   
            int nn = outw >> 3;
            int remain = outw & 7;
#else
            int remain = outw;
#endif // __ARM_NEON

#if __ARM_NEON
            for (; nn > 0; nn--)
            {
                // r0
                int8x8x2_t _r0 = vld2_s8(r0);
                int8x8x2_t _r0n = vld2_s8(r0+16);
                int8x8_t _r00 = _r0.val[0]; // r00 - r014
                int8x8_t _r01 = _r0.val[1]; // r01 - r015
                int8x8_t _r02 = vext_s8(_r00, _r0n.val[0], 1); // r02 - r016

                int16x8_t _r00_s16 = vmovl_s8(_r00); // r00 - r014
                int16x8_t _r01_s16 = vmovl_s8(_r01); // r01 - r015
                int16x8_t _r02_s16 = vmovl_s8(_r02); // r02 - r016

                int32x4_t _sum0_s32 = vmull_lane_s16(vget_low_s16(_r00_s16), _k0123, 0); // (r00-r06) * k00
                int32x4_t _sum0n_s32 = vmull_lane_s16(vget_high_s16(_r00_s16), _k0123, 0);

                int32x4_t _sum1_s32 = vmull_lane_s16(vget_low_s16(_r01_s16), _k0123, 1); // (r01-r07) * k01
                int32x4_t _sum1n_s32 = vmull_lane_s16(vget_high_s16(_r01_s16), _k0123, 1);                

                int32x4_t _sum2_s32 = vmull_lane_s16(vget_low_s16(_r02_s16), _k0123, 2); // (r02-r08) * k02
                int32x4_t _sum2n_s32 = vmull_lane_s16(vget_high_s16(_r02_s16), _k0123, 2); 

                // r1
                int8x8x2_t _r1 = vld2_s8(r1);
                int8x8x2_t _r1n = vld2_s8(r1+16);
                int8x8_t _r10 = _r1.val[0]; // r10 - r114
                int8x8_t _r11 = _r1.val[1]; // r11 - r115
                int8x8_t _r12 = vext_s8(_r10, _r1n.val[0], 1); // r12 - r116

                int16x8_t _r10_s16 = vmovl_s8(_r10); // r10 - r114
                int16x8_t _r11_s16 = vmovl_s8(_r11); // r11 - r115
                int16x8_t _r12_s16 = vmovl_s8(_r12); // r12 - r116

                _sum0_s32 = vmlal_lane_s16(_sum0_s32, vget_low_s16(_r10_s16), _k0123, 3); // (r10-r16) * k03
                _sum0n_s32 = vmlal_lane_s16(_sum0n_s32, vget_high_s16(_r10_s16), _k0123, 3);

                _sum1_s32 = vmlal_lane_s16(_sum1_s32, vget_low_s16(_r11_s16), _k4567, 0); // (r11-r17) * k04
                _sum1n_s32 = vmlal_lane_s16(_sum1n_s32, vget_high_s16(_r11_s16), _k4567, 0);                

                _sum2_s32 = vmlal_lane_s16(_sum2_s32, vget_low_s16(_r12_s16), _k4567, 1); // (r12-r18) * k05
                _sum2n_s32 = vmlal_lane_s16(_sum2n_s32, vget_high_s16(_r12_s16), _k4567, 1); 

                // r2
                int8x8x2_t _r2 = vld2_s8(r2);
                int8x8x2_t _r2n = vld2_s8(r2+16);
                int8x8_t _r20 = _r2.val[0]; // r20 - r214
                int8x8_t _r21 = _r2.val[1]; // r21 - r215
                int8x8_t _r22 = vext_s8(_r20, _r2n.val[0], 1); // r22 - r216

                int16x8_t _r20_s16 = vmovl_s8(_r20); // r20 - r214
                int16x8_t _r21_s16 = vmovl_s8(_r21); // r21 - r215
                int16x8_t _r22_s16 = vmovl_s8(_r22); // r22 - r216

                _sum0_s32 = vmlal_lane_s16(_sum0_s32, vget_low_s16(_r20_s16), _k4567, 2); // (r20-r26) * k06
                _sum0n_s32 = vmlal_lane_s16(_sum0n_s32, vget_high_s16(_r20_s16), _k4567, 2);

                _sum1_s32 = vmlal_lane_s16(_sum1_s32, vget_low_s16(_r21_s16), _k4567, 3); // (r21-r27) * k07
                _sum1n_s32 = vmlal_lane_s16(_sum1n_s32, vget_high_s16(_r21_s16), _k4567, 3);                

                _sum2_s32 = vmlal_lane_s16(_sum2_s32, vget_low_s16(_r22_s16), _k8xxx, 0); // (r22-r28) * k08
                _sum2n_s32 = vmlal_lane_s16(_sum2n_s32, vget_high_s16(_r22_s16), _k8xxx, 0); 

                _sum0_s32 = vaddq_s32(_sum0_s32, _sum1_s32);
                _sum0n_s32 = vaddq_s32(_sum0n_s32, _sum1n_s32);
                _sum2_s32 = vaddq_s32(_sum2_s32, _sum0_s32);
                _sum2n_s32 = vaddq_s32(_sum2n_s32, _sum0n_s32);

                vst1q_s32(outptr, _sum2_s32);
                vst1q_s32(outptr+4, _sum2n_s32);

                r0 += 16;
                r1 += 16;
                r2 += 16;
                outptr += 8;
            }       
#endif // __ARM_NEON
            for (; remain>0; remain--)
            {
                int sum = 0;
                
                sum += (int)r0[0] * kernel[0];
                sum += (int)r0[1] * kernel[1];
                sum += (int)r0[2] * kernel[2];
                sum += (int)r1[0] * kernel[3];
                sum += (int)r1[1] * kernel[4];
                sum += (int)r1[2] * kernel[5];
                sum += (int)r2[0] * kernel[6];
                sum += (int)r2[1] * kernel[7];
                sum += (int)r2[2] * kernel[8];

                *outptr = sum;

                r0 += 2;
                r1 += 2;
                r2 += 2;
                outptr++;
            }

            r0 += tailstep;
            r1 += tailstep;
            r2 += tailstep;
        }
    }
}
