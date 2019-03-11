// BUG1989 is pleased to support the open source community by supporting ncnn available.
//
// Copyright (C) 2019 BUG1989. All rights reserved.
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

static void conv_im2col_sgemm_int8_neon(const Mat &bottom_blob, Mat &top_blob, const Mat &_kernel, \
            const int kernel_w, const int kernel_h, const int stride_w, const int stride_h, const Option& opt)
{
    int w = bottom_blob.w;
    int inch = bottom_blob.c;

    int outw = top_blob.w;
    int outh = top_blob.h;
    int outch = top_blob.c;

    const signed char *kernel = _kernel;

    // im2col
    Mat bottom_im2col(outw*outh, kernel_h*kernel_w*inch, 1UL, opt.workspace_allocator);
    {
        const int stride = kernel_h*kernel_w*outw*outh;
        signed char* ret = (signed char*)bottom_im2col;
    
        #pragma omp parallel for num_threads(opt.num_threads)
        for (int p=0; p<inch; p++)
        {
            const signed char* input = bottom_blob.channel(p);
            int retID = stride * p;
            for (int u=0; u<kernel_h; u++)
            {
                for (int v=0; v<kernel_w; v++)
                {
                    for (int i=0; i<outh; i++)
                    {
                        for (int j=0; j<outw; j++)
                        {
                            int row = u + i * stride_h;
                            int col = v + j * stride_w;
                            int index = row * w + col;
                            ret[retID] = input[index];
                            retID++;
                        }
                    }
                }
            }
        }
    }

    int kernel_size = kernel_w * kernel_h;
    int out_size = outw * outh;

    // bottom_im2col memory packed 8 x 8
    Mat bottom_tm(8*kernel_size, inch, out_size/8 + out_size%8, (size_t)1u, opt.workspace_allocator);
    {
        int nn_size = out_size >> 3;
        int remain_size_start = nn_size << 3;

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int ii=0; ii<nn_size; ii++)
        {
            int i = ii * 8;

            const signed char* img0 = bottom_im2col.channel(0);
            img0 += i;

            signed char* tmpptr = bottom_tm.channel(i/8);

            for (int q=0; q<inch*kernel_size; q++)
            {
#if __ARM_NEON
#if __aarch64__
                asm volatile(
                    "prfm    pldl1keep, [%0, #64]    \n"
                    "ld1     {v0.8b}, [%0]           \n"
                    "st1     {v0.8b}, [%1]           \n"
                    : "=r"(img0),   // %0
                      "=r"(tmpptr)  // %1
                    : "0"(img0),
                      "1"(tmpptr)
                    : "cc", "memory", "v0"
                );                
#else
                asm volatile(
                    "pld        [%0, #64]     \n"
                    "vld1.s8   {d0}, [%0]     \n"
                    "vst1.s8   {d0}, [%1]     \n"
                    : "=r"(img0),   // %0
                      "=r"(tmpptr)  // %1
                    : "0"(img0),
                      "1"(tmpptr)
                    : "cc", "memory", "d0"
                );
#endif // __aarch64__                
#else                
                tmpptr[0] = img0[0];
                tmpptr[1] = img0[1];
                tmpptr[2] = img0[2];
                tmpptr[3] = img0[3];
                tmpptr[4] = img0[4];
                tmpptr[5] = img0[5];
                tmpptr[6] = img0[6];
                tmpptr[7] = img0[7];
#endif // __ARM_NEON              
                tmpptr += 8;
                img0 += out_size;
            }
        }

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int i=remain_size_start; i<out_size; i++)
        {
            const signed char* img0 = bottom_im2col.channel(0);
            img0 += i;

            signed char* tmpptr = bottom_tm.channel(i/8 + i%8);

            for (int q=0; q<inch*kernel_size; q++)
            {
                tmpptr[0] = img0[0];

                tmpptr += 1;
                img0 += out_size;
            }
        }       
    }
    
#if __aarch64__
    // kernel memory packed 8 x 8
    Mat kernel_tm(8*kernel_size, inch, outch/8 + (outch%8)/4 + outch%4, (size_t)1u, opt.workspace_allocator);
    {
        int nn_outch = 0;
        int remain_outch_start = 0;

        nn_outch = outch >> 3;
        remain_outch_start = nn_outch << 3;      
        
        #pragma omp parallel for num_threads(opt.num_threads)
        for (int pp=0; pp<nn_outch; pp++)
        {
            int p = pp * 8;

            const signed char* k0 = kernel + (p+0)*inch*kernel_size;
            const signed char* k1 = kernel + (p+1)*inch*kernel_size;
            const signed char* k2 = kernel + (p+2)*inch*kernel_size;
            const signed char* k3 = kernel + (p+3)*inch*kernel_size;
            const signed char* k4 = kernel + (p+4)*inch*kernel_size;
            const signed char* k5 = kernel + (p+5)*inch*kernel_size;
            const signed char* k6 = kernel + (p+6)*inch*kernel_size;
            const signed char* k7 = kernel + (p+7)*inch*kernel_size;                        

            signed char* ktmp = kernel_tm.channel(p/8);

            for (int q=0; q<inch*kernel_size; q++)
            {
                ktmp[0] = k0[0];
                ktmp[1] = k1[0];
                ktmp[2] = k2[0];
                ktmp[3] = k3[0];
                ktmp[4] = k4[0];
                ktmp[5] = k5[0];
                ktmp[6] = k6[0];
                ktmp[7] = k7[0];                
                ktmp += 8;

                k0 += 1;
                k1 += 1;
                k2 += 1;
                k3 += 1;
                k4 += 1;
                k5 += 1;
                k6 += 1;
                k7 += 1;                
            }            
        }

        nn_outch = (outch - remain_outch_start) >> 2;

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int pp=0; pp<nn_outch; pp++)
        {
            int p = remain_outch_start + pp * 4;

            const signed char* k0 = kernel + (p+0)*inch*kernel_size;
            const signed char* k1 = kernel + (p+1)*inch*kernel_size;
            const signed char* k2 = kernel + (p+2)*inch*kernel_size;
            const signed char* k3 = kernel + (p+3)*inch*kernel_size;

            signed char* ktmp = kernel_tm.channel(p/8 + (p%8)/4);

            for (int q=0; q<inch*kernel_size; q++)
            {
                ktmp[0] = k0[0];
                ktmp[1] = k1[0];
                ktmp[2] = k2[0];
                ktmp[3] = k3[0];
                ktmp += 4;

                k0 += 1;
                k1 += 1;
                k2 += 1;
                k3 += 1;
            }
        }

        remain_outch_start += nn_outch << 2;

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int p=remain_outch_start; p<outch; p++)
        {
            const signed char* k0 = kernel + (p+0)*inch*kernel_size;

            signed char* ktmp = kernel_tm.channel(p/8 + (p%8)/4 + p%4);

            for (int q=0; q<inch*kernel_size; q++)
            {
                ktmp[0] = k0[0];
                ktmp++;
                k0++;
            }
        }
    }
#else
    // kernel memory packed 4 x 8
    Mat kernel_tm(4*kernel_size, inch, outch/4 + outch%4, (size_t)1u, opt.workspace_allocator);
    {
        int nn_outch = 0;
        int remain_outch_start = 0;

        nn_outch = outch >> 2;
        remain_outch_start = nn_outch << 2;      
        
        #pragma omp parallel for num_threads(opt.num_threads)
        for (int pp=0; pp<nn_outch; pp++)
        {
            int p = pp * 4;

            const signed char* k0 = kernel + (p+0)*inch*kernel_size;
            const signed char* k1 = kernel + (p+1)*inch*kernel_size;
            const signed char* k2 = kernel + (p+2)*inch*kernel_size;
            const signed char* k3 = kernel + (p+3)*inch*kernel_size;

            signed char* ktmp = kernel_tm.channel(p/4);

            for (int q=0; q<inch*kernel_size; q++)
            {
                ktmp[0] = k0[0];
                ktmp[1] = k1[0];
                ktmp[2] = k2[0];
                ktmp[3] = k3[0];
                ktmp += 4;

                k0 += 1;
                k1 += 1;
                k2 += 1;
                k3 += 1;
            }
        }

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int p=remain_outch_start; p<outch; p++)
        {
            const signed char* k0 = kernel + (p+0)*inch*kernel_size;

            signed char* ktmp = kernel_tm.channel(p/4 + p%4);

            for (int q=0; q<inch*kernel_size; q++)
            {
                ktmp[0] = k0[0];
                ktmp++;
                k0++;
            }
        }
    }
#endif

    // sgemm(int M, int N, int L, float* A, float* B, float* C)
    {
        //int M = outch;  // outch
        int N = outw * outh; // outsize or out stride
        int L = kernel_w * kernel_h * inch; // ksize * inch

        int nn_outch = 0;
        int remain_outch_start = 0;

#if __aarch64__
        nn_outch = outch >> 3;
        remain_outch_start = nn_outch << 3;
#endif  

#if __aarch64__
        #pragma omp parallel for num_threads(opt.num_threads)
        for (int pp=0; pp<nn_outch; pp++)
        {
            int i = pp * 8;

            int* output0 = top_blob.channel(i);
            int* output1 = top_blob.channel(i+1);
            int* output2 = top_blob.channel(i+2);
            int* output3 = top_blob.channel(i+3);
            int* output4 = top_blob.channel(i+4);
            int* output5 = top_blob.channel(i+5);
            int* output6 = top_blob.channel(i+6);
            int* output7 = top_blob.channel(i+7);

            int j=0;
            for (; j+7<N; j=j+8)
            {
                signed char* vb = bottom_tm.channel(j/8);
                signed char* va = kernel_tm.channel(i/8);
#if __aarch64__
                asm volatile(
                    "eor    v16.16b, v16.16b, v16.16b    \n" // sum0
                    "eor    v17.16b, v17.16b, v17.16b    \n" // sum0n
                    "eor    v18.16b, v18.16b, v18.16b    \n" // sum1
                    "eor    v19.16b, v19.16b, v19.16b    \n" // sum1n
                    "eor    v20.16b, v20.16b, v20.16b    \n" // sum2
                    "eor    v21.16b, v21.16b, v21.16b    \n" // sum2n
                    "eor    v22.16b, v22.16b, v22.16b    \n" // sum3
                    "eor    v23.16b, v23.16b, v23.16b    \n" // sum3n
                    "eor    v24.16b, v24.16b, v24.16b    \n" // sum4
                    "eor    v25.16b, v25.16b, v25.16b    \n" // sum4n
                    "eor    v26.16b, v26.16b, v26.16b    \n" // sum5
                    "eor    v27.16b, v27.16b, v27.16b    \n" // sum5n
                    "eor    v28.16b, v28.16b, v28.16b    \n" // sum6
                    "eor    v29.16b, v29.16b, v29.16b    \n" // sum6n
                    "eor    v30.16b, v30.16b, v30.16b    \n" // sum7
                    "eor    v31.16b, v31.16b, v31.16b    \n" // sum7n

                    "lsr         w4, %w20, #3            \n"// r4 = nn = L >> 3
                    "cmp         w4, #0                  \n"
                    "beq         1f                      \n"

                    "0:                                  \n"// for (; k+7<L; k=k+8)

                    "prfm   pldl1keep, [%9, #128]                       \n"
                    "ld1    {v0.8b, v1.8b, v2.8b, v3.8b}, [%9], #32     \n"
                    "ld1    {v4.8b, v5.8b, v6.8b, v7.8b}, [%9], #32     \n"

                    "prfm   pldl1keep, [%8, #128]                       \n"
                    "ld1    {v8.8b, v9.8b, v10.8b, v11.8b}, [%8], #32   \n"
                    "ld1    {v12.8b, v13.8b, v14.8b, v15.8b}, [%8], #32 \n"

                    "sshll    v0.8h, v0.8b, #0           \n" // k00 - k70
                    "sshll    v1.8h, v1.8b, #0           \n" // k01 - k71
                    "sshll    v2.8h, v2.8b, #0           \n" // k02 - k72
                    "sshll    v3.8h, v3.8b, #0           \n" // k03 - k73
                    "sshll    v4.8h, v4.8b, #0           \n" // k04 - k74
                    "sshll    v5.8h, v5.8b, #0           \n" // k05 - k75
                    "sshll    v6.8h, v6.8b, #0           \n" // k06 - k76
                    "sshll    v7.8h, v7.8b, #0           \n" // k07 - k77

                    "sshll    v8.8h, v8.8b, #0           \n" // a00 - a70
                    "sshll    v9.8h, v9.8b, #0           \n" // a01 - a71
                    "sshll    v10.8h, v10.8b, #0         \n" // a02 - a72
                    "sshll    v11.8h, v11.8b, #0         \n" // a03 - a73
                    "sshll    v12.8h, v12.8b, #0         \n" // a04 - a74
                    "sshll    v13.8h, v13.8b, #0         \n" // a05 - a75
                    "sshll    v14.8h, v14.8b, #0         \n" // a06 - a76
                    "sshll    v15.8h, v15.8b, #0         \n" // a07 - a77
                    // k0
                    "smlal    v16.4s, v8.4h, v0.h[0]     \n"// sum0 += (a00-a70) * k00
                    "smlal2   v17.4s, v8.8h, v0.h[0]     \n"//
                    "smlal    v18.4s, v8.4h, v0.h[1]     \n"// sum1 += (a00-a70) * k10
                    "smlal2   v19.4s, v8.8h, v0.h[1]     \n"//
                    "smlal    v20.4s, v8.4h, v0.h[2]     \n"// sum2 += (a00-a70) * k20
                    "smlal2   v21.4s, v8.8h, v0.h[2]     \n"//
                    "smlal    v22.4s, v8.4h, v0.h[3]     \n"// sum3 += (a00-a70) * k30
                    "smlal2   v23.4s, v8.8h, v0.h[3]     \n"//
                    "smlal    v24.4s, v8.4h, v0.h[4]     \n"// sum4 += (a00-a70) * k40
                    "smlal2   v25.4s, v8.8h, v0.h[4]     \n"//
                    "smlal    v26.4s, v8.4h, v0.h[5]     \n"// sum5 += (a00-a70) * k50
                    "smlal2   v27.4s, v8.8h, v0.h[5]     \n"//
                    "smlal    v28.4s, v8.4h, v0.h[6]     \n"// sum6 += (a00-a70) * k60
                    "smlal2   v29.4s, v8.8h, v0.h[6]     \n"//
                    "smlal    v30.4s, v8.4h, v0.h[7]     \n"// sum7 += (a00-a70) * k70
                    "smlal2   v31.4s, v8.8h, v0.h[7]     \n"//
                    // k1
                    "smlal    v16.4s, v9.4h, v1.h[0]     \n"// sum0 += (a01-a71) * k01
                    "smlal2   v17.4s, v9.8h, v1.h[0]     \n"//
                    "smlal    v18.4s, v9.4h, v1.h[1]     \n"// sum1 += (a01-a71) * k11
                    "smlal2   v19.4s, v9.8h, v1.h[1]     \n"//
                    "smlal    v20.4s, v9.4h, v1.h[2]     \n"// sum2 += (a01-a71) * k21
                    "smlal2   v21.4s, v9.8h, v1.h[2]     \n"//
                    "smlal    v22.4s, v9.4h, v1.h[3]     \n"// sum3 += (a01-a71) * k31
                    "smlal2   v23.4s, v9.8h, v1.h[3]     \n"//
                    "smlal    v24.4s, v9.4h, v1.h[4]     \n"// sum4 += (a01-a71) * k41
                    "smlal2   v25.4s, v9.8h, v1.h[4]     \n"//
                    "smlal    v26.4s, v9.4h, v1.h[5]     \n"// sum5 += (a01-a71) * k51
                    "smlal2   v27.4s, v9.8h, v1.h[5]     \n"//
                    "smlal    v28.4s, v9.4h, v1.h[6]     \n"// sum6 += (a01-a71) * k61
                    "smlal2   v29.4s, v9.8h, v1.h[6]     \n"//
                    "smlal    v30.4s, v9.4h, v1.h[7]     \n"// sum7 += (a01-a71) * k71
                    "smlal2   v31.4s, v9.8h, v1.h[7]     \n"//
                    // k2
                    "smlal    v16.4s, v10.4h, v2.h[0]    \n"// sum0 += (a00-a70) * k00
                    "smlal2   v17.4s, v10.8h, v2.h[0]    \n"//
                    "smlal    v18.4s, v10.4h, v2.h[1]    \n"// sum1 += (a00-a70) * k10
                    "smlal2   v19.4s, v10.8h, v2.h[1]    \n"//
                    "smlal    v20.4s, v10.4h, v2.h[2]    \n"// sum2 += (a00-a70) * k20
                    "smlal2   v21.4s, v10.8h, v2.h[2]    \n"//
                    "smlal    v22.4s, v10.4h, v2.h[3]    \n"// sum3 += (a00-a70) * k30
                    "smlal2   v23.4s, v10.8h, v2.h[3]    \n"//
                    "smlal    v24.4s, v10.4h, v2.h[4]    \n"// sum4 += (a00-a70) * k40
                    "smlal2   v25.4s, v10.8h, v2.h[4]    \n"//
                    "smlal    v26.4s, v10.4h, v2.h[5]    \n"// sum5 += (a00-a70) * k50
                    "smlal2   v27.4s, v10.8h, v2.h[5]    \n"//
                    "smlal    v28.4s, v10.4h, v2.h[6]    \n"// sum6 += (a00-a70) * k60
                    "smlal2   v29.4s, v10.8h, v2.h[6]    \n"//
                    "smlal    v30.4s, v10.4h, v2.h[7]    \n"// sum7 += (a00-a70) * k70
                    "smlal2   v31.4s, v10.8h, v2.h[7]    \n"//
                    // k3
                    "smlal    v16.4s, v11.4h, v3.h[0]    \n"// sum0 += (a00-a70) * k00
                    "smlal2   v17.4s, v11.8h, v3.h[0]    \n"//
                    "smlal    v18.4s, v11.4h, v3.h[1]    \n"// sum1 += (a00-a70) * k10
                    "smlal2   v19.4s, v11.8h, v3.h[1]    \n"//
                    "smlal    v20.4s, v11.4h, v3.h[2]    \n"// sum2 += (a00-a70) * k20
                    "smlal2   v21.4s, v11.8h, v3.h[2]    \n"//
                    "smlal    v22.4s, v11.4h, v3.h[3]    \n"// sum3 += (a00-a70) * k30
                    "smlal2   v23.4s, v11.8h, v3.h[3]    \n"//
                    "smlal    v24.4s, v11.4h, v3.h[4]    \n"// sum4 += (a00-a70) * k40
                    "smlal2   v25.4s, v11.8h, v3.h[4]    \n"//
                    "smlal    v26.4s, v11.4h, v3.h[5]    \n"// sum5 += (a00-a70) * k50
                    "smlal2   v27.4s, v11.8h, v3.h[5]    \n"//
                    "smlal    v28.4s, v11.4h, v3.h[6]    \n"// sum6 += (a00-a70) * k60
                    "smlal2   v29.4s, v11.8h, v3.h[6]    \n"//
                    "smlal    v30.4s, v11.4h, v3.h[7]    \n"// sum7 += (a00-a70) * k70
                    "smlal2   v31.4s, v11.8h, v3.h[7]    \n"//
                    // k4
                    "smlal    v16.4s, v12.4h, v4.h[0]    \n"// sum0 += (a00-a70) * k00
                    "smlal2   v17.4s, v12.8h, v4.h[0]    \n"//
                    "smlal    v18.4s, v12.4h, v4.h[1]    \n"// sum1 += (a00-a70) * k10
                    "smlal2   v19.4s, v12.8h, v4.h[1]    \n"//
                    "smlal    v20.4s, v12.4h, v4.h[2]    \n"// sum2 += (a00-a70) * k20
                    "smlal2   v21.4s, v12.8h, v4.h[2]    \n"//
                    "smlal    v22.4s, v12.4h, v4.h[3]    \n"// sum3 += (a00-a70) * k30
                    "smlal2   v23.4s, v12.8h, v4.h[3]    \n"//
                    "smlal    v24.4s, v12.4h, v4.h[4]    \n"// sum4 += (a00-a70) * k40
                    "smlal2   v25.4s, v12.8h, v4.h[4]    \n"//
                    "smlal    v26.4s, v12.4h, v4.h[5]    \n"// sum5 += (a00-a70) * k50
                    "smlal2   v27.4s, v12.8h, v4.h[5]    \n"//
                    "smlal    v28.4s, v12.4h, v4.h[6]    \n"// sum6 += (a00-a70) * k60
                    "smlal2   v29.4s, v12.8h, v4.h[6]    \n"//
                    "smlal    v30.4s, v12.4h, v4.h[7]    \n"// sum7 += (a00-a70) * k70
                    "smlal2   v31.4s, v12.8h, v4.h[7]    \n"//
                    // k5
                    "smlal    v16.4s, v13.4h, v5.h[0]    \n"// sum0 += (a00-a70) * k00
                    "smlal2   v17.4s, v13.8h, v5.h[0]    \n"//
                    "smlal    v18.4s, v13.4h, v5.h[1]    \n"// sum1 += (a00-a70) * k10
                    "smlal2   v19.4s, v13.8h, v5.h[1]    \n"//
                    "smlal    v20.4s, v13.4h, v5.h[2]    \n"// sum2 += (a00-a70) * k20
                    "smlal2   v21.4s, v13.8h, v5.h[2]    \n"//
                    "smlal    v22.4s, v13.4h, v5.h[3]    \n"// sum3 += (a00-a70) * k30
                    "smlal2   v23.4s, v13.8h, v5.h[3]    \n"//
                    "smlal    v24.4s, v13.4h, v5.h[4]    \n"// sum4 += (a00-a70) * k40
                    "smlal2   v25.4s, v13.8h, v5.h[4]    \n"//
                    "smlal    v26.4s, v13.4h, v5.h[5]    \n"// sum5 += (a00-a70) * k50
                    "smlal2   v27.4s, v13.8h, v5.h[5]    \n"//
                    "smlal    v28.4s, v13.4h, v5.h[6]    \n"// sum6 += (a00-a70) * k60
                    "smlal2   v29.4s, v13.8h, v5.h[6]    \n"//
                    "smlal    v30.4s, v13.4h, v5.h[7]    \n"// sum7 += (a00-a70) * k70
                    "smlal2   v31.4s, v13.8h, v5.h[7]    \n"//
                    // k6
                    "smlal    v16.4s, v14.4h, v6.h[0]    \n"// sum0 += (a00-a70) * k00
                    "smlal2   v17.4s, v14.8h, v6.h[0]    \n"//
                    "smlal    v18.4s, v14.4h, v6.h[1]    \n"// sum1 += (a00-a70) * k10
                    "smlal2   v19.4s, v14.8h, v6.h[1]    \n"//
                    "smlal    v20.4s, v14.4h, v6.h[2]    \n"// sum2 += (a00-a70) * k20
                    "smlal2   v21.4s, v14.8h, v6.h[2]    \n"//
                    "smlal    v22.4s, v14.4h, v6.h[3]    \n"// sum3 += (a00-a70) * k30
                    "smlal2   v23.4s, v14.8h, v6.h[3]    \n"//
                    "smlal    v24.4s, v14.4h, v6.h[4]    \n"// sum4 += (a00-a70) * k40
                    "smlal2   v25.4s, v14.8h, v6.h[4]    \n"//
                    "smlal    v26.4s, v14.4h, v6.h[5]    \n"// sum5 += (a00-a70) * k50
                    "smlal2   v27.4s, v14.8h, v6.h[5]    \n"//
                    "smlal    v28.4s, v14.4h, v6.h[6]    \n"// sum6 += (a00-a70) * k60
                    "smlal2   v29.4s, v14.8h, v6.h[6]    \n"//
                    "smlal    v30.4s, v14.4h, v6.h[7]    \n"// sum7 += (a00-a70) * k70
                    "smlal2   v31.4s, v14.8h, v6.h[7]    \n"//
                    // k7
                    "smlal    v16.4s, v15.4h, v7.h[0]    \n"// sum0 += (a07-a77) * k07
                    "smlal2   v17.4s, v15.8h, v7.h[0]    \n"//
                    "smlal    v18.4s, v15.4h, v7.h[1]    \n"// sum1 += (a07-a77) * k17
                    "smlal2   v19.4s, v15.8h, v7.h[1]    \n"//
                    "smlal    v20.4s, v15.4h, v7.h[2]    \n"// sum2 += (a07-a77) * k27
                    "smlal2   v21.4s, v15.8h, v7.h[2]    \n"//
                    "smlal    v22.4s, v15.4h, v7.h[3]    \n"// sum3 += (a07-a77) * k37
                    "smlal2   v23.4s, v15.8h, v7.h[3]    \n"//
                    "smlal    v24.4s, v15.4h, v7.h[4]    \n"// sum4 += (a07-a77) * k47
                    "smlal2   v25.4s, v15.8h, v7.h[4]    \n"//
                    "smlal    v26.4s, v15.4h, v7.h[5]    \n"// sum5 += (a07-a77) * k57
                    "smlal2   v27.4s, v15.8h, v7.h[5]    \n"//
                    "smlal    v28.4s, v15.4h, v7.h[6]    \n"// sum6 += (a07-a77) * k67
                    "smlal2   v29.4s, v15.8h, v7.h[6]    \n"//
                    "smlal    v30.4s, v15.4h, v7.h[7]    \n"// sum7 += (a07-a77) * k77
                    "smlal2   v31.4s, v15.8h, v7.h[7]    \n"//

                    "subs   w4, w4, #1                   \n"
                    "bne    0b                           \n"

                    "1:                                  \n"

                    // remain loop
                    "and    w4, %w20, #7                 \n"// w4 = remain = inch & 7;
                    "cmp    w4, #0                       \n"
                    "beq    3f                           \n"

                    "2:                                  \n"

                    "prfm   pldl1keep, [%9, #128]        \n"
                    "ld1    {v0.8b}, [%9], #8            \n"

                    "prfm   pldl1keep, [%8, #128]        \n"
                    "ld1    {v8.8b}, [%8], #8            \n"

                    "sshll    v0.8h, v0.8b, #0           \n" // k00 - k70
                    "sshll    v8.8h, v8.8b, #0           \n" // a00 - a70

                    // k0
                    "smlal    v16.4s, v8.4h, v0.h[0]     \n"// sum0 += (a00-a70) * k00
                    "smlal2   v17.4s, v8.8h, v0.h[0]     \n"//
                    "smlal    v18.4s, v8.4h, v0.h[1]     \n"// sum1 += (a00-a70) * k10
                    "smlal2   v19.4s, v8.8h, v0.h[1]     \n"//
                    "smlal    v20.4s, v8.4h, v0.h[2]     \n"// sum2 += (a00-a70) * k20
                    "smlal2   v21.4s, v8.8h, v0.h[2]     \n"//
                    "smlal    v22.4s, v8.4h, v0.h[3]     \n"// sum3 += (a00-a70) * k30
                    "smlal2   v23.4s, v8.8h, v0.h[3]     \n"//
                    "smlal    v24.4s, v8.4h, v0.h[4]     \n"// sum4 += (a00-a70) * k40
                    "smlal2   v25.4s, v8.8h, v0.h[4]     \n"//
                    "smlal    v26.4s, v8.4h, v0.h[5]     \n"// sum5 += (a00-a70) * k50
                    "smlal2   v27.4s, v8.8h, v0.h[5]     \n"//
                    "smlal    v28.4s, v8.4h, v0.h[6]     \n"// sum6 += (a00-a70) * k60
                    "smlal2   v29.4s, v8.8h, v0.h[6]     \n"//
                    "smlal    v30.4s, v8.4h, v0.h[7]     \n"// sum7 += (a00-a70) * k70
                    "smlal2   v31.4s, v8.8h, v0.h[7]     \n"//

                    "subs   w4, w4, #1                   \n"

                    "bne    2b                           \n"

                    "3:                                  \n"

                    "st1    {v16.4s, v17.4s}, [%0]       \n"
                    "st1    {v18.4s, v19.4s}, [%1]       \n"
                    "st1    {v20.4s, v21.4s}, [%2]       \n"
                    "st1    {v22.4s, v23.4s}, [%3]       \n"
                    "st1    {v24.4s, v25.4s}, [%4]       \n"
                    "st1    {v26.4s, v27.4s}, [%5]       \n"
                    "st1    {v28.4s, v29.4s}, [%6]       \n"
                    "st1    {v30.4s, v31.4s}, [%7]       \n"
                    
                    : "=r"(output0), // %0
                      "=r"(output1), // %1
                      "=r"(output2), // %2
                      "=r"(output3), // %3
                      "=r"(output4), // %4
                      "=r"(output5), // %5
                      "=r"(output6), // %6
                      "=r"(output7), // %7
                      "=r"(vb),      // %8
                      "=r"(va)       // %9
                    : "0"(output0),
                      "1"(output1),
                      "2"(output2),
                      "3"(output3),
                      "4"(output4),
                      "5"(output5),
                      "6"(output6),
                      "7"(output7),
                      "8"(vb),
                      "9"(va),
                      "r"(L)         // %20 
                    : "cc", "memory", "x4", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"
                );                     
#else                
                int sum0[8] = {0};
                int sum1[8] = {0};
                int sum2[8] = {0};
                int sum3[8] = {0};
                int sum4[8] = {0};
                int sum5[8] = {0};
                int sum6[8] = {0};
                int sum7[8] = {0};

                int k=0;
                for (; k+7<L; k=k+8)
                {
                    for (int n=0; n<8; n++)
                    {
                        sum0[n] += (int)va[0] * vb[n];
                        sum1[n] += (int)va[1] * vb[n];
                        sum2[n] += (int)va[2] * vb[n];
                        sum3[n] += (int)va[3] * vb[n];
                        sum4[n] += (int)va[4] * vb[n];
                        sum5[n] += (int)va[5] * vb[n];
                        sum6[n] += (int)va[6] * vb[n];
                        sum7[n] += (int)va[7] * vb[n];
                        va += 8;

                        sum0[n] += (int)va[0] * vb[n+8];
                        sum1[n] += (int)va[1] * vb[n+8];
                        sum2[n] += (int)va[2] * vb[n+8];
                        sum3[n] += (int)va[3] * vb[n+8];
                        sum4[n] += (int)va[4] * vb[n+8];
                        sum5[n] += (int)va[5] * vb[n+8];
                        sum6[n] += (int)va[6] * vb[n+8];
                        sum7[n] += (int)va[7] * vb[n+8];
                        va += 8;

                        sum0[n] += (int)va[0] * vb[n+16];
                        sum1[n] += (int)va[1] * vb[n+16];
                        sum2[n] += (int)va[2] * vb[n+16];
                        sum3[n] += (int)va[3] * vb[n+16];
                        sum4[n] += (int)va[4] * vb[n+16];
                        sum5[n] += (int)va[5] * vb[n+16];
                        sum6[n] += (int)va[6] * vb[n+16];
                        sum7[n] += (int)va[7] * vb[n+16];
                        va += 8;

                        sum0[n] += (int)va[0] * vb[n+24];
                        sum1[n] += (int)va[1] * vb[n+24];
                        sum2[n] += (int)va[2] * vb[n+24];
                        sum3[n] += (int)va[3] * vb[n+24];
                        sum4[n] += (int)va[4] * vb[n+24];
                        sum5[n] += (int)va[5] * vb[n+24];
                        sum6[n] += (int)va[6] * vb[n+24];
                        sum7[n] += (int)va[7] * vb[n+24];
                        va += 8;

                        sum0[n] += (int)va[0] * vb[n+32];
                        sum1[n] += (int)va[1] * vb[n+32];
                        sum2[n] += (int)va[2] * vb[n+32];
                        sum3[n] += (int)va[3] * vb[n+32];
                        sum4[n] += (int)va[4] * vb[n+32];
                        sum5[n] += (int)va[5] * vb[n+32];
                        sum6[n] += (int)va[6] * vb[n+32];
                        sum7[n] += (int)va[7] * vb[n+32];
                        va += 8;

                        sum0[n] += (int)va[0] * vb[n+40];
                        sum1[n] += (int)va[1] * vb[n+40];
                        sum2[n] += (int)va[2] * vb[n+40];
                        sum3[n] += (int)va[3] * vb[n+40];
                        sum4[n] += (int)va[4] * vb[n+40];
                        sum5[n] += (int)va[5] * vb[n+40];
                        sum6[n] += (int)va[6] * vb[n+40];
                        sum7[n] += (int)va[7] * vb[n+40];
                        va += 8;

                        sum0[n] += (int)va[0] * vb[n+48];
                        sum1[n] += (int)va[1] * vb[n+48];
                        sum2[n] += (int)va[2] * vb[n+48];
                        sum3[n] += (int)va[3] * vb[n+48];
                        sum4[n] += (int)va[4] * vb[n+48];
                        sum5[n] += (int)va[5] * vb[n+48];
                        sum6[n] += (int)va[6] * vb[n+48];
                        sum7[n] += (int)va[7] * vb[n+48];
                        va += 8;

                        sum0[n] += (int)va[0] * vb[n+56];
                        sum1[n] += (int)va[1] * vb[n+56];
                        sum2[n] += (int)va[2] * vb[n+56];
                        sum3[n] += (int)va[3] * vb[n+56];
                        sum4[n] += (int)va[4] * vb[n+56];
                        sum5[n] += (int)va[5] * vb[n+56];
                        sum6[n] += (int)va[6] * vb[n+56];
                        sum7[n] += (int)va[7] * vb[n+56];                        
                        va -= 56;
                    }

                    va += 64;
                    vb += 64;
                }

                for (; k<L; k++)
                {
                    for (int n=0; n<8; n++)
                    {
                        sum0[n] += (int)va[0] * vb[n];
                        sum1[n] += (int)va[1] * vb[n];
                        sum2[n] += (int)va[2] * vb[n];
                        sum3[n] += (int)va[3] * vb[n];
                        sum4[n] += (int)va[4] * vb[n];
                        sum5[n] += (int)va[5] * vb[n];
                        sum6[n] += (int)va[6] * vb[n];
                        sum7[n] += (int)va[7] * vb[n];
                    }
                    
                    va += 8;
                    vb += 8;
                }

                for (int n=0; n<8; n++)
                {
                    output0[n] = sum0[n];
                    output1[n] = sum1[n];
                    output2[n] = sum2[n];
                    output3[n] = sum3[n];
                    output4[n] = sum4[n];
                    output5[n] = sum5[n];
                    output6[n] = sum6[n];
                    output7[n] = sum7[n];
                }
#endif
                output0 += 8;
                output1 += 8;
                output2 += 8;
                output3 += 8;
                output4 += 8;
                output5 += 8;
                output6 += 8;
                output7 += 8;
            }

            for (; j<N; j++)
            {
                signed char* vb = bottom_tm.channel(j/8 + j%8);
                signed char* va = kernel_tm.channel(i/8);

                int sum0 = 0;
                int sum1 = 0;
                int sum2 = 0;
                int sum3 = 0;
                int sum4 = 0;
                int sum5 = 0;
                int sum6 = 0;
                int sum7 = 0;

                for (int k=0; k<L; k++)
                {
                    sum0 += (int)va[0] * vb[0];
                    sum1 += (int)va[1] * vb[0];
                    sum2 += (int)va[2] * vb[0];
                    sum3 += (int)va[3] * vb[0];
                    sum4 += (int)va[4] * vb[0];
                    sum5 += (int)va[5] * vb[0];
                    sum6 += (int)va[6] * vb[0];
                    sum7 += (int)va[7] * vb[0];

                    va += 8;
                    vb += 1;
                }
                
                output0[0] = sum0;
                output1[0] = sum1;
                output2[0] = sum2;
                output3[0] = sum3;
                output4[0] = sum4;
                output5[0] = sum5;
                output6[0] = sum6;
                output7[0] = sum7;               

                output0++;
                output1++;
                output2++;
                output3++;
                output4++;
                output5++;
                output6++;
                output7++;
            }
        }
#endif // __aarch64__

        nn_outch = (outch - remain_outch_start) >> 2;

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int pp=0; pp<nn_outch; pp++)
        {
            int i = remain_outch_start + pp * 4;

            int* output0 = top_blob.channel(i);
            int* output1 = top_blob.channel(i+1);
            int* output2 = top_blob.channel(i+2);
            int* output3 = top_blob.channel(i+3);

            int j=0;
            for (; j+7<N; j=j+8)
            {
                signed char* vb = bottom_tm.channel(j/8);
#if __aarch64__
                signed char* va = kernel_tm.channel(i/8 + (i%8)/4);
#else                
                signed char* va = kernel_tm.channel(i/4);
#endif

#if __ARM_NEON
#if __aarch64__
                int32x4_t _sum0 = vdupq_n_s32(0);
                int32x4_t _sum0n = vdupq_n_s32(0);
                int32x4_t _sum1 = vdupq_n_s32(0);
                int32x4_t _sum1n = vdupq_n_s32(0);
                int32x4_t _sum2 = vdupq_n_s32(0);
                int32x4_t _sum2n = vdupq_n_s32(0);
                int32x4_t _sum3 = vdupq_n_s32(0);
                int32x4_t _sum3n = vdupq_n_s32(0);

                int k=0;
                for (; k+7<L; k=k+8)
                {
                    int8x8_t _vacc0_s8 = vld1_s8(va);
                    int8x8_t _vacc1_s8 = vld1_s8(va+8);
                    int8x8_t _vacc2_s8 = vld1_s8(va+16);
                    int8x8_t _vacc3_s8 = vld1_s8(va+24);
                    int16x8_t _vacc0 = vmovl_s8(_vacc0_s8);
                    int16x8_t _vacc1 = vmovl_s8(_vacc1_s8);
                    int16x8_t _vacc2 = vmovl_s8(_vacc2_s8);
                    int16x8_t _vacc3 = vmovl_s8(_vacc3_s8);

                    // k=0
                    int8x8_t _vb_s8 = vld1_s8(vb);
                    int16x8_t _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_low_s16(_vacc0), 0);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_low_s16(_vacc0), 0);
                    _sum1 = vmlal_lane_s16(_sum1, vget_low_s16(_vb), vget_low_s16(_vacc0), 1);
                    _sum1n = vmlal_lane_s16(_sum1n, vget_high_s16(_vb), vget_low_s16(_vacc0), 1);
                    _sum2 = vmlal_lane_s16(_sum2, vget_low_s16(_vb), vget_low_s16(_vacc0), 2);
                    _sum2n = vmlal_lane_s16(_sum2n, vget_high_s16(_vb), vget_low_s16(_vacc0), 2);
                    _sum3 = vmlal_lane_s16(_sum3, vget_low_s16(_vb), vget_low_s16(_vacc0), 3);
                    _sum3n = vmlal_lane_s16(_sum3n, vget_high_s16(_vb), vget_low_s16(_vacc0), 3);

                    // k=1
                    _vb_s8 = vld1_s8(vb+8);
                    _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_high_s16(_vacc0), 0);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_high_s16(_vacc0), 0);
                    _sum1 = vmlal_lane_s16(_sum1, vget_low_s16(_vb), vget_high_s16(_vacc0), 1);
                    _sum1n = vmlal_lane_s16(_sum1n, vget_high_s16(_vb), vget_high_s16(_vacc0), 1);
                    _sum2 = vmlal_lane_s16(_sum2, vget_low_s16(_vb), vget_high_s16(_vacc0), 2);
                    _sum2n = vmlal_lane_s16(_sum2n, vget_high_s16(_vb), vget_high_s16(_vacc0), 2);
                    _sum3 = vmlal_lane_s16(_sum3, vget_low_s16(_vb), vget_high_s16(_vacc0), 3);
                    _sum3n = vmlal_lane_s16(_sum3n, vget_high_s16(_vb), vget_high_s16(_vacc0), 3);

                    // k=2
                    _vb_s8 = vld1_s8(vb+16);
                    _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_low_s16(_vacc1), 0);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_low_s16(_vacc1), 0);
                    _sum1 = vmlal_lane_s16(_sum1, vget_low_s16(_vb), vget_low_s16(_vacc1), 1);
                    _sum1n = vmlal_lane_s16(_sum1n, vget_high_s16(_vb), vget_low_s16(_vacc1), 1);
                    _sum2 = vmlal_lane_s16(_sum2, vget_low_s16(_vb), vget_low_s16(_vacc1), 2);
                    _sum2n = vmlal_lane_s16(_sum2n, vget_high_s16(_vb), vget_low_s16(_vacc1), 2);
                    _sum3 = vmlal_lane_s16(_sum3, vget_low_s16(_vb), vget_low_s16(_vacc1), 3);
                    _sum3n = vmlal_lane_s16(_sum3n, vget_high_s16(_vb), vget_low_s16(_vacc1), 3);

                    // k=3
                    _vb_s8 = vld1_s8(vb+24);
                    _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_high_s16(_vacc1), 0);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_high_s16(_vacc1), 0);
                    _sum1 = vmlal_lane_s16(_sum1, vget_low_s16(_vb), vget_high_s16(_vacc1), 1);
                    _sum1n = vmlal_lane_s16(_sum1n, vget_high_s16(_vb), vget_high_s16(_vacc1), 1);
                    _sum2 = vmlal_lane_s16(_sum2, vget_low_s16(_vb), vget_high_s16(_vacc1), 2);
                    _sum2n = vmlal_lane_s16(_sum2n, vget_high_s16(_vb), vget_high_s16(_vacc1), 2);
                    _sum3 = vmlal_lane_s16(_sum3, vget_low_s16(_vb), vget_high_s16(_vacc1), 3);
                    _sum3n = vmlal_lane_s16(_sum3n, vget_high_s16(_vb), vget_high_s16(_vacc1), 3);

                    // k=4
                    _vb_s8 = vld1_s8(vb+32);
                    _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_low_s16(_vacc2), 0);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_low_s16(_vacc2), 0);
                    _sum1 = vmlal_lane_s16(_sum1, vget_low_s16(_vb), vget_low_s16(_vacc2), 1);
                    _sum1n = vmlal_lane_s16(_sum1n, vget_high_s16(_vb), vget_low_s16(_vacc2), 1);
                    _sum2 = vmlal_lane_s16(_sum2, vget_low_s16(_vb), vget_low_s16(_vacc2), 2);
                    _sum2n = vmlal_lane_s16(_sum2n, vget_high_s16(_vb), vget_low_s16(_vacc2), 2);
                    _sum3 = vmlal_lane_s16(_sum3, vget_low_s16(_vb), vget_low_s16(_vacc2), 3);
                    _sum3n = vmlal_lane_s16(_sum3n, vget_high_s16(_vb), vget_low_s16(_vacc2), 3);

                    // k=5
                    _vb_s8 = vld1_s8(vb+40);
                    _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_high_s16(_vacc2), 0);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_high_s16(_vacc2), 0);
                    _sum1 = vmlal_lane_s16(_sum1, vget_low_s16(_vb), vget_high_s16(_vacc2), 1);
                    _sum1n = vmlal_lane_s16(_sum1n, vget_high_s16(_vb), vget_high_s16(_vacc2), 1);
                    _sum2 = vmlal_lane_s16(_sum2, vget_low_s16(_vb), vget_high_s16(_vacc2), 2);
                    _sum2n = vmlal_lane_s16(_sum2n, vget_high_s16(_vb), vget_high_s16(_vacc2), 2);
                    _sum3 = vmlal_lane_s16(_sum3, vget_low_s16(_vb), vget_high_s16(_vacc2), 3);
                    _sum3n = vmlal_lane_s16(_sum3n, vget_high_s16(_vb), vget_high_s16(_vacc2), 3);

                    // k=6
                    _vb_s8 = vld1_s8(vb+48);
                    _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_low_s16(_vacc3), 0);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_low_s16(_vacc3), 0);
                    _sum1 = vmlal_lane_s16(_sum1, vget_low_s16(_vb), vget_low_s16(_vacc3), 1);
                    _sum1n = vmlal_lane_s16(_sum1n, vget_high_s16(_vb), vget_low_s16(_vacc3), 1);
                    _sum2 = vmlal_lane_s16(_sum2, vget_low_s16(_vb), vget_low_s16(_vacc3), 2);
                    _sum2n = vmlal_lane_s16(_sum2n, vget_high_s16(_vb), vget_low_s16(_vacc3), 2);
                    _sum3 = vmlal_lane_s16(_sum3, vget_low_s16(_vb), vget_low_s16(_vacc3), 3);
                    _sum3n = vmlal_lane_s16(_sum3n, vget_high_s16(_vb), vget_low_s16(_vacc3), 3);

                    // k=7
                    _vb_s8 = vld1_s8(vb+56);
                    _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_high_s16(_vacc3), 0);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_high_s16(_vacc3), 0);
                    _sum1 = vmlal_lane_s16(_sum1, vget_low_s16(_vb), vget_high_s16(_vacc3), 1);
                    _sum1n = vmlal_lane_s16(_sum1n, vget_high_s16(_vb), vget_high_s16(_vacc3), 1);
                    _sum2 = vmlal_lane_s16(_sum2, vget_low_s16(_vb), vget_high_s16(_vacc3), 2);
                    _sum2n = vmlal_lane_s16(_sum2n, vget_high_s16(_vb), vget_high_s16(_vacc3), 2);
                    _sum3 = vmlal_lane_s16(_sum3, vget_low_s16(_vb), vget_high_s16(_vacc3), 3);
                    _sum3n = vmlal_lane_s16(_sum3n, vget_high_s16(_vb), vget_high_s16(_vacc3), 3);

                    va += 32;
                    vb += 64;
                }

                for (; k<L; k++)
                {
                    int8x8_t _vacc0_s8 = vld1_s8(va);
                    int16x8_t _vacc0 = vmovl_s8(_vacc0_s8);

                    // k=0
                    int8x8_t _vb_s8 = vld1_s8(vb);
                    int16x8_t _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_low_s16(_vacc0), 0);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_low_s16(_vacc0), 0);
                    _sum1 = vmlal_lane_s16(_sum1, vget_low_s16(_vb), vget_low_s16(_vacc0), 1);
                    _sum1n = vmlal_lane_s16(_sum1n, vget_high_s16(_vb), vget_low_s16(_vacc0), 1);
                    _sum2 = vmlal_lane_s16(_sum2, vget_low_s16(_vb), vget_low_s16(_vacc0), 2);
                    _sum2n = vmlal_lane_s16(_sum2n, vget_high_s16(_vb), vget_low_s16(_vacc0), 2);
                    _sum3 = vmlal_lane_s16(_sum3, vget_low_s16(_vb), vget_low_s16(_vacc0), 3);
                    _sum3n = vmlal_lane_s16(_sum3n, vget_high_s16(_vb), vget_low_s16(_vacc0), 3);

                    va += 4;
                    vb += 8;
                }

                vst1q_s32(output0, _sum0);
                vst1q_s32(output0+4, _sum0n);
                vst1q_s32(output1, _sum1);
                vst1q_s32(output1+4, _sum1n);
                vst1q_s32(output2, _sum2);
                vst1q_s32(output2+4, _sum2n);
                vst1q_s32(output3, _sum3);
                vst1q_s32(output3+4, _sum3n); 
#else
                asm volatile(
                    // K loop
                    "vmov.s32    q8, #0             \n"
                    "vmov.s32    q9, #0             \n"
                    "vmov.s32    q10, #0            \n"
                    "vmov.s32    q11, #0            \n"
                    "vmov.s32    q12, #0            \n"
                    "vmov.s32    q13, #0            \n"
                    "vmov.s32    q14, #0            \n"
                    "vmov.s32    q15, #0            \n"

                    "lsr         r4, %12, #3        \n"// r4 = nn = L >> 3
                    "cmp         r4, #0             \n"
                    "beq         1f                 \n"
                    
                    "0:                             \n"// for(; nn != 0; nn--)
                    "pld         [%4, #128]         \n"
                    "vld1.s8     {d8-d11}, [%4]!    \n"// tmpr a00-a07,a10-a17,a20-a27,a30-a37    a(inch)(data)
                    "vmovl.s8    q7, d11            \n"// a30-a37
                    "vmovl.s8    q6, d10            \n"// a20-a27                    
                    "vmovl.s8    q5, d9             \n"// a10-a17
                    "vmovl.s8    q4, d8             \n"// a00-a07

                    "pld         [%5, #128]         \n"
                    "vld1.s8     {d0-d3}, [%5]!     \n"// kptr k00-k30,k01-k31, k02-k32,k03-k33, k04-k34,k05-k35, k06-k36,k07-k37    k(outch)(inch)
                    "vmovl.s8    q3, d3             \n"// k06-k36,k07-k37
                    "vmovl.s8    q2, d2             \n"// k04-k34,k05-k35
                    "vmovl.s8    q1, d1             \n"// k02-k32,k03-k33
                    "vmovl.s8    q0, d0             \n"// k00-k30,k01-k31

                    "vmlal.s16   q8, d8, d0[0]      \n"// sum0 = (a00-a07) * k00
                    "vmlal.s16   q9, d9, d0[0]      \n"
                    "vmlal.s16   q10, d8, d0[1]     \n"// sum1 = (a00-a07) * k10
                    "vmlal.s16   q11, d9, d0[1]     \n"
                    "vmlal.s16   q12, d8, d0[2]     \n"// sum2 = (a00-a07) * k20
                    "vmlal.s16   q13, d9, d0[2]     \n"
                    "vmlal.s16   q14, d8, d0[3]     \n"// sum3 = (a00-a07) * k30
                    "vmlal.s16   q15, d9, d0[3]     \n"                  

                    "vmlal.s16   q8, d10, d1[0]     \n"// sum0 += (a10-a17) * k01
                    "vmlal.s16   q9, d11, d1[0]     \n"
                    "vmlal.s16   q10, d10, d1[1]    \n"// sum1 += (a10-a17) * k11
                    "vmlal.s16   q11, d11, d1[1]    \n"
                    "vmlal.s16   q12, d10, d1[2]    \n"// sum2 += (a10-a17) * k21
                    "vmlal.s16   q13, d11, d1[2]    \n"
                    "vmlal.s16   q14, d10, d1[3]    \n"// sum3 += (a10-a17) * k31
                    "vmlal.s16   q15, d11, d1[3]    \n"

                    "pld         [%4, #128]         \n"
                    "vld1.s8     {d8-d9}, [%4]!     \n"// tmpr a00-a07,a10-a17,a20-a27,a30-a37    a(inch)(data)
                    "vmovl.s8    q5, d9             \n"// a10-a17
                    "vmovl.s8    q4, d8             \n"// a00-a07

                    "vmlal.s16   q8, d12, d2[0]     \n"// sum0 += (a20-a27) * k02
                    "vmlal.s16   q9, d13, d2[0]     \n"
                    "vmlal.s16   q10, d12, d2[1]    \n"// sum1 += (a20-a27) * k12
                    "vmlal.s16   q11, d13, d2[1]    \n"
                    "vmlal.s16   q12, d12, d2[2]    \n"// sum2 += (a20-a27) * k22
                    "vmlal.s16   q13, d13, d2[2]    \n"
                    "vmlal.s16   q14, d12, d2[3]    \n"// sum3 += (a20-a27) * k32
                    "vmlal.s16   q15, d13, d2[3]    \n"                      

                    "vmlal.s16   q8, d14, d3[0]     \n"// sum0 += (a30-a37) * k03
                    "vmlal.s16   q9, d15, d3[0]     \n"
                    "vmlal.s16   q10, d14, d3[1]    \n"// sum1 += (a30-a37) * k13
                    "vmlal.s16   q11, d15, d3[1]    \n"
                    "vmlal.s16   q12, d14, d3[2]    \n"// sum2 += (a30-a37) * k23
                    "vmlal.s16   q13, d15, d3[2]    \n"
                    "vmlal.s16   q14, d14, d3[3]    \n"// sum3 += (a30-a37) * k33
                    "vmlal.s16   q15, d15, d3[3]    \n"

                    "pld         [%4, #128]         \n"
                    "vld1.s8     {d0-d1}, [%4]!     \n"// tmpr a00-a07,a10-a17,a20-a27,a30-a37    a(inch)(data)
                    "vmovl.s8    q1, d1             \n"// a10-a17
                    "vmovl.s8    q0, d0             \n"// a00-a07

                    "vmlal.s16   q8, d8, d4[0]      \n"// sum0 += (a40-a47) * k04
                    "vmlal.s16   q9, d9, d4[0]      \n"
                    "vmlal.s16   q10, d8, d4[1]     \n"// sum1 += (a40-a47) * k14
                    "vmlal.s16   q11, d9, d4[1]     \n"
                    "vmlal.s16   q12, d8, d4[2]     \n"// sum2 += (a40-a47) * k24
                    "vmlal.s16   q13, d9, d4[2]     \n"
                    "vmlal.s16   q14, d8, d4[3]     \n"// sum3 += (a40-a47) * k34
                    "vmlal.s16   q15, d9, d4[3]     \n"                     

                    "vmlal.s16   q8, d10, d5[0]     \n"// sum0 += (a50-a57) * k05
                    "vmlal.s16   q9, d11, d5[0]     \n"
                    "vmlal.s16   q10, d10, d5[1]    \n"// sum1 += (a50-a57) * k15
                    "vmlal.s16   q11, d11, d5[1]    \n"
                    "vmlal.s16   q12, d10, d5[2]    \n"// sum2 += (a50-a57) * k25
                    "vmlal.s16   q13, d11, d5[2]    \n"
                    "vmlal.s16   q14, d10, d5[3]    \n"// sum3 += (a50-a57) * k35
                    "vmlal.s16   q15, d11, d5[3]    \n"                  

                    "vmlal.s16   q8, d0, d6[0]      \n"// sum0 += (a60-a67) * k06
                    "vmlal.s16   q9, d1, d6[0]      \n"
                    "vmlal.s16   q10, d0, d6[1]     \n"// sum1 += (a60-a67) * k16
                    "vmlal.s16   q11, d1, d6[1]     \n"
                    "vmlal.s16   q12, d0, d6[2]     \n"// sum2 += (a60-a67) * k26
                    "vmlal.s16   q13, d1, d6[2]     \n"
                    "vmlal.s16   q14, d0, d6[3]     \n"// sum3 += (a60-a67) * k36
                    "vmlal.s16   q15, d1, d6[3]     \n"                      

                    "vmlal.s16   q8, d2, d7[0]      \n"// sum0 += (a70-a77) * k07
                    "vmlal.s16   q9, d3, d7[0]      \n"
                    "vmlal.s16   q10, d2, d7[1]     \n"// sum1 += (a70-a77) * k17
                    "vmlal.s16   q11, d3, d7[1]     \n"
                    "vmlal.s16   q12, d2, d7[2]     \n"// sum2 += (a70-a77) * k27
                    "vmlal.s16   q13, d3, d7[2]     \n"
                    "vmlal.s16   q14, d2, d7[3]     \n"// sum3 += (a70-a77) * k37
                    "vmlal.s16   q15, d3, d7[3]     \n"                                        

                    "subs        r4, r4, #1         \n"
                    "bne         0b                 \n"// end for

                    "1:                             \n"
                    // remain loop
                    "and         r4, %12, #7        \n"// r4 = remain = inch & 7
                    "cmp         r4, #0             \n"
                    "beq         3f                 \n"

                    "2:                             \n"// for(; remain != 0; remain--)
                    "vld1.s8     {d2}, [%4]!        \n"// tmpr a00-a70    a(inch)(data)
                    "vld1.s8     {d0}, [%5]         \n"// kptr k00-k30    k(outch)(inch)
                    "vmovl.s8    q1, d2             \n"
                    "vmovl.s8    q0, d0             \n"
                    "add         %5, #4             \n"

                    "vmlal.s16   q8, d2, d0[0]      \n"// sum0 += (a00-a70) * k00
                    "vmlal.s16   q9, d3, d0[0]      \n"
                    "vmlal.s16   q10, d2, d0[1]     \n"// sum1 += (a00-a70) * k10
                    "vmlal.s16   q11, d3, d0[1]     \n"
                    "vmlal.s16   q12, d2, d0[2]     \n"// sum2 += (a00-a70) * k20
                    "vmlal.s16   q13, d3, d0[2]     \n"
                    "vmlal.s16   q14, d2, d0[3]     \n"// sum3 += (a00-a70) * k30
                    "vmlal.s16   q15, d3, d0[3]     \n"    

                    "subs        r4, r4, #1         \n"
                    "bne         2b                 \n"

                    "3:                             \n"// store the result to memory
                    "vst1.s32    {d16-d19}, [%0]    \n"
                    "vst1.s32    {d20-d23}, [%1]    \n"
                    "vst1.s32    {d24-d27}, [%2]    \n"
                    "vst1.s32    {d28-d31}, [%3]    \n"

                    : "=r"(output0), // %0
                      "=r"(output1), // %1
                      "=r"(output2), // %2
                      "=r"(output3), // %3
                      "=r"(vb),      // %4
                      "=r"(va)       // %5
                    : "0"(output0),
                      "1"(output1),
                      "2"(output2),
                      "3"(output3),
                      "4"(vb),
                      "5"(va),
                      "r"(L)         // %12  
                    : "cc", "memory", "r4", "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15"                    
                );
#endif // __aarch64__                                
#else
                int sum0[8] = {0};
                int sum1[8] = {0};
                int sum2[8] = {0};
                int sum3[8] = {0};
               
                int k=0;
                for (; k+7<L; k=k+8)
                {
                    for (int n=0; n<8; n++)
                    {
                        sum0[n] += (int)va[0] * vb[n];
                        sum1[n] += (int)va[1] * vb[n];
                        sum2[n] += (int)va[2] * vb[n];
                        sum3[n] += (int)va[3] * vb[n];
                        va += 4;

                        sum0[n] += (int)va[0] * vb[n+8];
                        sum1[n] += (int)va[1] * vb[n+8];
                        sum2[n] += (int)va[2] * vb[n+8];
                        sum3[n] += (int)va[3] * vb[n+8];
                        va += 4;

                        sum0[n] += (int)va[0] * vb[n+16];
                        sum1[n] += (int)va[1] * vb[n+16];
                        sum2[n] += (int)va[2] * vb[n+16];
                        sum3[n] += (int)va[3] * vb[n+16];
                        va += 4;

                        sum0[n] += (int)va[0] * vb[n+24];
                        sum1[n] += (int)va[1] * vb[n+24];
                        sum2[n] += (int)va[2] * vb[n+24];
                        sum3[n] += (int)va[3] * vb[n+24];
                        va += 4;

                        sum0[n] += (int)va[0] * vb[n+32];
                        sum1[n] += (int)va[1] * vb[n+32];
                        sum2[n] += (int)va[2] * vb[n+32];
                        sum3[n] += (int)va[3] * vb[n+32];
                        va += 4;

                        sum0[n] += (int)va[0] * vb[n+40];
                        sum1[n] += (int)va[1] * vb[n+40];
                        sum2[n] += (int)va[2] * vb[n+40];
                        sum3[n] += (int)va[3] * vb[n+40];
                        va += 4;

                        sum0[n] += (int)va[0] * vb[n+48];
                        sum1[n] += (int)va[1] * vb[n+48];
                        sum2[n] += (int)va[2] * vb[n+48];
                        sum3[n] += (int)va[3] * vb[n+48];
                        va += 4;

                        sum0[n] += (int)va[0] * vb[n+56];
                        sum1[n] += (int)va[1] * vb[n+56];
                        sum2[n] += (int)va[2] * vb[n+56];
                        sum3[n] += (int)va[3] * vb[n+56];
                        va -= 28;
                    }

                    va += 32;
                    vb += 64;
                }

                for (; k<L; k++)
                {
                    for (int n=0; n<8; n++)
                    {
                        sum0[n] += (int)va[0] * vb[n];
                        sum1[n] += (int)va[1] * vb[n];
                        sum2[n] += (int)va[2] * vb[n];
                        sum3[n] += (int)va[3] * vb[n];
                    }
                    
                    va += 4;
                    vb += 8;
                }

                for (int n=0; n<8; n++)
                {
                    output0[n] = sum0[n];
                    output1[n] = sum1[n];
                    output2[n] = sum2[n];
                    output3[n] = sum3[n];
                }
#endif // __ARM_NEON
                output0 += 8;
                output1 += 8;
                output2 += 8;
                output3 += 8;
            }

            for (; j<N; j++)
            {                
                signed char* vb = bottom_tm.channel(j/8 + j%8);
#if __aarch64__
                signed char* va = kernel_tm.channel(i/8 + (i%8)/4);
#else                
                signed char* va = kernel_tm.channel(i/4);
#endif

#if __ARM_NEON
#if __aarch64__
                int sum0 = 0;
                int sum1 = 0;
                int sum2 = 0;
                int sum3 = 0;

                for (int k=0; k<L; k++)
                {
                    sum0 += (int)va[0] * vb[0];
                    sum1 += (int)va[1] * vb[0];
                    sum2 += (int)va[2] * vb[0];
                    sum3 += (int)va[3] * vb[0];

                    va += 4;
                    vb += 1;
                }
                
                output0[0] = sum0;
                output1[0] = sum1;
                output2[0] = sum2;
                output3[0] = sum3;
#else
                asm volatile(
                    // inch loop
                    "veor        q6, q6, q6        \n"
                    "veor        q7, q7, q7        \n"
                    "veor        q8, q8, q8        \n"
                    "veor        q9, q9, q9        \n"
                    "veor        q10, q10, q10     \n"
                    "veor        q11, q11, q11     \n"
                    "veor        q12, q12, q12     \n"
                    "veor        q13, q13, q13     \n"                    
                    "vmov.s32    q14, #0           \n"

                    "lsr         r4, %12, #3       \n"// r4 = nn = L >> 2
                    "cmp         r4, #0            \n"
                    "beq         1f                \n"
                    
                    "0:                            \n"// for(; nn != 0; nn--)
                    "pld         [%4, #128]        \n"
                    "vld1.s8     {d0}, [%4]!       \n"// tmpr a00,a10,a20,a30    a(inch)(data)
                    "vmovl.s8    q0, d0            \n"// a00-a07

                    "pld         [%5, #128]        \n"
                    "vld1.s8     {d2-d5}, [%5]!    \n"// kptr k00-k30,k01-k31, k02-k32,k03-k33, k04-k34,k05-k35, k06-k36,k07-k37    k(outch)(inch)
                    "vmovl.s8    q4, d5            \n"// k06-k36,k07-k37
                    "vmovl.s8    q3, d4            \n"// k04-k34,k05-k35
                    "vmovl.s8    q2, d3            \n"// k02-k32,k03-k33
                    "vmovl.s8    q1, d2            \n"// k00-k30,k01-k31

                    "vmlal.s16   q6, d2, d0[0]     \n"// (k00-k30) * a00
                    "vmlal.s16   q7, d3, d0[1]     \n"// (k01-k31) * a01
                    "vmlal.s16   q8, d4, d0[2]     \n"// (k02-k32) * a02
                    "vmlal.s16   q9, d5, d0[3]     \n"// (k03-k33) * a03
                    "vmlal.s16   q10, d6, d1[0]    \n"// (k04-k34) * a04
                    "vmlal.s16   q11, d7, d1[1]    \n"// (k05-k35) * a05
                    "vmlal.s16   q12, d8, d1[2]    \n"// (k06-k36) * a06
                    "vmlal.s16   q13, d9, d1[3]    \n"// (k07-k37) * a07                    

                    "subs        r4, r4, #1        \n"
                    "bne         0b                \n"// end for

                    "vadd.s32    q6, q6, q7        \n"
                    "vadd.s32    q9, q9, q8        \n"
                    "vadd.s32    q11, q11, q10     \n"
                    "vadd.s32    q13, q13, q12     \n"

                    "vadd.s32    q9, q9, q6        \n"
                    "vadd.s32    q13, q13, q11     \n"
                    "vadd.s32    q14, q13, q9      \n"
    
                    "1:                            \n"
                    // remain loop
                    "and         r4, %12, #7       \n"// r4 = remain = inch & 3
                    "cmp         r4, #0            \n"
                    "beq         3f                \n"

                    "2:                            \n"// for(; remain != 0; remain--)
                    "vld1.s8     {d2}, [%4]        \n"// tmpr a00        a(inch)(data)
                    "vld1.s8     {d0}, [%5]        \n"// kptr k00-k30    k(outch)(inch)
                    "vmovl.s8    q1, d2            \n"
                    "vmovl.s8    q0, d0            \n"
                    "add         %4, #1            \n"
                    "add         %5, #4            \n"

                    "vmlal.s16   q14, d0, d2[0]    \n"

                    "subs        r4, r4, #1        \n"
                    "bne         2b                \n"

                    "3:                            \n"// store the result to memory
                    "vst1.s32    {d28[0]}, [%0]    \n"
                    "vst1.s32    {d28[1]}, [%1]    \n"
                    "vst1.s32    {d29[0]}, [%2]    \n"
                    "vst1.s32    {d29[1]}, [%3]    \n"

                    : "=r"(output0), // %0
                      "=r"(output1), // %1
                      "=r"(output2), // %2
                      "=r"(output3), // %3
                      "=r"(vb),      // %4
                      "=r"(va)       // %5
                    : "0"(output0),
                      "1"(output1),
                      "2"(output2),
                      "3"(output3),
                      "4"(vb),
                      "5"(va),
                      "r"(L)         // %12  
                    : "cc", "memory", "r4", "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8", "q9", "q10", "q11", "q12", "q13", "q14"
                );
#endif // __aarch64__                            
#else
                int sum0 = 0;
                int sum1 = 0;
                int sum2 = 0;
                int sum3 = 0;

                for (int k=0; k<L; k++)
                {
                    sum0 += (int)va[0] * vb[0];
                    sum1 += (int)va[1] * vb[0];
                    sum2 += (int)va[2] * vb[0];
                    sum3 += (int)va[3] * vb[0];

                    va += 4;
                    vb += 1;
                }
                
                output0[0] = sum0;
                output1[0] = sum1;
                output2[0] = sum2;
                output3[0] = sum3;
#endif // __ARM_NEON
                output0++;
                output1++;
                output2++;
                output3++;
            }
        }

        remain_outch_start += nn_outch << 2;

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int i=remain_outch_start; i<outch; i++)
        {
            int* output = top_blob.channel(i);

            int j=0;
            for (; j+7<N; j=j+8)
            {
                signed char* vb = bottom_tm.channel(j/8);
#if __aarch64__
                signed char* va = kernel_tm.channel(i/8 + (i%8)/4 + i%4);
#else                
                signed char* va = kernel_tm.channel(i/4 + i%4);
#endif

#if __ARM_NEON
#if __aarch64__
                int32x4_t _sum0 = vdupq_n_s32(0);
                int32x4_t _sum0n = vdupq_n_s32(0);

                int k=0;
                for (; k+7<L; k=k+8)
                {
                    int8x8_t _vacc0_s8 = vld1_s8(va);
                    int16x8_t _vacc0 = vmovl_s8(_vacc0_s8);

                    // k=0
                    int8x8_t _vb_s8 = vld1_s8(vb);
                    int16x8_t _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_low_s16(_vacc0), 0);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_low_s16(_vacc0), 0);

                    // k=1
                    _vb_s8 = vld1_s8(vb+8);
                    _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_low_s16(_vacc0), 1);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_low_s16(_vacc0), 1);

                    // k=2
                    _vb_s8 = vld1_s8(vb+16);
                    _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_low_s16(_vacc0), 2);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_low_s16(_vacc0), 2);

                    // k=3
                    _vb_s8 = vld1_s8(vb+24);
                    _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_low_s16(_vacc0), 3);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_low_s16(_vacc0), 3);

                    // k=4
                    _vb_s8 = vld1_s8(vb+32);
                    _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_high_s16(_vacc0), 0);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_high_s16(_vacc0), 0);

                    // k=5
                    _vb_s8 = vld1_s8(vb+40);
                    _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_high_s16(_vacc0), 1);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_high_s16(_vacc0), 1);

                    // k=6
                    _vb_s8 = vld1_s8(vb+48);
                    _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_high_s16(_vacc0), 2);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_high_s16(_vacc0), 2);

                    // k=7
                    _vb_s8 = vld1_s8(vb+56);
                    _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_high_s16(_vacc0), 3);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_high_s16(_vacc0), 3);

                    va += 8;
                    vb += 64;
                }

                for (; k<L; k++)
                {
                    int8x8_t _vacc0_s8 = vld1_s8(va);
                    int16x8_t _vacc0 = vmovl_s8(_vacc0_s8);

                    // k=0
                    int8x8_t _vb_s8 = vld1_s8(vb);
                    int16x8_t _vb = vmovl_s8(_vb_s8);
                    _sum0 = vmlal_lane_s16(_sum0, vget_low_s16(_vb), vget_low_s16(_vacc0), 0);
                    _sum0n = vmlal_lane_s16(_sum0n, vget_high_s16(_vb), vget_low_s16(_vacc0), 0);

                    va += 1;
                    vb += 8;
                }

                vst1q_s32(output, _sum0);
                vst1q_s32(output+4, _sum0n);  
#else
                asm volatile(
                    // inch loop
                    "vmov.s32    q6, #0            \n"
                    "vmov.s32    q7, #0            \n"

                    "lsr         r4, %6, #3        \n"// r4 = nn = inch >> 3
                    "cmp         r4, #0            \n"
                    "beq         1f                \n"
                    
                    "0:                            \n"// for(; nn != 0; nn--)
                    "pld         [%1, #128]        \n"
                    "vld1.s8     {d4-d7}, [%1]!    \n"// tmpr a00-a07,a10-a17,a20-a27,a30-a37    a(inch)(data)
                    "vmovl.s8    q5, d7            \n"// a30-a37
                    "vmovl.s8    q4, d6            \n"// a20-a27
                    "vmovl.s8    q3, d5            \n"// a10-a17
                    "vmovl.s8    q2, d4            \n"// a00-a07

                    "pld         [%2, #128]        \n"
                    "vld1.s8     {d0}, [%2]!       \n"// kptr k00-k07    k(outch)(inch)
                    "vmovl.s8    q1, d1            \n"// k04,k05,k06,k07
                    "vmovl.s8    q0, d0            \n"// k00,k01,k02,k03

                    "vmlal.s16   q6, d4, d0[0]     \n"// (a00-a07) * k00
                    "vmlal.s16   q7, d5, d0[0]     \n"
                    "vmlal.s16   q6, d6, d0[1]     \n"// (a10-a17) * k01
                    "vmlal.s16   q7, d7, d0[1]     \n"
                    "vmlal.s16   q6, d8, d0[2]     \n"// (a20-a27) * k02
                    "vmlal.s16   q7, d9, d0[2]     \n"
                    "vmlal.s16   q6, d10, d0[3]    \n"// (a30-a37) * k03
                    "vmlal.s16   q7, d11, d0[3]    \n"

                    "pld         [%1, #128]        \n"
                    "vld1.s8     {d4-d7}, [%1]!    \n"// tmpr a40-a47,a50-a57,a60-a67,a70-a77    a(inch)(data)
                    "vmovl.s8    q5, d7            \n"// a70-a77
                    "vmovl.s8    q4, d6            \n"// a60-a67
                    "vmovl.s8    q3, d5            \n"// a50-a57
                    "vmovl.s8    q2, d4            \n"// a40-a47

                    "vmlal.s16   q6, d4, d1[0]     \n"// (a00-a07) * k00
                    "vmlal.s16   q7, d5, d1[0]     \n"
                    "vmlal.s16   q6, d6, d1[1]     \n"// (a10-a17) * k01
                    "vmlal.s16   q7, d7, d1[1]     \n"
                    "vmlal.s16   q6, d8, d1[2]     \n"// (a20-a27) * k02
                    "vmlal.s16   q7, d9, d1[2]     \n"
                    "vmlal.s16   q6, d10, d1[3]    \n"// (a30-a37) * k03
                    "vmlal.s16   q7, d11, d1[3]    \n"                    

                    "subs        r4, r4, #1        \n"
                    "bne         0b                \n"// end for
    
                    "1:                            \n"
                    // remain loop
                    "and         r4, %6, #7        \n"// r4 = remain = inch & 7
                    "cmp         r4, #0            \n"
                    "beq         3f                \n"

                    "2:                            \n"// for(; remain != 0; remain--)
                    "vld1.s8     {d2}, [%1]!       \n"// tmpr a00-a07    a(inch)(data)
                    "vld1.s8     {d0}, [%2]        \n"// kptr k00        k(outch)(inch)
                    "vmovl.s8    q1, d2            \n"
                    "vmovl.s8    q0, d0            \n"
                    "add         %2, #1            \n"

                    "vmlal.s16   q6, d2, d0[0]     \n"// (a00-a07) * k00
                    "vmlal.s16   q7, d3, d0[0]     \n"  

                    "subs        r4, r4, #1        \n"
                    "bne         2b                \n"

                    "3:                            \n"// store the result to memory
                    "vst1.s32    {d12-d15}, [%0]   \n"

                    : "=r"(output), // %0
                      "=r"(vb),     // %1
                      "=r"(va)      // %2
                    : "0"(output),
                      "1"(vb),
                      "2"(va),
                      "r"(L)        // %6  
                    : "cc", "memory", "r4", "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7"
                );
#endif // __aarch64__                                         
#else                
                int sum[8] = {0};

                int k=0;
                for (; k+7<L; k=k+8)
                {
                    for (int n=0; n<8; n++)
                    {
                        sum[n] += (int)va[0] * vb[n];
                        sum[n] += (int)va[1] * vb[n+8];
                        sum[n] += (int)va[2] * vb[n+16];
                        sum[n] += (int)va[3] * vb[n+24];
                        sum[n] += (int)va[4] * vb[n+32];
                        sum[n] += (int)va[5] * vb[n+40];
                        sum[n] += (int)va[6] * vb[n+48];
                        sum[n] += (int)va[7] * vb[n+56];
                    }

                    va += 8;
                    vb += 64;    
                }

                for (; k<L; k++)
                {
                    for (int n=0; n<8; n++)
                    {
                        sum[n] += (int)va[0] * vb[n];
                    }

                    va += 1;
                    vb += 8;
                }

                for (int n=0; n<8; n++)
                {
                    output[n] = sum[n];
                }
#endif // __ARM_NEON
                output += 8;
            }

            for (; j<N; j++)
            {
                int sum = 0;

                signed char* vb = bottom_tm.channel(j/8 + j%8);
#if __aarch64__
                signed char* va = kernel_tm.channel(i/8 + (i%8)/4 + i%4);
#else                
                signed char* va = kernel_tm.channel(i/4 + i%4);
#endif

                for (int k=0; k<L; k++)
                {
                    sum += (int)va[0] * vb[0];

                    va += 1;
                    vb += 1;
                }
                output[0] = sum;

                output++;
            }
        }
    }
}
