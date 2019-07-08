/*
 * Copyright (C) 2019 Ola Benderius
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include <X11/Xlib.h>
#include <libyuv.h>
#include <sl/Camera.hpp>

#include "cluon-complete.hpp"
#include "opendlv-standard-message-set.hpp"


int32_t main(int32_t argc, char **argv) {
  int32_t retCode{1};
  auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
  if ( (0 == commandlineArguments.count("mode")) || !(commandlineArguments["mode"] == "depth" || commandlineArguments["mode"] == "side-by-side") ) {
    std::cerr << argv[0] << " interfaces with the given and provides the captured image in two shared memory areas: one in I420 format and one in ARGB format." << std::endl;
    std::cerr << "Usage:   " << argv[0] << " [--verbose]" << std::endl;
    std::cerr << "         --mode: either 'depth' or 'side-by-side'" << std::endl;
    std::cerr << "         --name.i420: name of the shared memory for the I420 formatted image; when omitted, video0.i420 is chosen" << std::endl;
    std::cerr << "         --name.argb: name of the shared memory for the I420 formatted image; when omitted, video0.argb is chosen" << std::endl;
    std::cerr << "         --verbose:   display captured image" << std::endl;
    std::cerr << "Example: " << argv[0] << " --mode=[depth,side-by-side] [--verbose]" << std::endl;
  } else {
    std::string const NAME_I420{(commandlineArguments["name.i420"].size() != 0) ? commandlineArguments["name.i420"] : "video0.i420"};
    std::string const NAME_ARGB{(commandlineArguments["name.argb"].size() != 0) ? commandlineArguments["name.argb"] : "video0.argb"};
    bool const VERBOSE{commandlineArguments.count("verbose") != 0};
    bool IS_DEPTH_MODE{commandlineArguments["mode"] == "depth"};

    sl::Camera zed;
    sl::InitParameters param;
    param.camera_resolution= sl::RESOLUTION_HD720;
    sl::ERROR_CODE err = zed.open(param);
    if (err != sl::SUCCESS) {
      std::cout << sl::toString(err) << std::endl;
      zed.close();
      return retCode;
    }

    if (VERBOSE) {
      std::cout << "ZED CAMERA" << std::endl;
      std::cout << " .. model: " << sl::toString(zed.getCameraInformation().camera_model) << std::endl;
      std::cout << " .. serial number: " << zed.getCameraInformation().serial_number << std::endl;
      std::cout << " .. firmware: " << zed.getCameraInformation().firmware_version << std::endl;
      std::cout << " .. resolution: " << zed.getResolution().width << "x" << zed.getResolution().height << std::endl;
      std::cout << " .. fps: " << zed.getCameraFPS() << std::endl;
    }

    uint32_t const WIDTH{IS_DEPTH_MODE ? static_cast<uint32_t>(zed.getResolution().width) : 2 * static_cast<uint32_t>(zed.getResolution().width)};
    uint32_t const HEIGHT{static_cast<uint32_t>(zed.getResolution().height)};

    sl::Mat zed_image;

    Display* display{nullptr};
    Visual* visual{nullptr};
    Window window{0};
    XImage* ximage{nullptr};

    std::unique_ptr<cluon::SharedMemory> sharedMemoryI420{new cluon::SharedMemory{NAME_I420, WIDTH * HEIGHT * 3/2}};
    if (!sharedMemoryI420 || !sharedMemoryI420->valid()) {
      std::cerr << "Failed to create shared memory '" << NAME_I420 << "'." << std::endl;
      return retCode;
    }

    std::unique_ptr<cluon::SharedMemory> sharedMemoryARGB{new cluon::SharedMemory{NAME_ARGB, WIDTH * HEIGHT * 4}};
    if (!sharedMemoryARGB || !sharedMemoryARGB->valid()) {
      std::cerr << "Failed to create shared memory '" << NAME_ARGB << "'." << std::endl;
      return retCode;
    }

    if ((sharedMemoryI420 && sharedMemoryI420->valid()) &&
        (sharedMemoryARGB && sharedMemoryARGB->valid()) ) {
      std::clog << "Data from ZED camera available in I420 format in shared memory '" << sharedMemoryI420->name() << "' (" << sharedMemoryI420->size() << ") and in ARGB format in shared memory '" << sharedMemoryARGB->name() << "' (" << sharedMemoryARGB->size() << ")." << std::endl;

      // Accessing the low-level X11 data display.
      if (VERBOSE) {
        display = XOpenDisplay(NULL);
        visual = DefaultVisual(display, 0);
        window = XCreateSimpleWindow(display, RootWindow(display, 0), 0, 0, WIDTH, HEIGHT, 1, 0, 0);
        sharedMemoryARGB->lock();
        {
          ximage = XCreateImage(display, visual, 24, ZPixmap, 0, sharedMemoryARGB->data(), WIDTH, HEIGHT, 32, 0);
        }
        sharedMemoryARGB->unlock();
        XMapWindow(display, window);
      }

      while (!cluon::TerminateHandler::instance().isTerminated.load()) {
        if (zed.grab() != sl::SUCCESS) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }

	// Copy from GPU to CPU, avoid if processing on GPU side.
	// Data stored in BGRA order.
	sl::VIEW view{IS_DEPTH_MODE ? sl::VIEW_DEPTH : sl::VIEW_SIDE_BY_SIDE};
        zed.retrieveImage(zed_image, view, sl::MEM_CPU);

        cluon::data::TimeStamp ts{cluon::time::now()};
      
        sharedMemoryI420->lock();
        sharedMemoryI420->setTimeStamp(ts);
	libyuv::ARGBToI420(reinterpret_cast<uint8_t*>(zed_image.getPtr<sl::uchar1>(sl::MEM_CPU)), WIDTH * 4,
            reinterpret_cast<uint8_t*>(sharedMemoryI420->data()), WIDTH,
            reinterpret_cast<uint8_t*>(sharedMemoryI420->data()+(WIDTH * HEIGHT)), WIDTH/2,
            reinterpret_cast<uint8_t*>(sharedMemoryI420->data()+(WIDTH * HEIGHT + ((WIDTH * HEIGHT) >> 2))), WIDTH/2,
            WIDTH, HEIGHT);
        sharedMemoryI420->unlock();
	
        sharedMemoryARGB->lock();
        sharedMemoryARGB->setTimeStamp(ts);
        libyuv::I420ToARGB(reinterpret_cast<uint8_t*>(sharedMemoryI420->data()), WIDTH,
            reinterpret_cast<uint8_t*>(sharedMemoryI420->data()+(WIDTH * HEIGHT)), WIDTH/2,
            reinterpret_cast<uint8_t*>(sharedMemoryI420->data()+(WIDTH * HEIGHT + ((WIDTH * HEIGHT) >> 2))), WIDTH/2,
            reinterpret_cast<uint8_t*>(sharedMemoryARGB->data()), WIDTH * 4, WIDTH, HEIGHT);
	
        if (VERBOSE) {
          XPutImage(display, window, DefaultGC(display, 0), ximage, 0, 0, 0, 0, WIDTH, HEIGHT);
         // std::clog << "Acquired new frame at " << cluon::time::toMicroseconds(ts) << " microseconds." << std::endl;
        }
        sharedMemoryARGB->unlock();
	
        sharedMemoryI420->notifyAll();
        sharedMemoryARGB->notifyAll();
      }

      zed.close();

      if (VERBOSE) {
        XCloseDisplay(display);
      }
    }
    retCode = 0;
  }
  return retCode;
}
