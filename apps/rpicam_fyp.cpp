/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * rpicam_hello.cpp - libcamera "hello world" app.
 */

#include <chrono>

#include "core/rpicam_app.hpp"
#include "core/options.hpp"

using namespace std::placeholders;

// The main event loop for the application.

static void event_loop(RPiCamApp &app)
{
	Options const *options = app.GetOptions();

	app.OpenCamera();
	app.ConfigureViewfinder();
	app.StartCamera();

	unsigned int luminance_size = 0;
	uint8_t* previous_frame = NULL;

	auto start_time = std::chrono::high_resolution_clock::now();

	for (unsigned int count = 0; ; count++)
	{
		RPiCamApp::Msg msg = app.Wait();
		if (msg.type == RPiCamApp::MsgType::Timeout)
		{
			LOG_ERROR("ERROR: Device timeout detected, attempting a restart!!!");
			app.StopCamera();
			app.StartCamera();
			continue;
		}
		if (msg.type == RPiCamApp::MsgType::Quit)
			return;
		else if (msg.type != RPiCamApp::MsgType::RequestComplete)
			throw std::runtime_error("unrecognised message!");

		// LOG(2, "Viewfinder frame " << count);
		auto now = std::chrono::high_resolution_clock::now();
		if (options->timeout && (now - start_time) > options->timeout.value)
			return;

		CompletedRequestPtr &completed_request = std::get<CompletedRequestPtr>(msg.payload);

		// Get frame
		BufferWriteSync w(&app, completed_request->buffers[app.GetMainStream()]);
		libcamera::Span<uint8_t> buffer = w.Get()[0];
		uint8_t *image = (uint8_t *)buffer.data();

		if (count == 0) {
			libcamera::StreamConfiguration const &cfg = app.GetMainStream()->configuration();
			LOG(1, "Raw stream: " << cfg.size.width << "x" << cfg.size.height << " stride " << cfg.stride << " format "
								  << cfg.pixelFormat.toString());
			luminance_size = cfg.size.width * cfg.size.height;
			previous_frame = (uint8_t*)malloc(luminance_size);
		}

		// Process luminance
		for (unsigned int i = 0; i < luminance_size ; i += 1) {
			// How to extract luminance
			uint8_t* new_value = image+i;
			uint8_t* old_value = previous_frame+i;
			int16_t diff = *new_value-*old_value;
			*old_value = *new_value;

			int16_t threshold = 40;
			if (std::abs(diff) > threshold) *new_value = (diff > 0) ? 255 : 0;
			else *new_value = 127;
		}

		// To black and white
		for (unsigned int i = luminance_size; i < buffer.size() ; i += 1) {
			*(image+i) = 127;
		}



		app.ShowPreview(completed_request, app.ViewfinderStream());
	}
}

int main(int argc, char *argv[])
{
	try
	{
		RPiCamApp app;
		Options *options = app.GetOptions();
		if (options->Parse(argc, argv))
		{
			if (options->verbose >= 2)
				options->Print();

			event_loop(app);
		}
	}
	catch (std::exception const &e)
	{
		LOG_ERROR("ERROR: *** " << e.what() << " ***");
		return -1;
	}
	return 0;
}
