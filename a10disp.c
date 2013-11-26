/*
  a10disp -- program to change the display mode of Allwinner A10 devices.

  Copyright 2013 Harm Hanemaaijer <fgenfb@yahoo.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/


#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <asm/types.h>

#include <linux/fb.h>
#include "sunxi_disp_ioctl.h"

// Comment out the following line to not use scaler mode for large modes
// like 1920x1080 at 32bpp.
#define USE_SCALER_FOR_LARGE_32BPP_MODES

// The number of buffers per framebuffer used when checking whether the
// selected mode fits into the framebuffer.
// You can change this to 1 if you don't use Mali or video acceleration and
// want to be able set larger modes with a small framebuffer.
// This can also be changed with the --nodoublebuffer option.
#define DEFAULT_NUMBER_OF_FRAMEBUFFER_BUFFERS 2

#define COMMAND_SWITCH_TO_HDMI		0
#define COMMAND_SWITCH_TO_HDMI_FORCE	1
#define COMMAND_SWITCH_TO_LCD		2
#define COMMAND_CHANGE_HDMI_MODE	3
#define COMMAND_CHANGE_HDMI_MODE_FORCE	4
#define COMMAND_CHANGE_PIXEL_DEPTH	5
#define COMMAND_DISPLAY_OFF		6
#define COMMAND_LCD_ON			7
#define COMMAND_INFO			8

static int fd_disp;
static int fd_fb[2];
static int nu_framebuffer_buffers = DEFAULT_NUMBER_OF_FRAMEBUFFER_BUFFERS;

static const char *mode_str[28] = {
	"480i",
	"576i",
	"480p",
	"576p",
	"720p 50Hz",
	"720p 60Hz",
 	"1080i 50 Hz",
	"1080i 60 Hz",
	"1080p 24 Hz",
	"1080p 50 Hz",
	"1080p 60 Hz",
        "PAL",
	"PAL SVIDEO",
	"",
	"NTSC",
	"NTSC SVIDEO",
	"",
	"PAL_M",
	"PAL_M SVIDEO",
	"",
	"PAL_NC",
	"PAL_NC SVIDEO",
	"",
	"1080p 24 Hz 3D",
	"720p 50 Hz 3D",
	"720p 60 Hz 3D",
	"1360x768 60 Hz",
	"1280x1024 60 Hz"
};

static int mode_size[28] = {
	640 * 480, 720 * 576, 640 * 480, 720 * 576, 1280 * 720, 1280 * 720, 1920 * 1080, 1920 * 1080, 1920 * 1080, 1920 * 1080, 1920 * 1080,
	720 * 576, 720 * 576, 0, 640 * 480, 640 * 480, 0, 720 * 576, 720 * 576, 0, 720 * 576, 720 * 576, 0, 1920 * 1080, 1280 * 720, 1280 * 720,
	1360 * 768, 1280 * 1024
};

static int mode_width[28] = { 640, 720, 640, 720, 1280, 1280, 1920, 1920, 1920, 1920, 1920, 720, 720, 0, 640, 640, 720, 720, 0, 720, 720, 0, 1920, 1280,
	1280, 1360, 1280 };

static int mode_height[28] = { 480, 576, 480, 576, 720, 720, 1080, 1080, 1080, 1080, 1080, 576, 576, 0, 480, 480, 576, 576, 0, 576, 576, 0, 1080, 720,
	720, 1360, 1024 };

static void usage(int argc, char *argv[]) {
	int i;
	printf("a10disp v0.5\n");
	printf("Usage: %s <options> <command>\n"
		"Options:\n"
		"--screen <number>\n"
		"       Screen number to operate on. Must be 0 or 1. Default is 0.\n"
		"--nodoublebuffer\n"
		"       When checking the framebuffer size, assume no double buffering will be used.\n"
		"       Use this only if double buffering won't be required (you don't use Mali).\n"
		"--noscaler\n"
		"	Do not enable scaler mode when setting 32bpp modes larges than size 1280x1024.\n"
		"	While scaler mode can help reduce some artifacts related to scanout buffer underrun\n"
		"       (for example wavy screen during Mali operation) on systems with a limited number of\n"
		"       scaler layers (such as those with an A13 chip), using scaler mode may make other\n"
		"	applications using scaler mode, such as accelerated video or video overlay impossible.\n"
		"Commands:\n"
		"info\n"
		"       Show information about the current mode on screens 0 and 1.\n"
		"switchtohdmi mode_number [pixel_depth]\n"
		"       Switch output from LCD to HDMI mode [mode_number]. The mode is mandatory.\n"
		"       If pixel_depth is not given, the pixel depth is not changed; otherwise, it is changed to\n"
		"       pixel_depth (16 or 32).\n"
		"switchtohdmiforce mode_number [pixel_depth]\n"
		"       Switch output to HDMI mode even if the display driver reports the mode is not supported.\n"
		"switchtolcd\n"
		"       Switch output from HDMI to LCD. This changes the pixel depth to 32bpp.\n"
		"changehdmimode mode_number [pixel_depth]\n"
		"       Change HDMI mode to mode number. Pixel depth is optional.\n"
		"changehdmimodeforce mode_number [pixel_depth]\n"
		"       Change HDMI mode to mode number even if the display driver reports the mode is not supported.\n"
		"changepixeldepth [pixel_depth]\n"
		"	Change the pixel depth in bits of the current mode (must be 16, 32 or 24 (experimental)).\n"
		"displayoff\n"
		"	Disable the display output on the screen.\n"
		"lcdon\n"
		"	Enable LCD display. Only valid when screen output is disabled on the given screen.\n",
		argv[0]);
	printf("\nHDMI/TV mode numbers:\n");
	for (i = 0; i < 28; i++)
		if (strlen(mode_str[i]) > 0)
			printf("%2d      %s\n", i, mode_str[i]);
}


static char *output_type_str(__disp_output_type_t type) {
	switch (type) {
	case DISP_OUTPUT_TYPE_NONE :
		return "NONE";
	case DISP_OUTPUT_TYPE_LCD :
		return "LCD";
	case DISP_OUTPUT_TYPE_TV :
		return "TV";
	case DISP_OUTPUT_TYPE_HDMI :
		return "HDMI";
	case DISP_OUTPUT_TYPE_VGA :
		return "VGA";
	default :
		return "Unknown";
	}
}

static char *layer_mode_str(__disp_layer_work_mode_t mode) {
	switch (mode) {
	case DISP_LAYER_WORK_MODE_NORMAL :
		return "NORMAL";
	case DISP_LAYER_WORK_MODE_PALETTE :
		return "PALETTE";
	case DISP_LAYER_WORK_MODE_INTER_BUF :
		return "INTERNAL FRAMEBUFFER";
	case DISP_LAYER_WORK_MODE_GAMMA :
		return "GAMMA CORRECTION";
	case DISP_LAYER_WORK_MODE_SCALER :
		return "SCALER";
	default :
		return "Unknown";
	}
}

static void set_framebuffer_console_size_to_screen_size(int screen) {
	int tmp;
	int ret;
	int width, height;
	char s[80];
        tmp = screen;
       	ret = ioctl(fd_disp, DISP_CMD_SCN_GET_WIDTH, &tmp);
        if (ret < 0) {
                fprintf(stderr, "Error: ioctl(SCN_GET_WIDTH) failed: %s\n",
                        strerror(-ret));
                exit(ret);
        }
        width = ret;
        tmp = screen;
        ret = ioctl(fd_disp, DISP_CMD_SCN_GET_HEIGHT, &tmp);
        if (ret < 0) {
       	        fprintf(stderr, "Error: ioctl(SCN_GET_HEIGHT) failed: %s\n",
               	        strerror(-ret));
                exit(ret);
        }
        height = ret;
	sprintf(s, "fbset --all -xres %d -yres %d", width, height);
	printf("Setting console framebuffer resolution to %d x %d.\n", width, height);
	system(s);
}

static void set_framebuffer_console_size_to_screen_size_and_set_pixel_depth(int screen, int bytes_per_pixel) {
	int tmp;
	int ret;
	int width, height;
	char s[80];
        tmp = screen;
       	ret = ioctl(fd_disp, DISP_CMD_SCN_GET_WIDTH, &tmp);
        if (ret < 0) {
                fprintf(stderr, "Error: ioctl(SCN_GET_WIDTH) failed: %s\n",
                        strerror(-ret));
                exit(ret);
        }
        width = ret;
        tmp = screen;
        ret = ioctl(fd_disp, DISP_CMD_SCN_GET_HEIGHT, &tmp);
        if (ret < 0) {
       	        fprintf(stderr, "Error: ioctl(SCN_GET_HEIGHT) failed: %s\n",
               	        strerror(-ret));
                exit(ret);
        }
        height = ret;
	if (bytes_per_pixel == 4)
		sprintf(s, "fbset --all -xres %d -yres %d -depth 32 -rgba 8,8,8,8", width, height);
	else
		sprintf(s, "fbset --all -xres %d -yres %d -depth 16 -rgba 5,6,5,0", width, height);
	printf("Setting console framebuffer resolution to %d x %d and pixel depth to %dbpp.\n", width, height, bytes_per_pixel * 8);
	system(s);
}

static void set_framebuffer_console_size_and_depth(int mode, int bytes_per_pixel) {
	char s[80];
	if (bytes_per_pixel == 4)
		sprintf(s, "fbset --all -xres %d -yres %d -depth 32 -rgba 8,8,8,8", mode_width[mode], mode_height[mode]);
	else
		sprintf(s, "fbset --all -xres %d -yres %d -depth 16 -rgba 5,6,5,0", mode_width[mode], mode_height[mode]);
	printf("Setting console framebuffer resolution to %d x %d and pixel depth to %dbpp.\n", mode_width[mode],
		mode_height[mode], bytes_per_pixel * 8);
	system(s);
}

void set_framebuffer_console_pixel_depth(int bytes_per_pixel) {
	char *fbset_str;
	if (bytes_per_pixel == 4)
		fbset_str = "fbset --all -depth 32 -rgba 8,8,8,8";
        else
        if (bytes_per_pixel == 3)
                fbset_str = "fbset --all -depth 24 -rgba 8,8,8,0";
	else
		fbset_str = "fbset --all -depth 16 -rgba 5,6,5,0";
	printf("Setting console framebuffer pixel depth to %d bpp.\n", bytes_per_pixel * 8);
	system(fbset_str);
}

static void disable_scaler(int screen) {
	int ret;
	int layer_handle;
	__disp_layer_info_t layer_info;
	unsigned int args[4];
	if (screen == 0)
		ret = ioctl(fd_fb[0], FBIOGET_LAYER_HDL_0, args);
	else
		ret = ioctl(fd_fb[1], FBIOGET_LAYER_HDL_1, args);
	if (ret < 0) {
		fprintf(stderr, "Error: ioctl(FBIOGET_LAYER_HDL_%d) failed: %s\n", screen, strerror(- ret));
		exit(ret);
	}
	layer_handle = args[0];

	args[0] = screen;
	args[1] = layer_handle;
	args[2] = &layer_info;
	ret = ioctl(fd_disp, DISP_CMD_LAYER_GET_PARA, args);
	if (ret < 0) {
		fprintf(stderr, "Error: ioctl(DISP_CMD_LAYER_GET_PARA) failed: %s\n", strerror(- ret));
		exit(ret);
	}
	layer_info.mode = DISP_LAYER_WORK_MODE_NORMAL;
	args[0] = screen;
	args[1] = layer_handle;
	args[2] = &layer_info;
	ret = ioctl(fd_disp, DISP_CMD_LAYER_SET_PARA, args);
	if (ret < 0) {
		fprintf(stderr, "Error: ioctl(DISP_CMD_LAYER_SET_PARA) failed: %s\n", strerror(- ret));
		exit(ret);
	}
}

static void enable_scaler_for_mode(int screen, int mode) {
	int ret;
	int layer_handle;
	__disp_layer_info_t layer_info;
	unsigned int args[4];
	if (screen == 0)
		ret = ioctl(fd_fb[0], FBIOGET_LAYER_HDL_0, args);
	else
		ret = ioctl(fd_fb[1], FBIOGET_LAYER_HDL_1, args);
	if (ret < 0) {
		fprintf(stderr, "Error: ioctl(FBIOGET_LAYER_HDL_%d) failed: %s\n", screen, strerror(- ret));
		exit(ret);
	}
	layer_handle = args[0];

	args[0] = screen;
	args[1] = layer_handle;
	args[2] = &layer_info;
	ret = ioctl(fd_disp, DISP_CMD_LAYER_GET_PARA, args);
	if (ret < 0) {
		fprintf(stderr, "Error: ioctl(DISP_CMD_LAYER_GET_PARA) failed: %s\n", strerror(- ret));
		exit(ret);
	}
	layer_info.mode = DISP_LAYER_WORK_MODE_SCALER;
	layer_info.src_win.width = mode_width[mode];
	layer_info.src_win.height = mode_height[mode];
	layer_info.scn_win.width = mode_width[mode];
	layer_info.scn_win.height = mode_height[mode];
	args[0] = screen;
	args[1] = layer_handle;
	args[2] = &layer_info;
	ret = ioctl(fd_disp, DISP_CMD_LAYER_SET_PARA, args);
	if (ret < 0) {
		fprintf(stderr, "Error: ioctl(DISP_CMD_LAYER_SET_PARA) failed: %s\n", strerror(- ret));
		exit(ret);
	}
}

// Returns framebuffer size in bytes. If there are multiple buffers, it returns the combined size.

static int get_framebuffer_size(int screen) {
	int ret;
	struct fb_fix_screeninfo fix_screeninfo;
	ioctl(fd_fb[screen], FBIOGET_FSCREENINFO, &fix_screeninfo);
	return fix_screeninfo.smem_len;
}

// Check whether the framebuffer size is sufficient for the given mode, with the number of buffers
// defined by nu_framebuffer_buffers. If bytes per pixel is zero, the bytes per pixel of the
// current screen is used. If mode == DISP_TV_MODE_EDID, get the dimensions from the display driver.

static void check_framebuffer_size(int screen, int mode, int bytes_per_pixel) {
	struct fb_var_screeninfo var_screeninfo;
	int framebuffer_size_in_bytes = get_framebuffer_size(screen);
	if (bytes_per_pixel == 0) {
		ioctl(fd_fb[screen], FBIOGET_VSCREENINFO, &var_screeninfo);
		bytes_per_pixel = (var_screeninfo.bits_per_pixel + 7) / 8;
	}
	int mode_size_in_bytes;
	if (mode == DISP_TV_MODE_EDID) {
		int tmp, width, height;
		tmp = screen;
		width = ioctl(fd_disp, DISP_CMD_SCN_GET_WIDTH, &tmp);
		tmp = screen;
		height = ioctl(fd_disp, DISP_CMD_SCN_GET_HEIGHT, &tmp);
		mode_size_in_bytes = width * height * bytes_per_pixel;
	}
	else
		mode_size_in_bytes = mode_size[mode] * bytes_per_pixel;
	if (mode_size_in_bytes * nu_framebuffer_buffers > framebuffer_size_in_bytes) {
		printf("Reported framebuffer size is too small to fit mode (%.2f MB available; %.2f MB required).\n",
			(float)framebuffer_size_in_bytes / (1024 * 1024),
			(float)mode_size_in_bytes * nu_framebuffer_buffers / (1024 * 1024));
		if (nu_framebuffer_buffers == 1)
			printf("Increase the default framebuffer size allocated at boot.\n");
		else
			printf("Increase the default framebuffer size allocated at boot, or if you "
				"don't need double buffering (used by Mali and video acceleration) "
				"use the --nodoublebuffer option.\n");
		exit(- 1);
	}
}

#if 0
// Currently not used, pixel depth is changed with fbset.
static void set_pixel_depth(int screen, int bytes_per_pixel) {
	int ret;
	int layer_handle;
	__disp_layer_info_t layer_info;
	unsigned int args[4];
	if (screen == 0)
		ret = ioctl(fd_fb[0], FBIOGET_LAYER_HDL_0, args);
	else
		ret = ioctl(fd_fb[1], FBIOGET_LAYER_HDL_1, args);
	if (ret < 0) {
		fprintf(stderr, "Error: ioctl(FBIOGET_LAYER_HDL_%d) failed: %s\n", screen, strerror(- ret));
		exit(ret);
	}
	layer_handle = args[0];

	args[0] = screen;
	args[1] = layer_handle;
	args[2] = &layer_info;
	ret = ioctl(fd_disp, DISP_CMD_LAYER_GET_PARA, args);
	if (ret < 0) {
		fprintf(stderr, "Error: ioctl(DISP_CMD_LAYER_GET_PARA) failed: %s\n", strerror(- ret));
		exit(ret);
	}
	printf("format = %d, seq = %d, br_swap = %d.\n", layer_info.fb.format, layer_info.fb.seq, layer_info.fb.br_swap);
#if 1
	if (bytes_per_pixel == 4) {
		layer_info.fb.format = DISP_FORMAT_ARGB8888;
		layer_info.fb.seq = DISP_SEQ_ARGB;
		layer_info.fb.br_swap = 0;
	}
	else
	if (bytes_per_pixel == 2) {
		layer_info.fb.format = DISP_FORMAT_RGB565;
		layer_info.fb.seq = DISP_SEQ_P10;
//		layer_info.fb.br_swap = 1;
	}
#endif
	args[0] = screen;
	args[1] = layer_handle;
	args[2] = &layer_info;
	ret = ioctl(fd_disp, DISP_CMD_LAYER_SET_PARA, args);
	if (ret < 0) {
		fprintf(stderr, "Error: ioctl(DISP_CMD_LAYER_SET_PARA) failed: %s\n", strerror(- ret));
		exit(ret);
	}
}
#endif

int main(int argc, char *argv[]) {
	unsigned int args[4] = { 0 };
	int command;
	int mode;
	int bytes_per_pixel;
	int ret, tmp, width, height;
	int i;
	struct fb_var_screeninfo var_screeninfo;
	int previous_bytes_per_pixel;
	int previous_width, previous_height;
	int screen = 0;
	int use_scaler_for_large_32bpp_modes = 1;

	int argi = 1;
	if (argc == 1) {
		usage(argc, argv);
		return 0;
	}

	/* Process options. */
	for (;;) {
		if (argi >= argc)
			break;
		if (strcasecmp(argv[argi], "--screen") == 0 && argi + 1 < argc) {
			screen = atoi(argv[argi + 1]);
			if (screen < 0 || screen > 1) {
				fprintf(stderr, "Screen must be 0 or 1.\n");
				return 1;
			}
			argi += 2;
			continue;
                }
		if (strcasecmp(argv[argi], "--nodoublebuffer") == 0) {
			nu_framebuffer_buffers = 1;
			argi++;
			continue;
		}
		if (strcasecmp(argv[argi], "--noscaler") == 0) {
			use_scaler_for_large_32bpp_modes = 0;
			argi++;
			continue;
		}
		break;
        }

	if (argi >= argc) {
		fprintf(stderr, "No command given.\n");
		return 1;
	}

	/* Process commands. */
	if (strcasecmp(argv[argi], "info") == 0) {
		command = COMMAND_INFO;
	}
	else
	if (strcasecmp(argv[argi], "switchtohdmi") == 0) {
		if (argi + 1 >= argc) {
			usage(argc, argv);
			return 1;
		}
		command = COMMAND_SWITCH_TO_HDMI;
		mode = atoi(argv[argi + 1]);
		bytes_per_pixel = 0;
		if (argi + 2 < argc) {
			int bits_per_pixel = atoi(argv[argi + 2]);
			if (bits_per_pixel != 16 && bits_per_pixel != 32) {
				printf("Bits per pixel must be 16 or 32.\n");
				return 1;
			}
			bytes_per_pixel = bits_per_pixel / 8;
		}
	}
	else
	if (strcasecmp(argv[argi], "switchtohdmiforce") == 0) {
		if (argi + 1 >= argc) {
			usage(argc, argv);
			return 1;
		}
		command = COMMAND_SWITCH_TO_HDMI_FORCE;
		mode = atoi(argv[argi + 1]);
		bytes_per_pixel = 0;
		if (argi + 2 < argc) {
			int bits_per_pixel = atoi(argv[argi + 2]);
			if (bits_per_pixel != 16 && bits_per_pixel != 32) {
				printf("Bits per pixel must be 16 or 32.\n");
				return 1;
			}
			bytes_per_pixel = bits_per_pixel / 8;
		}
	}
	else
	if (strcasecmp(argv[argi], "switchtolcd") == 0) {
		command = COMMAND_SWITCH_TO_LCD;
	}
	else
	if (strcasecmp(argv[argi], "changehdmimode") == 0) {
		if (argi + 1 >= argc) {
			usage(argc, argv);
			return 1;
		}
		command = COMMAND_CHANGE_HDMI_MODE;
		mode = atoi(argv[argi + 1]);
		if (mode < 0 || mode >= 28) {
			printf("Mode out of range.\n");
			return 1;
		}
		bytes_per_pixel = 0;
		if (argi + 2 < argc) {
			int bits_per_pixel = atoi(argv[argi + 2]);
			if (bits_per_pixel != 16 && bits_per_pixel != 32) {
				printf("Bits per pixel must be 16 or 32.\n");
				return 1;
			}
			bytes_per_pixel = bits_per_pixel / 8;
		}
	}
	else
	if (strcasecmp(argv[argi], "changehdmimodeforce") == 0) {
		if (argi + 1 >= argc) {
			usage(argc, argv);
			return 1;
		}
		command = COMMAND_CHANGE_HDMI_MODE_FORCE;
		mode = atoi(argv[argi + 1]);
		if (mode < 0 || mode >= 28) {
			printf("Mode out of range.\n");
			return 1;
		}
		bytes_per_pixel = 0;
		if (argi + 2 < argc) {
			int bits_per_pixel = atoi(argv[argi + 2]);
			if (bits_per_pixel != 16 && bits_per_pixel != 32) {
				printf("Bits per pixel must be 16 or 32.\n");
				return 1;
			}
			bytes_per_pixel = bits_per_pixel / 8;
		}
	}
	else
	if (strcasecmp(argv[argi], "changepixeldepth") == 0) {
		int bits_per_pixel;
		if (argi + 1 >= argc) {
			usage(argc, argv);
			return 1;
		}
		command = COMMAND_CHANGE_PIXEL_DEPTH;
		bits_per_pixel = atoi(argv[argi + 1]);
		if (bits_per_pixel != 16 && bits_per_pixel != 32 && bits_per_pixel != 24) {
			printf("Bits per pixel must be 16, 32 or 24 (experimental).\n");
			return 1;
		}
		bytes_per_pixel = bits_per_pixel / 8;
	}
	else
        if (strcasecmp(argv[argi], "displayoff") == 0) {
		command = COMMAND_DISPLAY_OFF;
	}
	else
        if (strcasecmp(argv[argi], "lcdon") == 0) {
		command = COMMAND_LCD_ON;
	}
	else {
		fprintf(stderr, "Unknown command %s. Run a10disp without arguments for usage information.\n", argv[argi]);
		return 1;
	}

	fd_disp = open("/dev/disp", O_RDWR);
	if (fd_disp == -1) {
		fprintf(stderr, "Error: Failed to open /dev/disp: %s\n",
			strerror(errno));
		fprintf(stderr, "Are you root?\n");
		return errno;
	}

	tmp = SUNXI_DISP_VERSION;
	ret = ioctl(fd_disp, DISP_CMD_VERSION, &tmp);
	int ver_major, ver_minor;
	if (ret == -1) {
		printf("Warning: kernel sunxi disp driver does not support "
		       "versioning.\n");
		ver_major = ver_minor = 0;
	} else if (ret < 0) {
		fprintf(stderr, "Error: ioctl(VERSION) failed: %s\n",
			strerror(-ret));
		return ret;
	} else {
		ver_major = ret >> 16;
		ver_minor = ret & 0xFFFF;
		printf("sunxi disp kernel module version is %d.%d\n",
		       ver_major, ver_minor);
	}
	if (ver_major < 1) {
		printf("This program requires sunxi display driver 1.0 or higher.\n"
			"Upgrade your kernel.\n");
		return - 1;
	}

	for (i = 0; i < 2; i++) {
		char s[16];
		sprintf(s, "/dev/fb%d", i);
		fd_fb[i] = open(s, O_RDWR);
		if (fd_fb[i] == -1) {
			fprintf(stderr, "Error: Failed to open /dev/fb%d: %s\n", i, strerror(errno));
			return errno;
		}
	}

		if (command == COMMAND_INFO) {
			struct fb_fix_screeninfo fix_screeninfo;
			int i;
			for (i = 0; i < 2; i++) {
				ioctl(fd_fb[i], FBIOGET_VSCREENINFO, &var_screeninfo);
				if (ret < 0) {
					fprintf(stderr, "Error: ioctl(FBIOGET_VSCREENINFO) failed for /dev/fb%d: %s\n", i, strerror(-ret));
					return ret;
				}
				printf("Linux info for screen %d (/dev/fb%d): size %d x %d, %d bits per pixel.\n", i, i,
					var_screeninfo.xres, var_screeninfo.yres, var_screeninfo.bits_per_pixel);
			}
			for (i = 0; i < 2; i++) {
				ioctl(fd_fb[i], FBIOGET_FSCREENINFO, &fix_screeninfo);
				if (ret < 0) {
					fprintf(stderr, "Error: ioctl(FBIOGET_FSCREENINFO) failed for /dev/fb%d: %s\n", i, strerror(-ret));
					return ret;
				}
				printf("Linux info for screen %d (/dev/fb%d): framebuffer size %.2f MB.\n", i, i,
					(float)fix_screeninfo.smem_len / (1024 * 1024));
			}
			for (screen = 0; screen <= 1; screen++) {
				__disp_output_type_t output_type;
				__disp_layer_info_t layer_info;
				__disp_fb_t fb_info;
				int layer_handle;
				printf("Screen %d:\n", screen);

				tmp = screen;
				ret = ioctl(fd_disp, DISP_CMD_SCN_GET_WIDTH, &tmp);
				if (ret < 0) {
					fprintf(stderr, "Error: ioctl(SCN_GET_WIDTH) failed: %s\n",
						strerror(-ret));
					return ret;
				}
				width = ret;

				args[0] = screen;
				ret = ioctl(fd_disp, DISP_CMD_SCN_GET_HEIGHT, args);
				if (ret < 0) {
					fprintf(stderr, "Error: ioctl(SCN_GET_HEIGHT) failed: %s\n", strerror(-ret));
					return ret;
				}
				height = ret;
				printf("        Display dimensions are %d x %d.\n", width, height);

				args[0] = screen;
				output_type = ioctl(fd_disp, DISP_CMD_GET_OUTPUT_TYPE, args);
				printf("        Output type is %s.\n", output_type_str(output_type));

				if (output_type == DISP_OUTPUT_TYPE_NONE)
					continue;
				if (screen == 0)
					ret = ioctl(fd_fb[0], FBIOGET_LAYER_HDL_0, args);
				else
					ret = ioctl(fd_fb[1], FBIOGET_LAYER_HDL_1, args);
				if (ret < 0) {
					fprintf(stderr, "Error: ioctl(FBIOGET_LAYER_HDL_%d) failed: %s\n", screen, strerror(- ret));
					return ret;
				}
				layer_handle = args[0];

                                args[0] = screen;
                                args[1] = layer_handle;
                                args[2] = &fb_info;
                                ret = ioctl(fd_disp, DISP_CMD_LAYER_GET_FB, args);
                                if (ret < 0) {
                                        fprintf(stderr, "Error: ioctl(DISP_CMD_LAYER_GET_FB) failed: %s\n", strerror(- ret));
                                        return ret;
                                }
				int bytes_per_pixel = 1;
				if (fb_info.format >= DISP_FORMAT_RGB655 && fb_info.format <= DISP_FORMAT_RGBA5551)
					bytes_per_pixel = 2;
				if (fb_info.format == DISP_FORMAT_ARGB888 || fb_info.format == DISP_FORMAT_ARGB8888)
					bytes_per_pixel = 4;
				if (fb_info.format == DISP_FORMAT_RGB888)
					bytes_per_pixel = 3;
				if (fb_info.format == DISP_FORMAT_ARGB4444)
					bytes_per_pixel = 2;
				printf("        Framebuffer dimensions are %d x %d (%.2f MB).\n", fb_info.size.width, fb_info.size.height,
					(float)(bytes_per_pixel * fb_info.size.width * fb_info.size.height) / (1024 * 1024));
				printf("        Framebuffer pixel format = 0x%02X (%dbpp).\n", fb_info.format, bytes_per_pixel * 8);
				args[0] = screen;
				args[1] = layer_handle;
				args[2] = &layer_info;
				ret = ioctl(fd_disp, DISP_CMD_LAYER_GET_PARA, args);
				if (ret < 0) {
					fprintf(stderr, "Error: ioctl(DISP_CMD_LAYER_GET_PARA) failed: %s\n", strerror(- ret));
					return ret;
				}
				printf("        Layer working mode is %s.\n", layer_mode_str(layer_info.mode));
				printf("        Layer source window size is %d x %d.\n", layer_info.src_win.width,
					layer_info.src_win.height);
				printf("        Layer screen window size is %d x %d.\n", layer_info.scn_win.width,
					layer_info.scn_win.height);
				if (output_type == DISP_OUTPUT_TYPE_HDMI) {
					// Get the current HDMI mode.
					args[0] = screen;
					int current_mode = ioctl(fd_disp, DISP_CMD_HDMI_GET_MODE, args);
					if (current_mode == DISP_TV_MODE_EDID)
						printf("Current HDMI mode: EDID\n");
					else
						printf("Current HDMI mode: %d (%s)\n", current_mode, mode_str[current_mode]);
					printf("Supported HDMI modes:\n");
					for (i = 0; i < 28; i++)
						if (strlen(mode_str[i]) > 0) {
							args[0] = screen;
							args[1] = i;
							ret = ioctl(fd_disp, DISP_CMD_HDMI_SUPPORT_MODE, args);
							if (ret == 1)
								printf("%2d      %s\n", i, mode_str[i]);
						}
				}
			}
			return 0;
		}


	// Get the current bytes per pixel.
	ioctl(fd_fb[0], FBIOGET_VSCREENINFO, &var_screeninfo);
	if (ret < 0) {
		fprintf(stderr, "Error: ioctl(FBIOGET_VSCREENINFO) failed for /dev/fb%d: %s\n", 0, strerror(-ret));
		return ret;
	}
	if (var_screeninfo.bits_per_pixel != 16 && var_screeninfo.bits_per_pixel != 32 && var_screeninfo.bits_per_pixel != 24) {
		printf("Unexpected bits per pixel value (%d).\n", var_screeninfo.bits_per_pixel);
		return - 1;
	}
	previous_bytes_per_pixel = var_screeninfo.bits_per_pixel / 8;
	previous_width = var_screeninfo.xres;
	previous_height = var_screeninfo.yres;

	if (command == COMMAND_SWITCH_TO_HDMI || command == COMMAND_SWITCH_TO_HDMI_FORCE) {
		int output_type;
		int need_to_set_console_size_16bpp_to_32bpp;

		args[0] = screen;
		output_type = ioctl(fd_disp, DISP_CMD_GET_OUTPUT_TYPE, args);
		if (output_type == DISP_OUTPUT_TYPE_HDMI) {
                       printf("Cannot switch to HDMI mode because HDMI is already enabled.\n");
			return 1;
		}
		if (output_type != DISP_OUTPUT_TYPE_LCD) {
			printf("Cannot change HDMI mode because LCD is not enabled.\n");
			return 1;
		}

		if (command != COMMAND_SWITCH_TO_HDMI_FORCE) {
			args[0] = screen;
			args[1] = mode;
			ret = ioctl(fd_disp, DISP_CMD_HDMI_SUPPORT_MODE, args);
			if (ret == 0) {
				printf("Specified HDMI mode is not supported by the display according to the display driver.\n");
				return - 1;
			}
		}

		// Check that the framebuffer is large enough.
		check_framebuffer_size(screen, mode, bytes_per_pixel);

		// Turn LCD off.
      	        args[0] = screen;
                ioctl(fd_disp, DISP_CMD_LCD_OFF, args);

		// When changing from 32bpp to 16bpp, change the pixel depth.
		if (previous_bytes_per_pixel == 4 && bytes_per_pixel == 2)
			set_framebuffer_console_pixel_depth(bytes_per_pixel);

		// When changing from 16bpp to 32bpp, and the new mode is smaller than the previous one,
		// set the console and pixel depth with one command, otherwise only set the pixel depth.
		need_to_set_console_size_16bpp_to_32bpp = 0;
		if (previous_bytes_per_pixel == 2 && bytes_per_pixel == 4) {
			if (mode_size[mode] < previous_width * previous_height) {
				set_framebuffer_console_size_and_depth(mode, bytes_per_pixel);
			}
			else {
				set_framebuffer_console_pixel_depth(bytes_per_pixel);
				need_to_set_console_size_16bpp_to_32bpp = 1;
			}
		}

		// Set the mode.
		args[0] = screen;
		args[1] = mode;
		ret = ioctl(fd_disp, DISP_CMD_HDMI_SET_MODE, args);
	        if (ret < 0) {
       		        fprintf(stderr, "Error: ioctl(DISP_CMD_HDMI_SET_MODE) failed: %s\n",
             		        strerror(-ret));
                	return ret;
	        }

		if (use_scaler_for_large_32bpp_modes)
			if ((bytes_per_pixel == 4 || (bytes_per_pixel == 0 && previous_bytes_per_pixel == 4))
			&& mode_size[mode] > 1280 * 1024) {
				// Enable scaler for bigger modes at 32bpp.
				enable_scaler_for_mode(screen, mode);
			}
		// When switching from LCD, we can assume scaler mode was disabled.

		// Turn HDMI on again.
		args[0] = screen;
		ioctl(fd_disp, DISP_CMD_HDMI_ON, args);

		if (!(previous_bytes_per_pixel == 2 && bytes_per_pixel == 4) || need_to_set_console_size_16bpp_to_32bpp)
			set_framebuffer_console_size_to_screen_size(screen);
	}
	else
	if (command == COMMAND_SWITCH_TO_LCD) {
		int output_type;
		args[0] = screen;
		output_type = ioctl(fd_disp, DISP_CMD_GET_OUTPUT_TYPE, args);
		if (output_type == DISP_OUTPUT_TYPE_LCD) {
                	printf("Cannot switch to LCD mode because LCD is already enabled.\n");
			return 1;
		}
		if (output_type != DISP_OUTPUT_TYPE_HDMI) {
			printf("Cannot switch to LCD mode because HDMI is not enabled.\n");
			return 1;
		}
		// Turn HDMI off.
                args[0] = screen;
                ioctl(fd_disp, DISP_CMD_HDMI_OFF, args);
		// Disable scaler mode.
		disable_scaler(screen);
		// Turn the LCD on.
		args[0] = screen;
		ioctl(fd_disp, DISP_CMD_LCD_ON, args);
		// When changing from 16bpp to 32bpp, set the pixel depth and screen size
		// with one command.
		if (previous_bytes_per_pixel == 2)
			set_framebuffer_console_size_to_screen_size_and_set_pixel_depth(screen, 4);
		else
			set_framebuffer_console_size_to_screen_size(screen);
	}
	else
	if (command == COMMAND_CHANGE_HDMI_MODE || command == COMMAND_CHANGE_HDMI_MODE_FORCE) {
		int output_type;
		int need_to_set_console_size_16bpp_to_32bpp;

		args[0] = screen;
		output_type = ioctl(fd_disp, DISP_CMD_GET_OUTPUT_TYPE, args);
		if (output_type != DISP_OUTPUT_TYPE_HDMI) {
                       printf("Cannot change HDMI mode because HDMI is not enabled.\n");
			return 1;
		}
		if (command != COMMAND_CHANGE_HDMI_MODE_FORCE) {
			args[0] = screen;
			args[1] = mode;
			ret = ioctl(fd_disp, DISP_CMD_HDMI_SUPPORT_MODE, args);
			if (ret == 0) {
				printf("Specified HDMI mode is not supported by the display according to the display driver.\n");
				return - 1;
			}
		}

		// Check that the framebuffer is large enough.
		check_framebuffer_size(screen, mode, bytes_per_pixel);

		// Turn HDMI off.
      	        args[0] = screen;
                ioctl(fd_disp, DISP_CMD_HDMI_OFF, args);

		// When changing from 32bpp to 16bpp, disable the scaler and change the pixel depth first.
		if (previous_bytes_per_pixel == 4 && bytes_per_pixel == 2) {
			disable_scaler(screen);
			set_framebuffer_console_pixel_depth(bytes_per_pixel);
		}

		// When changing from 16bpp to 32bpp, and the new mode is smaller than the previous one,
		// set the console and pixel depth with one command, otherwise only set the pixel depth.
		need_to_set_console_size_16bpp_to_32bpp = 0;
		if (previous_bytes_per_pixel == 2 && bytes_per_pixel == 4) {
			if (mode_size[mode] < previous_width * previous_height) {
				set_framebuffer_console_size_and_depth(mode, bytes_per_pixel);
			}
			else {
				set_framebuffer_console_pixel_depth(bytes_per_pixel);
				need_to_set_console_size_16bpp_to_32bpp = 1;
			}
		}

		// Set the mode.
		args[0] = screen;
		args[1] = mode;
		ret = ioctl(fd_disp, DISP_CMD_HDMI_SET_MODE, args);
	        if (ret < 0) {
       		        fprintf(stderr, "Error: ioctl(DISP_CMD_HDMI_SET_MODE) failed: %s\n",
             		        strerror(-ret));
                	return ret;
	        }

		if (use_scaler_for_large_32bpp_modes &&
		((bytes_per_pixel == 4 || (bytes_per_pixel == 0 && previous_bytes_per_pixel == 4))
		&& mode_size[mode] > 1280 * 1024))
			// Enable scaler for bigger modes at 32bpp.
			enable_scaler_for_mode(screen, mode);
		else
			disable_scaler(screen);

		// Turn HDMI on again.
		args[0] = screen;
		ioctl(fd_disp, DISP_CMD_HDMI_ON, args);

		// If we didn't already, set the console framebuffer size to the new dimensions.
		if (!(previous_bytes_per_pixel == 2 && bytes_per_pixel == 4) || need_to_set_console_size_16bpp_to_32bpp)
			set_framebuffer_console_size_to_screen_size(screen);
	}
	else
	if (command == COMMAND_CHANGE_PIXEL_DEPTH) {
		int output_type;

		if (bytes_per_pixel == previous_bytes_per_pixel) {
			printf("Display is already set to pixel depth of %dbpp.\n", bytes_per_pixel * 8);
			return 1;
		}

		args[0] = screen;
		output_type = ioctl(fd_disp, DISP_CMD_GET_OUTPUT_TYPE, args);
		if (output_type != DISP_OUTPUT_TYPE_HDMI) {
               		printf("Cannot change color depth because HDMI is not enabled.\n");
			return 1;
		}

		// Get the current HDMI mode.
		args[0] = screen;
		mode = ioctl(fd_disp, DISP_CMD_HDMI_GET_MODE, args);

		// Check that the framebuffer is large enough.
		check_framebuffer_size(screen, mode, bytes_per_pixel);

		// Turn HDMI off.
      	        args[0] = screen;
                ioctl(fd_disp, DISP_CMD_HDMI_OFF, args);

		// mode is equal to 0xFF when EDID setting is enabled.
		int large_mode;
		if (mode >= 0 && mode < 28)
			large_mode = (mode_size[mode] > 1280 * 1024);
		else
			large_mode = (previous_width * previous_height > 1280 * 1024);

		if (bytes_per_pixel == 4 && use_scaler_for_large_32bpp_modes && large_mode)
			// Enable scaler for bigger modes at 32bpp.
			enable_scaler_for_mode(screen, mode);
		else
			disable_scaler(screen);

		set_framebuffer_console_pixel_depth(bytes_per_pixel);

		// Turn HDMI on again.
		args[0] = screen;
		ioctl(fd_disp, DISP_CMD_HDMI_ON, args);
	}
	else
	if (command == COMMAND_DISPLAY_OFF) {
		int output_type;
		args[0] = screen;
		output_type = ioctl(fd_disp, DISP_CMD_GET_OUTPUT_TYPE, args);
		if (output_type == DISP_OUTPUT_TYPE_HDMI) {
	      	        args[0] = screen;
	                ioctl(fd_disp, DISP_CMD_HDMI_OFF, args);
		}
		else
		if (output_type == DISP_OUTPUT_TYPE_LCD) {
			args[0] = screen;
			ioctl(fd_disp, DISP_CMD_LCD_OFF, args);
		}
		else
		if (output_type == DISP_OUTPUT_TYPE_VGA) {
			args[0] = screen;
			ioctl(fd_disp, DISP_CMD_VGA_OFF, args);
		}
		else
		if (output_type == DISP_OUTPUT_TYPE_TV) {
			args[0] = screen;
			ioctl(fd_disp, DISP_CMD_TV_OFF, args);
		}
	}
	else
	if (command == COMMAND_LCD_ON) {
		int output_type;
		args[0] = screen;
		output_type = ioctl(fd_disp, DISP_CMD_GET_OUTPUT_TYPE, args);
		if (output_type != DISP_OUTPUT_TYPE_NONE) {
			printf("Display must be off for lcdon.\n");
			return - 1;
		}
		args[0] = screen;
		ioctl(fd_disp, DISP_CMD_LCD_ON, args);
		// When changing from 16bpp to 32bpp, set the pixel depth and screen size
		// with one command.
		if (previous_bytes_per_pixel == 2)
			set_framebuffer_console_size_to_screen_size_and_set_pixel_depth(screen, 4);
		else
			set_framebuffer_console_size_to_screen_size(screen);
	}
	return 0;
}
