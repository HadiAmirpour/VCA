/*****************************************************************************
 * Copyright (C) 2022 Christian Doppler Laboratory ATHENA
 *
 * Authors: Vignesh V Menon <vignesh.menon@aau.at>
 *          Christian Feldmann <christian.feldmann@bitmovin.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 *****************************************************************************/

/// This is the public interface for the bitstream lib

#pragma once

#include "vcaColorSpace.h"
#include <stdint.h>

#if defined(_MSC_VER) && !defined(VCA_STATIC_BUILD)
#define DLL_PUBLIC __declspec(dllexport)
#else
#define DLL_PUBLIC __attribute__((__visibility__("default")))
#endif

extern "C" {

/* vca_analyzer:
 *      opaque handler for analyzer */
typedef void vca_analyzer;

/* vca_picyuv:
 *      opaque handler for PicYuv */
typedef struct vca_picyuv vca_picyuv;

enum class LogLevel
{
    Error,
    Warning,
    Info,
    Debug
};

enum class CpuSimd
{
    Autodetect,
    None,
    SSE2,
    SSSE3,
    SSE4,
    AVX2
};

struct vca_frame_texture_t
{
    int32_t *m_ctuAbsoluteEnergy;
    double *m_ctuRelativeEnergy;
    int32_t m_variance;
    int32_t m_avgEnergy;
};

/* Frame level statistics */
struct vca_frame_stats
{
    int poc;
    int32_t e_value;
    double h_value;
    double epsilon;
};

struct vca_frame_results
{
    /* Pointer to storage for one value per block in the frame.
     * The caller must make sure that this is pointing to a valid and big enough block of memory.
     * These must not be pointing to memory. If they are nullptr, no data will be written.
     */
    uint32_t *energyPerBlock{};
    uint32_t averageEnergy{};

    uint32_t *sadPerBlock{};
    double sad{};

    double epsilon{};

    int poc{};

    // An increasing counter that is incremented with each call to 'vca_analyzer_push'.
    // So with this one can double check that the results are recieved in the right order.
    unsigned jobID{};
};

struct vca_frame_info
{
    unsigned width{};
    unsigned height{};
    unsigned bitDepth{8};
    vca_colorSpace colorspace{vca_colorSpace::YUV420};
};

/* Used to pass pictures into the analyzer, and to get picture data back out of
 * the analyzer.  The input and output semantics are different */
struct vca_frame
{
    /* Must be specified on input pictures, the number of planes is determined
     * by the colorSpace value */
    uint8_t *planes[3]{nullptr, nullptr, nullptr};

    /* Stride is the number of bytes between row starts */
    int stride[3]{0, 0, 0};

    vca_frame_stats stats;
    vca_frame_info info;
};

/* vca input parameters
 *
 */
struct vca_param
{
    bool enableASM{true};

    vca_frame_info frameInfo{};

    // Size (width/height) of the analysis block. Must be 8, 16 or 32.
    unsigned blockSize{32};

    unsigned nrFrameThreads{0};
    unsigned nrSliceThreads{0};

    CpuSimd cpuSimd{CpuSimd::Autodetect};

    void (*logFunction)(void *, LogLevel, const char *){};
    void *logFunctionPrivateData{};
};

/* Create a new analyzer or nullptr if the config is invalid.
 */
DLL_PUBLIC vca_analyzer *vca_analyzer_open(vca_param cfg);

typedef enum
{
    VCA_OK = 0,
    VCA_ERROR
} vca_result;

/* Push a frame to the analyzer and start the analysis.
 * Note that only the pointers will be copied but no ownership of the memory is
 * transferred to the library. The caller must make sure that the pointers are
 * valid until the frame was analyzed. Once a results for a frame was pulled the
 * library will not use pointers anymore.
 * This may block until there is a slot available to work on. The number of
 * frames that will be processed in parallel can be set using nrFrameThreads.
 */
DLL_PUBLIC vca_result vca_analyzer_push(vca_analyzer *enc, vca_frame *pic_in);

/* Check if a result is available to pull.
 */
DLL_PUBLIC bool vca_result_available(vca_analyzer *enc);

/* Pull a result from the analyzer. This may block until a result is available.
 * Use vca_result_available if you want to only check if a result is ready.
 */
DLL_PUBLIC vca_result vca_analyzer_pull_frame_result(vca_analyzer *enc, vca_frame_results *result);

DLL_PUBLIC void vca_analyzer_close(vca_analyzer *enc);

struct vca_shot_detection_param
{
    double minEpsilonThresh{10};
    double maxEpsilonThresh{50};

    double fps{};

    void (*logFunction)(void *, LogLevel, const char *){};
    void *logFunctionPrivateData{};
};

struct vca_shot_detect_frame
{
    double epsilon{};
    bool isNewShot{};
};

DLL_PUBLIC vca_result vca_shot_detection(const vca_shot_detection_param &param,
                                         vca_shot_detect_frame *frames,
                                         size_t num_frames);

DLL_PUBLIC extern const char *vca_version_str;

} // extern "C"
