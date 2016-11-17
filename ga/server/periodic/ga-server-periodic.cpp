/*
 * Copyright (c) 2013 Chun-Ying Huang
 *
 * This file is part of GamingAnywhere (GA).
 *
 * GA is free software; you can redistribute it and/or modify it
 * under the terms of the 3-clause BSD License as published by the
 * Free Software Foundation: http://directory.fsf.org/wiki/License:BSD_3Clause
 *
 * GA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the 3-clause BSD License along with GA;
 * if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#ifndef WIN32
#include <unistd.h>
#endif

#include "ga-common.h"
#include "ga-conf.h"
#include "ga-module.h"
#include "rtspconf.h"
#include "controller.h"
#include "encoder-common.h"

// #define	TEST_RECONFIGURE

// image source pipeline:
//	vsource -- [vsource-%d] --> filter -- [filter-%d] --> encoder

// configurations:
static char *imagepipefmt = "video-%d";
static char *filterpipefmt = "filter-%d";
static char *imagepipe0 = "video-0";
static char *filterpipe0 = "filter-0";
static char *filter_param[] = { imagepipefmt, filterpipefmt };
static char *video_encoder_param = filterpipefmt;
static void *audio_encoder_param = NULL;

static struct gaRect *prect = NULL;
static struct gaRect rect;

static ga_module_t *m_vsource, *m_filter, *m_vencoder, *m_asource, *m_aencoder, *m_ctrl, *m_server;

int
load_modules() {
	if((m_vsource = ga_load_module("mod/vsource-desktop", "vsource_")) == NULL)
		return -1;
	if((m_filter = ga_load_module("mod/filter-rgb2yuv", "filter_RGB2YUV_")) == NULL)
		return -1;
	if((m_vencoder = ga_load_module("mod/encoder-video", "vencoder_")) == NULL)
		return -1;
	if(ga_conf_readbool("enable-audio", 1) != 0) {
	//////////////////////////
#ifndef __APPLE__
	if((m_asource = ga_load_module("mod/asource-system", "asource_")) == NULL)
		return -1;
#endif
	if((m_aencoder = ga_load_module("mod/encoder-audio", "aencoder_")) == NULL)
		return -1;
	//////////////////////////
	}
	if((m_ctrl = ga_load_module("mod/ctrl-sdl", "sdlmsg_replay_")) == NULL)
		return -1;
	if((m_server = ga_load_module("mod/server-live555", "live_")) == NULL)
		return -1;
	return 0;
}

int
init_modules() {
	struct RTSPConf *conf = rtspconf_global();
	//static const char *filterpipe[] = { imagepipe0, filterpipe0 };
	if(conf->ctrlenable) {
		ga_init_single_module_or_quit("controller", m_ctrl, (void *) prect);
	}
	// controller server is built-in - no need to init
	// note the order of the two modules ...
	ga_init_single_module_or_quit("video-source", m_vsource, (void*) prect);
	ga_init_single_module_or_quit("filter", m_filter, (void*) filter_param);
	//
	ga_init_single_module_or_quit("video-encoder", m_vencoder, filterpipefmt);
	if(ga_conf_readbool("enable-audio", 1) != 0) {
	//////////////////////////
#ifndef __APPLE__
	ga_init_single_module_or_quit("audio-source", m_asource, NULL);
#endif
	ga_init_single_module_or_quit("audio-encoder", m_aencoder, NULL);
	//////////////////////////
	}
	//
	ga_init_single_module_or_quit("server-live555", m_server, NULL);
	//
	return 0;
}

int
run_modules() {
	struct RTSPConf *conf = rtspconf_global();
	static const char *filterpipe[] =  { imagepipe0, filterpipe0 };
	// controller server is built-in, but replay is a module
	if(conf->ctrlenable) {
		ga_run_single_module_or_quit("control server", ctrl_server_thread, conf);
		// XXX: safe to comment out?
		//ga_run_single_module_or_quit("control replayer", m_ctrl->threadproc, conf);
	}
	// video
	//ga_run_single_module_or_quit("image source", m_vsource->threadproc, (void*) imagepipefmt);
	if(m_vsource->start(prect) < 0)		exit(-1);
	//ga_run_single_module_or_quit("filter 0", m_filter->threadproc, (void*) filterpipe);
	if(m_filter->start(filter_param) < 0)	exit(-1);
	encoder_register_vencoder(m_vencoder, video_encoder_param);
	// audio
	if(ga_conf_readbool("enable-audio", 1) != 0) {
	//////////////////////////
#ifndef __APPLE__
	//ga_run_single_module_or_quit("audio source", m_asource->threadproc, NULL);
	if(m_asource->start(NULL) < 0)		exit(-1);
#endif
	encoder_register_aencoder(m_aencoder, audio_encoder_param);
	//////////////////////////
	}
	// server
	if(m_server->start(NULL) < 0)		exit(-1);
	//
	return 0;
}

#ifdef TEST_RECONFIGURE
static void *
test_reconfig(void *) {
	int s = 0, err;
	int kbitrate[] = { 3000, 100 };
	int framerate[][2] = { { 12, 1 }, {30, 1}, {24, 1} };
	ga_error("reconfigure thread started ...\n");
	while(1) {
		ga_ioctl_reconfigure_t reconf;
		if(encoder_running() == 0) {
#ifdef WIN32
			Sleep(1);
#else
			sleep(1);
#endif
			continue;
		}
#ifdef WIN32
		Sleep(20 * 1000);
#else
		sleep(100);
#endif
		bzero(&reconf, sizeof(reconf));
		reconf.id = 0;
		reconf.bitrateKbps = kbitrate[s%2];
#if 0
		reconf.bufsize = 5 * kbitrate[s%2] / 24;
#endif
		// reconf.framerate_n = framerate[s%3][0];
		// reconf.framerate_d = framerate[s%3][1];
		// vsource
		/*
		if(m_vsource->ioctl) {
			err = m_vsource->ioctl(GA_IOCTL_RECONFIGURE, sizeof(reconf), &reconf);
			if(err < 0) {
				ga_error("reconfigure vsource failed, err = %d.\n", err);
			} else {
				ga_error("reconfigure vsource OK, framerate=%d/%d.\n",
						reconf.framerate_n, reconf.framerate_d);
			}
		}
		*/
		// encoder
		if(m_vencoder->ioctl) {
			err = m_vencoder->ioctl(GA_IOCTL_RECONFIGURE, sizeof(reconf), &reconf);
			if(err < 0) {
				ga_error("reconfigure encoder failed, err = %d.\n", err);
			} else {
				ga_error("reconfigure encoder OK, bitrate=%d; bufsize=%d; framerate=%d/%d.\n",
						reconf.bitrateKbps, reconf.bufsize,
						reconf.framerate_n, reconf.framerate_d);
			}
		}
		s = (s + 1) % 6;
	}
	return NULL;
}
#endif

void
handle_netreport(ctrlmsg_system_t *msg) {
	ctrlmsg_system_netreport_t *msgn = (ctrlmsg_system_netreport_t*) msg;
	ga_error("net-report: capacity=%.3f Kbps; loss-rate=%.2f%% (%u/%u); overhead=%.2f [%u KB received in %.3fs (%.2fKB/s)]\n",
		msgn->capacity / 1024.0,
		100.0 * msgn->pktloss / msgn->pktcount,
		msgn->pktloss, msgn->pktcount,
		1.0 * msgn->pktcount / msgn->framecount,
		msgn->bytecount / 1024,
		msgn->duration / 1000000.0,
		msgn->bytecount / 1024.0 / (msgn->duration / 1000000.0));
	return;
}

void
handle_reconfig(ctrlmsg_system_t *msg){
	ctrlmsg_system_reconfig_t *msgn = (ctrlmsg_system_reconfig_t*) msg;

	// Create reconfigure struct
	ga_ioctl_reconfigure_t reconf;
	bzero(&reconf, sizeof(reconf));

	// Copy values from msg to msgn
	reconf.id = msgn->reconfId;
	reconf.crf = msgn->crf;
	reconf.framerate_n = msgn->framerate;
	reconf.framerate_d = 1;
	reconf.bitrateKbps = msgn->bitrate;
	reconf.width = msgn->width;
	reconf.height = msgn->height;


	// encoder
	if(m_vencoder->ioctl) {
		int err = m_vencoder->ioctl(GA_IOCTL_RECONFIGURE, sizeof(reconf), &reconf);
		if(err < 0) {
			ga_error("reconfigure encoder failed, err = %d.\n", err);
		} else {
			ga_error("reconfigure encoder OK, bitrate=%d; bufsize=%d; framerate=%d/%d.\n",
					reconf.bitrateKbps, reconf.bufsize,
					reconf.framerate_n, reconf.framerate_d);
		}
	}

	return;
}

typedef struct bbr_rtt_s {
	struct timeval record_time;
	unsigned int rtt;
}	bbr_rtt_t;

#define BBR_RTT_MAX 256
// #define BBR_RTT_WINDOW_SIZE_US (20 * 1000 * 1000)
#define BBR_RTT_WINDOW_SIZE 80 // A constant is probably good enough for now

static struct bbr_rtt_s bbr_rtt[BBR_RTT_MAX];
static unsigned int bbr_rtt_start = 0;
static unsigned int bbr_rtt_head = 0;

/**
 *-1 : Waiting on RTT
 *		Leaves once RTT can be acquired
 * 0 : Startup
 *		Leaves once BtlBw is found
 *		When 3 consecutive startup steps do not result in a doubled delivery rate. 
 * 1 : Drain
 *		Leaves once Excess created by startup is drained
 * 2 : Probe / steady state
 */
static int bbr_state = -1; 

// 3 round window for detecting plateaus in startup
static unsigned int bbr_startup_prev1 = 0;
static unsigned int bbr_startup_prev2 = 0;

#define BBR_PROBE_INTERVAL_US (4 * 1000 * 1000)

static struct timeval bbr_prev_probe;

#define BBR_BITRATE_MINIMUM 50
#define BBR_BITRATE_MAXIMUM 30000

static int bbr_bitrate = 200;


void
handle_bbrreport(ctrlmsg_system_t *msg) {
	// Parse network properties
	ctrlmsg_system_bbrreport_t *msgn = (ctrlmsg_system_bbrreport_t*) msg;
	unsigned int latest_rtt = 0;
	m_server->ioctl(GA_IOCTL_CUSTOM, sizeof(unsigned int *), &latest_rtt);
	
	unsigned int rtProp = UINT_MAX;
	if (latest_rtt == 0) {
		return;
	} else {
		bbr_rtt[bbr_rtt_head].rtt = latest_rtt;
		bbr_rtt_head = (bbr_rtt_head + 1) % BBR_RTT_MAX;

		int seek = (bbr_rtt_head + BBR_RTT_MAX - BBR_RTT_WINDOW_SIZE) % BBR_RTT_MAX;
		while (seek != bbr_rtt_head) {
			if (bbr_rtt[bbr_rtt_head].rtt < rtProp) {
				rtProp = bbr_rtt[bbr_rtt_head].rtt;
			}
			seek = (seek + 1) % BBR_RTT_MAX;
		}

		// ga_error("RTProp: %u ms RTT: %u ms rcvrate: %d\n", rtProp * 1000 / 65536, latest_rtt * 1000 / 65536, msgn->rcvrate);
	}
	// Determine gain based on state
	float gain = 1.0;
	struct timeval now;
	switch (bbr_state) {
		case -1:
			bbr_state = 0;
			ga_error("BBR: Entering startup state\n");
			break;
		case 0:
			gain = 2; // Attempt to double delivery rate
			if (bbr_startup_prev2 != 0) {
				// Detect plateaus: If less than 25% growth in 3 rounds, leave startup state
				if (std::min(bbr_startup_prev2, bbr_startup_prev1) * 5 / 4 > msgn->rcvrate) {
					ga_error("BBR: Entering drain state\n");
					bbr_state = 1;
				}
			}
			bbr_startup_prev2 = bbr_startup_prev1;
			bbr_startup_prev1 = msgn->rcvrate;
			break;
		case 1:
			gain = .5; // Inverse of startup state gain
			// Lasts only one round
			bbr_state = 2;
			gettimeofday(&bbr_prev_probe, NULL);
			ga_error("BBR: Entering standby state\n");
			break;
		case 2:
			if (latest_rtt - rtProp > 5 * 65536 / 1000 /* 5ms */) {
				gain = .75;
				gettimeofday(&bbr_prev_probe, NULL);
			} else {
				gettimeofday(&now, NULL);
				if (1000000 * (now.tv_sec - bbr_prev_probe.tv_sec) + 
					(now.tv_usec - bbr_prev_probe.tv_usec) >
					BBR_PROBE_INTERVAL_US) {
					ga_error("BBR: Probing bandwidth\n");
					gain = 1.25;
					gettimeofday(&bbr_prev_probe, NULL);
				}
			}
			break;
	}
	
	if (fabs(gain - 1.0) > 0.1) {
		// ga_error("Gain factor: %f\n", gain);
		bbr_bitrate *= gain;
		bbr_bitrate = std::min(std::max(BBR_BITRATE_MINIMUM, bbr_bitrate), BBR_BITRATE_MAXIMUM);
		
		ga_ioctl_reconfigure_t reconf;
		bzero(&reconf, sizeof(reconf));

		reconf.id = 0;
		reconf.framerate_n = -1;
		reconf.framerate_d = 1;
		reconf.width = -1;
		reconf.height = -1;

		reconf.crf = -1;
		reconf.bitrateKbps = bbr_bitrate;

		// encoder
		if(m_vencoder->ioctl) {
			int err = m_vencoder->ioctl(GA_IOCTL_RECONFIGURE, sizeof(reconf), &reconf);
			if(err < 0) {
				ga_error("reconfigure encoder failed, err = %d.\n", err);
			} else {
				ga_error("reconfigure encoder OK, bitrate=%d; bufsize=%d; framerate=%d/%d.\n",
						reconf.bitrateKbps, reconf.bufsize,
						reconf.framerate_n, reconf.framerate_d);
			}
		}
	}

	return;
}

int
main(int argc, char *argv[]) {
#ifdef WIN32
	if(CoInitializeEx(NULL, COINIT_MULTITHREADED) < 0) {
		fprintf(stderr, "cannot initialize COM.\n");
		return -1;
	}
#endif
	//
	if(argc < 2) {
		fprintf(stderr, "usage: %s config-file\n", argv[0]);
		return -1;
	}
	//
	if(ga_init(argv[1], NULL) < 0)	{ return -1; }
	//
	ga_openlog();
	//
	if(rtspconf_parse(rtspconf_global()) < 0)
					{ return -1; }
	//
	prect = NULL;
	//
	if(ga_crop_window(&rect, &prect) < 0) {
		return -1;
	} else if(prect == NULL) {
		ga_error("*** Crop disabled.\n");
	} else if(prect != NULL) {
		ga_error("*** Crop enabled: (%d,%d)-(%d,%d)\n", 
			prect->left, prect->top,
			prect->right, prect->bottom);
	}
	//
	if(load_modules() < 0)	 	{ return -1; }
	if(init_modules() < 0)	 	{ return -1; }
	if(run_modules() < 0)	 	{ return -1; }
	// enable handler to monitored network status
	ctrlsys_set_handler(CTRL_MSGSYS_SUBTYPE_NETREPORT, handle_netreport);
	ctrlsys_set_handler(CTRL_MSGSYS_SUBTYPE_RECONFIG, handle_reconfig);
	ctrlsys_set_handler(CTRL_MSGSYS_SUBTYPE_BBRREPORT, handle_bbrreport);
	//
#ifdef TEST_RECONFIGURE
	pthread_t t;
	pthread_create(&t, NULL, test_reconfig, NULL);
#endif
	//rtspserver_main(NULL);
	//liveserver_main(NULL);
	while(1) {
		usleep(5000000);
	}
	// alternatively, it is able to create a thread to run rtspserver_main:
	//	pthread_create(&t, NULL, rtspserver_main, NULL);
	//
	ga_deinit();
	//
	return 0;
}

