/*****************************************************************************
 * Copyright (C) 2021 Christian Doppler Laboratory ATHENA
 *
 * Authors: Vignesh V Menon <vignesh.menon@aau.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#if _MSC_VER
#pragma warning(disable: 4127) // conditional expression is constant
#endif

#include "vca.h"
#include "vcacli.h"

#include "input/input.h"

#if HAVE_VLD
/* Visual Leak Detector */
#include <vld.h>
#endif

#include <signal.h>
#include <errno.h>
#include <fcntl.h>

#include <string>
#include <ostream>
#include <fstream>
#include <queue>

#define CONSOLE_TITLE_SIZE 200
#ifdef _WIN32
#include <windows.h>
#define SetThreadExecutionState(es)
static char orgConsoleTitle[CONSOLE_TITLE_SIZE] = "";
#else
#define GetConsoleTitle(t, n)
#define SetConsoleTitle(t)
#define SetThreadExecutionState(es)
#endif

using namespace VCA_NS;

/* Ctrl-C handler */
static volatile sig_atomic_t b_ctrl_c /* = 0 */;
static void sigint_handler(int)
{
    b_ctrl_c = 1;
}
#define START_CODE 0x00000001
#define START_CODE_BYTES 4

static void printVersion(vca_param *param)
{
    vca_log(param, VCA_LOG_INFO, "Video Complexity Analyzer\n");
}

struct CLIOptions
{
    const vca_api* api;
    InputFile* input;
    vca_param* param;
    bool bProgress;
    bool bForceY4m;
    bool bDither;
    uint32_t seek;              // number of frames to skip from the beginning
    uint32_t framesToBeAnalyzed; // number of frames to analyze
    uint64_t totalbytes;
    int64_t startTime;
    int64_t prevUpdateTime;

    /* in microseconds */
    static const int UPDATE_INTERVAL = 250000;

    CLIOptions()
    {
        api = NULL;
        input = NULL;
        param = NULL;
        framesToBeAnalyzed = seek = 0;
        totalbytes = 0;
        bProgress = true;
        bForceY4m = false;
        startTime = vca_mdate();
        prevUpdateTime = 0;
        bDither = false;
    }

    void destroy();
    void printStatus(uint32_t frameNum);
    bool parse(int argc, char **argv);
};

void CLIOptions::destroy()
{
    if (input)
        input->release();
    input = NULL;
}

void CLIOptions::printStatus(uint32_t frameNum)
{
    char buf[200];
    int64_t time = vca_mdate();

    if (!bProgress || !frameNum || (prevUpdateTime && time - prevUpdateTime < UPDATE_INTERVAL))
        return;

    int64_t elapsed = time - startTime;
    double fps = elapsed > 0 ? frameNum * 1000000. / elapsed : 0;
    if (framesToBeAnalyzed)
    {
        int eta = (int)(elapsed * (framesToBeAnalyzed - frameNum) / ((int64_t)frameNum * 1000000));
        sprintf(buf, "vca [%.1f%%] %d/%d frames, %.2f fps, eta %d:%02d:%02d",
            100. * frameNum / (param->totalFrames), frameNum, param->totalFrames, fps,
                eta / 3600, (eta / 60) % 60, eta % 60);
    }
    else
        sprintf(buf, "vca %d frames: %.2f fps", frameNum, fps);

    fprintf(stderr, "%s  \r", buf + 5);
    SetConsoleTitle(buf);
    fflush(stderr); // needed in windows
    prevUpdateTime = time;
}

bool CLIOptions::parse(int argc, char **argv)
{
    bool bError = false;
    int bShowHelp = false;
    int inputBitDepth = 8;
    const char *inputfn = NULL;

    if (argc <= 1)
    {
        vca_log(NULL, VCA_LOG_ERROR, "No input file. Run vca --help for a list of options.\n");
        return true;
    }

    /* Presets are applied before all other options. */
    for (optind = 0;; )
    {
        int c = getopt_long(argc, argv, short_options, long_options, NULL);
        if (c == -1)
            break;
        else if (c == '?')
            bShowHelp = true;
    }

    api = vca_api_get(0);
    param = param_alloc();
    if (!param)
    {
        vca_log(NULL, VCA_LOG_ERROR, "param alloc failed\n");
        return true;
    }
    param_default(param);
    if (bShowHelp)
    {
        showHelp(param);
    }

    for (optind = 0;; )
    {
        int long_options_index = -1;
        int c = getopt_long(argc, argv, short_options, long_options, &long_options_index);
        if (c == -1)
            break;

        switch (c)
        {
        case 'h':
            printVersion(param);
            showHelp(param);
            break;

        case 'V':
            printVersion(param);
            vca_report_simd(param);
            exit(0);
        default:
            if (long_options_index < 0 && c > 0)
            {
                for (size_t i = 0; i < sizeof(long_options) / sizeof(long_options[0]); i++)
                {
                    if (long_options[i].val == c)
                    {
                        long_options_index = (int)i;
                        break;
                    }
                }

                if (long_options_index < 0)
                {
                    /* getopt_long might have already printed an error message */
                    if (c != 63)
                        vca_log(NULL, VCA_LOG_WARNING, "internal error: short option '%c' has no long option\n", c);
                    return true;
                }
            }
            if (long_options_index < 0)
            {
                vca_log(NULL, VCA_LOG_WARNING, "short option '%c' unrecognized\n", c);
                return true;
            }
#define OPT(longname) \
    else if (!strcmp(long_options[long_options_index].name, longname))
#define OPT2(name1, name2) \
    else if (!strcmp(long_options[long_options_index].name, name1) || \
             !strcmp(long_options[long_options_index].name, name2))

            if (0) ;
            OPT2("frame-skip", "seek") this->seek = (uint32_t)vca_atoi(optarg, bError);
            OPT("frames") this->framesToBeAnalyzed = (uint32_t)vca_atoi(optarg, bError);
            OPT("no-progress") this->bProgress = false;
            OPT("input") inputfn = optarg;
            OPT("input-depth") inputBitDepth = (uint32_t)vca_atoi(optarg, bError);
            OPT("dither") this->bDither = true;
            OPT("fullhelp")
            {
                param->logLevel = VCA_LOG_FULL;
                showHelp(param);
                break;
            }
            else
                bError |= !!param_parse(param, long_options[long_options_index].name, optarg);
            if (bError)
            {
                const char *name = long_options_index > 0 ? long_options[long_options_index].name : argv[optind - 2];
                vca_log(NULL, VCA_LOG_ERROR, "invalid argument: %s = %s\n", name, optarg);
                return true;
            }
#undef OPT
        }
    }

    if (optind < argc && !inputfn)
        inputfn = argv[optind++];
    if (optind < argc)
    {
        vca_log(param, VCA_LOG_WARNING, "extra unused command arguments given <%s>\n", argv[optind]);
        return true;
    }

    if (argc <= 1)
    {
        param_default(param);
        showHelp(param);
    }

    if (!inputfn)
    {
        vca_log(param, VCA_LOG_ERROR, "input file not specified, try --help for help\n");
        return true;
    }

    InputFileInfo info;
    info.filename = inputfn;
    info.depth = inputBitDepth;
    info.csp = param->internalCsp;
    info.width = param->sourceWidth;
    info.height = param->sourceHeight;
    info.fpsNum = param->fpsNum;
    info.fpsDenom = param->fpsDenom;
    info.sarWidth = param->vui.sarWidth;
    info.sarHeight = param->vui.sarHeight;
    info.skipFrames = seek;
    info.frameCount = 0;
    getParamAspectRatio(param, info.sarWidth, info.sarHeight);


    this->input = InputFile::open(info, this->bForceY4m);
    if (!this->input || this->input->isFail())
    {
        vca_log_file(param, VCA_LOG_ERROR, "unable to open input file <%s>\n", inputfn);
        return true;
    }

    if (info.depth < 8 || info.depth > 16)
    {
        vca_log(param, VCA_LOG_ERROR, "Input bit depth (%d) must be between 8 and 16\n", inputBitDepth);
        return true;
    }

    /* Unconditionally accept height/width/csp from file info */
    param->sourceWidth = info.width;
    param->sourceHeight = info.height;
    param->internalCsp = info.csp;

    /* Accept fps and sar from file info if not specified by user */
    if (param->fpsDenom == 0 || param->fpsNum == 0)
    {
        param->fpsDenom = info.fpsDenom;
        param->fpsNum = info.fpsNum;
    }
    if (!param->vui.aspectRatioIdc && info.sarWidth && info.sarHeight)
        setParamAspectRatio(param, info.sarWidth, info.sarHeight);
    if (this->framesToBeAnalyzed == 0 && info.frameCount > (int)seek)
        this->framesToBeAnalyzed = info.frameCount - seek;
    param->totalFrames = this->framesToBeAnalyzed;
    
    /* Force CFR until we have support for VFR */
    info.timebaseNum = param->fpsDenom;
    info.timebaseDenom = param->fpsNum;

    if (param->logLevel >= VCA_LOG_INFO)
    {
        char buf[128];
        int p = sprintf(buf, "%dx%d fps %d/%d %sp%d", param->sourceWidth, param->sourceHeight,
                        param->fpsNum, param->fpsDenom, vca_source_csp_names[param->internalCsp], info.depth);

        int width, height;
        getParamAspectRatio(param, width, height);
        if (width && height)
            p += sprintf(buf + p, " sar %d:%d", width, height);

        if (framesToBeAnalyzed <= 0 || info.frameCount <= 0)
            strcpy(buf + p, " unknown frame count");
        else
            sprintf(buf + p, " frames %u - %d of %d", this->seek, this->seek + this->framesToBeAnalyzed - 1, info.frameCount);

        general_log(param, input->getName(), VCA_LOG_INFO, "%s\n", buf);
    }
    this->input->startReader();
    return false;
}

#ifdef _WIN32
/* Copy of x264 code, which allows for Unicode characters in the command line.
 * Retrieve command line arguments as UTF-8. */
static int get_argv_utf8(int *argc_ptr, char ***argv_ptr)
{
    int ret = 0;
    wchar_t **argv_utf16 = CommandLineToArgvW(GetCommandLineW(), argc_ptr);
    if (argv_utf16)
    {
        int argc = *argc_ptr;
        int offset = (argc + 1) * sizeof(char*);
        int size = offset;

        for (int i = 0; i < argc; i++)
            size += WideCharToMultiByte(CP_UTF8, 0, argv_utf16[i], -1, NULL, 0, NULL, NULL);

        char **argv = *argv_ptr = (char**)malloc(size);
        if (argv)
        {
            for (int i = 0; i < argc; i++)
            {
                argv[i] = (char*)argv + offset;
                offset += WideCharToMultiByte(CP_UTF8, 0, argv_utf16[i], -1, argv[i], size - offset, NULL, NULL);
            }
            argv[argc] = NULL;
            ret = 1;
        }
        LocalFree(argv_utf16);
    }
    return ret;
}
#endif

/* CLI return codes:
 *
 * 0 - analyze successful
 * 1 - unable to parse command line
 * 2 - unable to open analyzer
 * 3 - unable to generate stream headers
 * 4 - analyzer abort */

int main(int argc, char **argv)
{
#if HAVE_VLD
    // This uses Microsoft's proprietary WCHAR type, but this only builds on Windows to start with
    VLDSetReportOptions(VLD_OPT_REPORT_TO_DEBUGGER | VLD_OPT_REPORT_TO_FILE, L"vca_leaks.txt");
#endif
    PROFILE_INIT();
    THREAD_NAME("API", 0);

    GetConsoleTitle(orgConsoleTitle, CONSOLE_TITLE_SIZE);
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
#if _WIN32
    char** orgArgv = argv;
    get_argv_utf8(&argc, &argv);
#endif

    CLIOptions cliopt;

    if (cliopt.parse(argc, argv))
    {
        cliopt.destroy();
        param_free(cliopt.param);
        exit(1);
    }

    vca_param* param = cliopt.param;
    const vca_api* api = cliopt.api;
    vca_analyzer *analyzer = api->analyzer_open(param);
    if (!analyzer)
    {
        vca_log(param, VCA_LOG_ERROR, "failed to open analyzer\n");
        cliopt.destroy();
        param_free(param);
        exit(2);
    }
    api->analyzer_parameters(analyzer, param);

     /* Control-C handler */
    if (signal(SIGINT, sigint_handler) == SIG_ERR)
        vca_log(param, VCA_LOG_ERROR, "Unable to register CTRL+C handler: %s\n", strerror(errno));

    vca_picture pic_orig;
    vca_picture *pic_in = &pic_orig;
    uint32_t inFrameCount = 0;
    uint32_t outFrameCount = 0;
    int16_t *errorBuf = NULL;
    int ret = 0;

    api->picture_init(param, pic_in);
    if (cliopt.bDither)
    {
        errorBuf = VCA_MALLOC(int16_t, param->sourceWidth + 1);
        if (errorBuf)
            memset(errorBuf, 0, (param->sourceWidth + 1) * sizeof(int16_t));
        else
            cliopt.bDither = false;
    }

    // main analyzer loop
    while (pic_in && !b_ctrl_c)
    {
        pic_orig.poc = inFrameCount;
        if (cliopt.framesToBeAnalyzed && inFrameCount >= cliopt.framesToBeAnalyzed)
            pic_in = NULL;
        else if (cliopt.input->readPicture(pic_orig))
            inFrameCount++;
        else
            pic_in = NULL;

        if (pic_in)
        {
            if (pic_in->bitDepth > param->internalBitDepth && cliopt.bDither)
            {
                dither_image(pic_in, cliopt.input->getWidth(), cliopt.input->getHeight(), errorBuf, param->internalBitDepth);
                pic_in->bitDepth = param->internalBitDepth;
            }
            /* Overwrite PTS */
            pic_in->pts = pic_in->poc;
        }
        int numAnalyzed = api->analyzer_analyze(analyzer, pic_in);
        if (numAnalyzed < 0)
        {
            b_ctrl_c = 1;
            ret = 4;
            break;
        }
        outFrameCount += numAnalyzed;
        cliopt.printStatus(outFrameCount);
    }

    /* Flush the analyzer */
    while (!b_ctrl_c)
    {
        int numAnalyzed = api->analyzer_analyze(analyzer, NULL);
        if (numAnalyzed < 0)
        {
            ret = 4;
            break;
        }
        outFrameCount += numAnalyzed;
        cliopt.printStatus(outFrameCount);
        if (!numAnalyzed)
            break;
    }

    /* clear progress report */
    if (cliopt.bProgress)
        fprintf(stderr, "%*s\r", 80, " ");

    /* Shot detection */
    if (param->bEnableShotdetect)
    {
        api->analyzer_shot_detect(analyzer);
        api->analyzer_shot_print(analyzer);
    }

    api->analyzer_close(analyzer);
    cliopt.destroy();
    param_free(param);
    VCA_FREE(errorBuf);
    SetConsoleTitle(orgConsoleTitle);
    SetThreadExecutionState(ES_CONTINUOUS);
#if _WIN32
    if (argv != orgArgv)
    {
        free(argv);
        argv = orgArgv;
    }
#endif

    return ret;
}