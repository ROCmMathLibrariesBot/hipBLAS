/* ************************************************************************
 * Copyright 2016-2021 Advanced Micro Devices, Inc.
 *
 * ************************************************************************ */

#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "testing_common.hpp"

using namespace std;

/* ============================================================================================ */

template <typename T>
hipblasStatus_t testing_copy_batched(const Arguments& argus)
{
    bool FORTRAN = argus.fortran;
    auto hipblasCopyBatchedFn
        = FORTRAN ? hipblasCopyBatched<T, true> : hipblasCopyBatched<T, false>;

    int N           = argus.N;
    int incx        = argus.incx;
    int incy        = argus.incy;
    int batch_count = argus.batch_count;

    hipblasLocalHandle handle(argus);

    // argument sanity check, quick return if input parameters are invalid before allocating invalid
    // memory
    if(N <= 0 || batch_count <= 0)
    {
        CHECK_HIPBLAS_ERROR(
            hipblasCopyBatchedFn(handle, N, nullptr, incx, nullptr, incy, batch_count));
        return HIPBLAS_STATUS_SUCCESS;
    }

    int abs_incy = incy >= 0 ? incy : -incy;

    double hipblas_error = 0.0;
    double gpu_time_used = 0.0;

    // Naming: dX is in GPU (device) memory. hK is in CPU (host) memory, plz follow this practice
    host_batch_vector<T> hx(N, incx, batch_count);
    host_batch_vector<T> hy(N, incy, batch_count);
    host_batch_vector<T> hx_cpu(N, incx, batch_count);
    host_batch_vector<T> hy_cpu(N, incy, batch_count);

    device_batch_vector<T> dx(N, incx, batch_count);
    device_batch_vector<T> dy(N, incy, batch_count);
    CHECK_HIP_ERROR(dx.memcheck());
    CHECK_HIP_ERROR(dy.memcheck());

    hipblas_init(hx, true);
    hipblas_init(hy, false);

    hx_cpu.copy_from(hx);
    hy_cpu.copy_from(hy);
    CHECK_HIP_ERROR(dx.transfer_from(hx));
    CHECK_HIP_ERROR(dy.transfer_from(hy));

    if(argus.unit_check || argus.norm_check)
    {
        /* =====================================================================
                    HIPBLAS
        =================================================================== */
        CHECK_HIPBLAS_ERROR(hipblasCopyBatchedFn(
            handle, N, dx.ptr_on_device(), incx, dy.ptr_on_device(), incy, batch_count));

        // copy output from device to CPU
        CHECK_HIP_ERROR(hx.transfer_from(dx));
        CHECK_HIP_ERROR(hy.transfer_from(dy));

        /* =====================================================================
                    CPU BLAS
        =================================================================== */
        for(int b = 0; b < batch_count; b++)
        {
            cblas_copy<T>(N, hx_cpu[b], incx, hy_cpu[b], incy);
        }

        // enable unit check, notice unit check is not invasive, but norm check is,
        // unit check and norm check can not be interchanged their order
        if(argus.unit_check)
        {
            unit_check_general<T>(1, N, batch_count, abs_incy, hy_cpu, hy);
        }
        if(argus.norm_check)
        {
            hipblas_error = norm_check_general<T>('F', 1, N, abs_incy, hy_cpu, hy, batch_count);
        }

    } // end of if unit check

    if(argus.timing)
    {
        hipStream_t stream;
        CHECK_HIPBLAS_ERROR(hipblasGetStream(handle, &stream));

        int runs = argus.cold_iters + argus.iters;
        for(int iter = 0; iter < runs; iter++)
        {
            if(iter == argus.cold_iters)
                gpu_time_used = get_time_us_sync(stream);

            CHECK_HIPBLAS_ERROR(hipblasCopyBatchedFn(
                handle, N, dx.ptr_on_device(), incx, dy.ptr_on_device(), incy, batch_count));
        }
        gpu_time_used = get_time_us_sync(stream) - gpu_time_used;

        ArgumentModel<e_N, e_incx, e_incy, e_batch_count>{}.log_args<T>(std::cout,
                                                                        argus,
                                                                        gpu_time_used,
                                                                        copy_gflop_count<T>(N),
                                                                        copy_gbyte_count<T>(N),
                                                                        hipblas_error);
    }

    return HIPBLAS_STATUS_SUCCESS;
}
