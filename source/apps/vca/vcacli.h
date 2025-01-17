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

#pragma once

#include <common/getopt/getopt.h>

#include <stdio.h>

static const char short_options[]         = "f:h?";
static const struct option long_options[] = {{"help", no_argument, NULL, 'h'},
                                             {"asm", required_argument, NULL, 0},
                                             {"no-asm", no_argument, NULL, 0},
                                             {"input", required_argument, NULL, 0},
                                             {"input-depth", required_argument, NULL, 0},
                                             {"input-res", required_argument, NULL, 0},
                                             {"input-csp", required_argument, NULL, 0},
                                             {"input-fps", required_argument, NULL, 0},
                                             {"skip", required_argument, NULL, 0},
                                             {"frames", required_argument, NULL, 'f'},
                                             {"complexity-csv", required_argument, NULL, 0},
                                             {"shot-csv", required_argument, NULL, 0},
                                             {"yuview-stats", required_argument, NULL, 0},
                                             {"max-thresh", required_argument, NULL, 0},
                                             {"min-thresh", required_argument, NULL, 0},
                                             {"block-size", required_argument, NULL, 0},
                                             {"threads", required_argument, NULL, 0},
                                             {0, 0, 0, 0},
                                             {0, 0, 0, 0},
                                             {0, 0, 0, 0},
                                             {0, 0, 0, 0},
                                             {0, 0, 0, 0}};

static void showHelp()
{
    printf("\nSyntax: vca [options] infile\n");
    printf("    infile can be YUV or Y4M\n");
    printf("\nExecutable Options:\n");
    printf("-h/--help                        Show this help text and exit\n");
    printf("\nInput Options:\n");
    printf("   --input <filename>            Raw YUV or Y4M input file name. `-` for stdin\n");
    printf("   --y4m                         Force parsing of input stream as YUV4MPEG2 regardless "
           "of file extension\n");
    printf("   --input-res WxH               Source picture size [w x h], auto-detected if Y4M\n");
    printf("   --input-depth <integer>       Bit-depth of input file. Default 8\n");
    printf("   --input-csp <string>          Chroma subsampling, auto-detected if Y4M\n");
    printf("                                 400 (4:0:0 monochrome)\n");
    printf("                                 420 (4:2:0 default)\n");
    printf("                                 422 (4:2:2)\n");
    printf("                                 444 (4:4:4)\n");
    printf("   --input-fps <double>          Input fps, auto-detected if Y4M. Needed for shot "
           "detection.\n");
    printf("-f/--frames <integer>            Maximum number of frames to analyze. Default all\n");
    printf("   --skip <integer>              Skip N frames in the input before starting the "
           "analysis\n");
    printf("\nOutput Options:\n");
    printf("   --complexity-csv <filename>   Comma separated complexity log file\n");
    printf("   --shot-csv <filename>         Comma separated shot detection log file.\n");
    printf("                                 Specify a filename to enable shot-detection.\n");
    printf("   --yuview-stats <filename>     Write the per block results (energy, sad) to a stats "
           "file\n");
    printf("                                 that can be visualized using YUView.\n");
    printf("\nOperation Options:\n");
    printf("   --[no-]asm                    Enable / disable ASM. Default: Enabled\n");
    printf("   --max-thresh <float>          Maximum threshold of epsilon in shot detection\n");
    printf("   --min-thresh <float>          Minimum threshold of epsilon in shot detection\n");
    printf("   --block-size <integer>        Block size for DCT transform. Must be 8, 16 or 32 "
           "(Default).\n");
    printf("   --threads <integer>           Nr of threads to use. (Default: 0 (autodetect))\n");
}
