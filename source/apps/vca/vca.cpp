/*****************************************************************************
 * Copyright (C) 2022 Christian Doppler Laboratory ATHENA
 *
 * Authors: Christian Feldmann <christian.feldmann@bitmovin.com>
 *          Vignesh V Menon <vignesh.menon@aau.at>
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

#include "vcacli.h"

#include <common/input/Y4MInput.h>
#include <common/input/YUVInput.h>
#include <common/stats/YUViewStatsFile.h>
#include <lib/vcaLib.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <optional>
#include <signal.h>
#include <queue>

#ifdef _WIN32
#include <windows.h>
#pragma warning(disable : 4996)
#endif

using namespace vca;
using namespace std::string_literals;

/* Ctrl-C handler */
static volatile sig_atomic_t b_ctrl_c /* = 0 */;
static void sigint_handler(int)
{
    b_ctrl_c = 1;
}

void logLibraryMessage(void *, LogLevel logLevel, const char *message)
{
    vca_log(logLevel, "[LIB] "s + message);
}

void printStatus(uint32_t frameNum, unsigned framesToBeAnalyzed, bool printSummary = false)
{
    char buf[200];
    static auto startTime      = std::chrono::high_resolution_clock::now();
    static auto prevUpdateTime = std::chrono::high_resolution_clock::now();
    auto now                   = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - prevUpdateTime);
    if (!printSummary && duration < std::chrono::milliseconds(250))
        return;

    prevUpdateTime = now;

    auto elapsedAbsMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime)
                            .count();
    double fps = elapsedAbsMs > 0 ? frameNum * 1000. / elapsedAbsMs : 0;

    if (printSummary)
    {
        fprintf(stdout,
                "vca - Analyzed %d/%d frames, %.2f fps, time %d:%02d:%02d.%03d\n",
                frameNum,
                framesToBeAnalyzed,
                fps,
                int(elapsedAbsMs / 1000 / 3600),
                int((elapsedAbsMs / 1000 / 60) % 60),
                int((elapsedAbsMs / 1000) % 60),
                int(elapsedAbsMs % 1000));
        return;
    }
    else if (framesToBeAnalyzed > 0)
    {
        int eta = (int) (elapsedAbsMs * (framesToBeAnalyzed - frameNum)
                         / ((int64_t) frameNum * 1000));
        sprintf(buf,
                "vca [%.1f%%] %d/%d frames, %.2f fps, eta %d:%02d:%02d",
                100. * frameNum / (framesToBeAnalyzed),
                frameNum,
                framesToBeAnalyzed,
                fps,
                eta / 3600,
                (eta / 60) % 60,
                eta % 60);
    }
    else
        sprintf(buf, "vca %d frames: %.2f fps", frameNum, fps);

    fprintf(stdout, "%s  \r", buf);
    fflush(stdout); // needed in windows
}

struct CLIOptions
{
    std::string inputFilename;
    bool openAsY4m{};
    unsigned skipFrames{};
    unsigned framesToBeAnalyzed{};
    std::string complexityCSVFilename;
    std::string shotCSVFilename;
    std::string yuviewStatsFilename;

    vca_param vcaParam;
    vca_shot_detection_param shotDetectParam;
};

struct Result
{
    Result(const vca_frame_info &info, unsigned blockSize)
    {
        auto widthInBlocks = (info.width + blockSize - 1) / blockSize;
        auto heightInBlock = (info.height + blockSize - 1) / blockSize;
        auto numberBlocks  = widthInBlocks * heightInBlock;
        this->energyPerBlockData.resize(numberBlocks);
        this->result.energyPerBlock = this->energyPerBlockData.data();
        this->sadPerBlockData.resize(numberBlocks);
        this->result.sadPerBlock = this->sadPerBlockData.data();
    }

    std::vector<uint32_t> energyPerBlockData;
    std::vector<uint32_t> sadPerBlockData;
    vca_frame_results result;
};

std::optional<CLIOptions> parseCLIOptions(int argc, char **argv)
{
    bool bError = false;
    CLIOptions options;

    if (argc <= 1)
    {
        vca_log(LogLevel::Error, "No input file. Run vca --help for a list of options.");
        return {};
    }

    int long_options_index = -1;
    while (true)
    {
        auto c = getopt_long(argc, argv, short_options, long_options, &long_options_index);
        if (c == -1)
            break;

        if (c == 'h')
        {
            showHelp();
            return {};
        }

        if (long_options_index < 0 && c > 0)
        {
            for (size_t i = 0; i < sizeof(long_options) / sizeof(long_options[0]); i++)
            {
                if (long_options[i].val == c)
                {
                    long_options_index = (int) i;
                    break;
                }
            }

            if (long_options_index < 0)
            {
                /* getopt_long might have already printed an error message */
                if (c != 63)
                    vca_log(LogLevel::Warning,
                            "internal error: short option " + std::string(1, c)
                                + " has no long option");
                return {};
            }
        }
        if (long_options_index < 0)
        {
            vca_log(LogLevel::Warning, "short option " + std::string(1, c) + " unrecognized");
            return {};
        }

        auto name = std::string(long_options[long_options_index].name);
        auto arg  = std::string(optarg);
        if (name == "asm")
            options.vcaParam.enableASM = true;
        else if (name == "no-asm")
            options.vcaParam.enableASM = false;
        else if (name == "input")
            options.inputFilename = optarg;
        else if (name == "input-depth")
            options.vcaParam.frameInfo.bitDepth = std::stoul(optarg);
        else if (name == "input-res")
        {
            auto posX = arg.find("x");
            if (posX == std::string::npos)
            {
                vca_log(LogLevel::Error, "Invalid resolution provided. Format WxH.");
                return {};
            }
            options.vcaParam.frameInfo.width  = std::stoul(arg.substr(0, posX));
            options.vcaParam.frameInfo.height = std::stoul(arg.substr(posX + 1));
        }
        else if (name == "input-csp")
        {
            if (arg == "400" || arg == "4:0:0")
                options.vcaParam.frameInfo.colorspace = vca_colorSpace::YUV400;
            else if (arg == "420" || arg == "4:2:0")
                options.vcaParam.frameInfo.colorspace = vca_colorSpace::YUV420;
            else if (arg == "422" || arg == "4:2:2")
                options.vcaParam.frameInfo.colorspace = vca_colorSpace::YUV422;
            else if (arg == "444" || arg == "4:4:4")
                options.vcaParam.frameInfo.colorspace = vca_colorSpace::YUV444;
        }
        else if (name == "input-fps")
            options.shotDetectParam.fps = std::stod(optarg);
        else if (name == "skip")
            options.skipFrames = std::stoul(optarg);
        else if (name == "frames")
            options.framesToBeAnalyzed = std::stoul(optarg);
        else if (name == "complexity-csv")
            options.complexityCSVFilename = optarg;
        else if (name == "shot-csv")
            options.shotCSVFilename = optarg;
        else if (name == "yuview-stats")
            options.yuviewStatsFilename = optarg;
        else if (name == "max-thresh")
            options.shotDetectParam.maxEpsilonThresh = std::stod(optarg);
        else if (name == "min-thresh")
            options.shotDetectParam.minEpsilonThresh = std::stod(optarg);
        else if (name == "block-size")
            options.vcaParam.blockSize = std::stoi(optarg);
        else if (name == "threads")
            options.vcaParam.nrFrameThreads = std::stoi(optarg);
    }

    if (options.inputFilename.substr(options.inputFilename.size() - 4) == ".y4m")
        options.openAsY4m = true;

    return options;
}

bool checkOptions(CLIOptions options)
{
    if (options.inputFilename.empty())
    {
        vca_log(LogLevel::Error, "No input filename specified");
        return false;
    }

    if (options.vcaParam.frameInfo.bitDepth < 8 || options.vcaParam.frameInfo.bitDepth > 16)
    {
        vca_log(LogLevel::Error, "Bit depth must be between 8 and 16 bits.");
        return false;
    }

    if (!options.openAsY4m
        && (options.vcaParam.frameInfo.width == 0 || options.vcaParam.frameInfo.height == 0))
    {
        vca_log(LogLevel::Error, "No frame size provided.");
        return false;
    }

    if (options.vcaParam.blockSize != 32 && options.vcaParam.blockSize != 16
        && options.vcaParam.blockSize != 8)
    {
        vca_log(LogLevel::Error,
                "Invalid block size (" + std::to_string(options.vcaParam.blockSize)
                    + ") provided. Valid values are 8, 16 and 32.");
        return false;
    }

    return true;
}

void logOptions(const CLIOptions &options)
{
    vca_log(LogLevel::Info, "Options:   "s);
    vca_log(LogLevel::Info, "  Input file name:   "s + options.inputFilename);
    vca_log(LogLevel::Info, "  Open as Y4m:       "s + (options.openAsY4m ? "True"s : "False"s));
    vca_log(LogLevel::Info, "  Skip frames:       "s + std::to_string(options.skipFrames));
    vca_log(LogLevel::Info, "  Frames to analyze: "s + std::to_string(options.framesToBeAnalyzed));
    vca_log(LogLevel::Info, "  Complexity csv:    "s + options.complexityCSVFilename);
    vca_log(LogLevel::Info, "  Shot csv:          "s + options.shotCSVFilename);
    vca_log(LogLevel::Info, "  YUView stats file: "s + options.yuviewStatsFilename);
}

void logResult(const Result &result, const vca_frame *frame, const unsigned resultsCounter)
{
    if (result.result.poc != frame->stats.poc)
        vca_log(LogLevel::Warning,
                "The poc of the returned data (" + std::to_string(result.result.poc)
                    + ") does not match the expected next frames POC ("
                    + std::to_string(frame->stats.poc) + ").");
    if (result.result.poc != resultsCounter)
        vca_log(LogLevel::Warning,
                "The poc of the returned data (" + std::to_string(result.result.poc)
                    + ") does not match the expected results counter ("
                    + std::to_string(resultsCounter) + ").");

    vca_log(LogLevel::Debug,
            "Got results POC " + std::to_string(result.result.poc) + " averageEnergy "
                + std::to_string(result.result.averageEnergy) + " sad "
                + std::to_string(result.result.sad));
}

void writeComplexityStatsToFile(const Result &result, std::ofstream &file)
{
    file << result.result.poc << ", " << result.result.averageEnergy << ", " << result.result.sad
         << ", " << result.result.epsilon << "\n";
}

void writeShotDetectionResultsToFile(const std::vector<vca_shot_detect_frame> &shotDetectFrames,
                                     std::ofstream &file)
{
    size_t shotCounter = 0;
    for (size_t i = 0; i < shotDetectFrames.size(); i++)
    {
        auto &frame = shotDetectFrames.at(i);
        if (frame.isNewShot)
        {
            file << shotCounter << ", " << i << "\n";
            shotCounter++;
        }
    }
}

#ifdef _WIN32
/* Copy of x264 code, which allows for Unicode characters in the command line.
 * Retrieve command line arguments as UTF-8. */
static int get_argv_utf8(int *argc_ptr, char ***argv_ptr)
{
    int ret              = 0;
    wchar_t **argv_utf16 = CommandLineToArgvW(GetCommandLineW(), argc_ptr);
    if (argv_utf16)
    {
        int argc   = *argc_ptr;
        int offset = (argc + 1) * sizeof(char *);
        int size   = offset;

        for (int i = 0; i < argc; i++)
            size += WideCharToMultiByte(CP_UTF8, 0, argv_utf16[i], -1, NULL, 0, NULL, NULL);

        char **argv = *argv_ptr = (char **) malloc(size);
        if (argv)
        {
            for (int i = 0; i < argc; i++)
            {
                argv[i] = (char *) argv + offset;
                offset += WideCharToMultiByte(CP_UTF8,
                                              0,
                                              argv_utf16[i],
                                              -1,
                                              argv[i],
                                              size - offset,
                                              NULL,
                                              NULL);
            }
            argv[argc] = NULL;
            ret        = 1;
        }
        LocalFree(argv_utf16);
    }
    return ret;
}
#endif

int main(int argc, char **argv)
{
#if _WIN32
    char **orgArgv = argv;
    get_argv_utf8(&argc, &argv);
#endif

    vca_log(LogLevel::Info, "VCA - Video Complexity Analyzer " + std::string(vca_version_str));

    CLIOptions options;
    if (auto cliOptions = parseCLIOptions(argc, argv))
        options = *cliOptions;
    else
    {
        vca_log(LogLevel::Error, "Error parsing parameters");
        return 1;
    }

    if (!checkOptions(options))
    {
        vca_log(LogLevel::Error, "Error checking parameters");
        return 1;
    }

    logOptions(options);

    std::unique_ptr<IInputFile> inputFile;
    if (options.openAsY4m)
        inputFile = std::make_unique<Y4MInput>(options.inputFilename, options.skipFrames);
    else
        inputFile = std::make_unique<YUVInput>(options.inputFilename,
                                               options.vcaParam.frameInfo,
                                               options.skipFrames);

    if (inputFile->isFail())
    {
        vca_log(LogLevel::Error, "Error opening input file");
        return 1;
    }

    std::ofstream complexityFile;
    if (!options.complexityCSVFilename.empty())
    {
        complexityFile.open(options.complexityCSVFilename);
        if (!complexityFile.is_open())
        {
            vca_log(LogLevel::Error,
                    "Error opening complexity CSV file " + options.complexityCSVFilename);
            return 1;
        }
        complexityFile << "POC, E, h, epsilon \n";
    }

    options.vcaParam.logFunction        = logLibraryMessage;
    options.shotDetectParam.logFunction = logLibraryMessage;

    auto analyzer = vca_analyzer_open(options.vcaParam);
    if (analyzer == nullptr)
    {
        vca_log(LogLevel::Error, "Error opening analyzer");
        return 2;
    }

    /* Control-C handler */
    if (signal(SIGINT, sigint_handler) == SIG_ERR)
        vca_log(LogLevel::Error,
                "Unable to register CTRL+C handler: " + std::string(strerror(errno)));

    auto frameInfo = inputFile->getFrameInfo();

    using framePtr = std::unique_ptr<FrameWithData>;
    std::queue<framePtr> frameRecycling;
    std::queue<framePtr> activeFrames;
    std::unique_ptr<YUViewStatsFile> yuviewStatsFile;
    std::vector<vca_shot_detect_frame> shotDetectFrames;
    unsigned pushedFrames   = 0;
    unsigned resultsCounter = 0;
    while (!inputFile->isEof() && !inputFile->isFail()
           && (options.framesToBeAnalyzed == 0 || pushedFrames < options.framesToBeAnalyzed))
    {
        {
            framePtr frame;
            if (frameRecycling.empty())
                frame = std::make_unique<FrameWithData>(inputFile->getFrameInfo());
            else
            {
                frame = std::move(frameRecycling.front());
                frameRecycling.pop();
            }

            try
            {
                if (!inputFile->readFrame(*frame))
                {
                    break;
                }
            }
            catch (const std::exception &e)
            {
                vca_log(LogLevel::Error, "Error reading frame from input: " + std::string(e.what()));
                return 3;
            }

            frame->getFrame()->stats.poc = pushedFrames;
            vca_log(LogLevel::Debug, "Read frame " + std::to_string(pushedFrames) + " from input");

            if (!options.yuviewStatsFilename.empty() && !yuviewStatsFile)
                yuviewStatsFile = std::make_unique<YUViewStatsFile>(options.yuviewStatsFilename,
                                                                    options.inputFilename,
                                                                    frame->getFrame()->info);

            auto ret = vca_analyzer_push(analyzer, frame->getFrame());
            if (ret == VCA_ERROR)
            {
                vca_log(LogLevel::Error, "Error pushing frame to lib");
                return 3;
            }
            vca_log(LogLevel::Debug,
                    "Pushed frame " + std::to_string(pushedFrames) + " to analyzer");

            activeFrames.push(std::move(frame));
            pushedFrames++;
        }

        while (vca_result_available(analyzer))
        {
            Result result(frameInfo, options.vcaParam.blockSize);

            if (vca_analyzer_pull_frame_result(analyzer, &result.result) == VCA_ERROR)
            {
                vca_log(LogLevel::Error, "Error pulling frame result");
                return 3;
            }

            if (yuviewStatsFile)
                yuviewStatsFile->write(result.result, options.vcaParam.blockSize);
            if (complexityFile.is_open())
                writeComplexityStatsToFile(result, complexityFile);
            if (!options.shotCSVFilename.empty())
                shotDetectFrames.push_back({result.result.epsilon, false});

            auto processedFrame = std::move(activeFrames.front());
            activeFrames.pop();

            logResult(result, processedFrame->getFrame(), resultsCounter);

            frameRecycling.push(std::move(processedFrame));
            resultsCounter++;
        }

        printStatus(resultsCounter, options.framesToBeAnalyzed);
    }

    while (resultsCounter < pushedFrames)
    {
        Result result(frameInfo, options.vcaParam.blockSize);

        if (vca_analyzer_pull_frame_result(analyzer, &result.result) == VCA_ERROR)
        {
            vca_log(LogLevel::Error, "Error pulling frame result");
            return 3;
        }

        if (yuviewStatsFile)
            yuviewStatsFile->write(result.result, options.vcaParam.blockSize);
        if (complexityFile.is_open())
            writeComplexityStatsToFile(result, complexityFile);
        if (!options.shotCSVFilename.empty())
            shotDetectFrames.push_back({result.result.epsilon, false});

        auto processedFrame = std::move(activeFrames.front());
        activeFrames.pop();

        logResult(result, processedFrame->getFrame(), resultsCounter);

        resultsCounter++;
    }

    vca_analyzer_close(analyzer);
    printStatus(resultsCounter, pushedFrames, true);

    if (!options.shotCSVFilename.empty())
    {
        if (options.shotDetectParam.fps == 0.0)
            options.shotDetectParam.fps = inputFile->getFPS();

        vca_shot_detection(options.shotDetectParam,
                           shotDetectFrames.data(),
                           shotDetectFrames.size());

        std::ofstream shotsFile;
        shotsFile.open(options.shotCSVFilename);
        if (!shotsFile.is_open())
        {
            vca_log(LogLevel::Error, "Error opening shot CSV file " + options.shotCSVFilename);
            return 1;
        }
        shotsFile << "ID, Start POC \n";
        writeShotDetectionResultsToFile(shotDetectFrames, shotsFile);

        vca_log(LogLevel::Info,
                "Performed shot detection for " + std::to_string(shotDetectFrames.size())
                    + " frames.");
    }

    return 0;
}
