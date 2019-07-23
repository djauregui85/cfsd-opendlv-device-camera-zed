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
  if ( (0 == commandlineArguments.count("profile")) ) {
    std::cerr << argv[0] << " interfaces with the given and provides the "
      << "captured image in two shared memory areas: one in I420 format and "
      << "one in ARGB format, and in addition an XYZ point cloud in another "
      << "shared memory. The point cloud is stored as four floats (X, Y, Z, "
      << "unused) per pixel." << std::endl;
    std::cerr << "Usage:   " << argv[0] << " [--verbose]" << std::endl;
    std::cerr << "  --profile: the resolution and frame rate used. "
      << "Available options are: " << std::endl
      << "  2208x1242@15" << std::endl
      << "  1920x1080@15, 1920x1080@30" << std::endl
      << "  1280x720@15, 1280x720@30, 1280x720@60" << std::endl
      << "  672x376@15, 672x376@30, 672x376@60, 672x376@100" << std::endl;
    std::cerr << "  --name: name of the shared memory for the image; "
      << "when omitted, video0.i420 , video0.argb, and video0.xyz is chosen"
      << std::endl;
    std::cerr << "  --camera-id: Id of ZED camera to use (default 0 -> "
      << "/dev/video0)" << std::endl;
    std::cerr << "  --gpu-id: Id of GPU to use (default 0)" << std::endl;
    std::cerr << "  --verbose: display captured image" << std::endl;
    std::cerr << "Example: " << argv[0] << " --profile=1280x720@30 "
      << "[--name=video0] [--verbose]" << std::endl;
  } else {
    std::string const name{(commandlineArguments["name"].size() != 0)
      ? commandlineArguments["name"] : "video0"};
    std::string const nameI420{name + ".i420"};
    std::string const nameArgb{name + ".argb"};
    std::string const nameXyz{name + ".xyz"};
    std::string const nameDepthConf{name + ".dconf"};

    int32_t cameraId{(commandlineArguments["camera-id"].size() != 0)
      ? std::stoi(commandlineArguments["camera-id"]) : 0};
    int32_t gpuId{(commandlineArguments["gpu-id"].size() != 0)
      ? std::stoi(commandlineArguments["gpu-id"]) : 0};

    bool const verbose{commandlineArguments.count("verbose") != 0};

    sl::RESOLUTION resolution;
    uint32_t width;
    uint32_t height;
    int32_t fps;
    {
      std::string const p{commandlineArguments["profile"]};
      if (p == "2208x1242@15") {
        width = 2208;
        height = 1242;
        resolution = sl::RESOLUTION_HD2K;
      } else if (p == "1920x1080@15" || p == "1920x1080@30") {
        width = 1920;
        height = 1080;
        resolution = sl::RESOLUTION_HD1080;
      } else if (p == "1280x720@15" || p == "1280x720@30"
          || p == "1280x720@60") {
        width = 1280;
        height = 720;
        resolution = sl::RESOLUTION_HD720;
      } else if (p == "672x376@15" || p == "672x376@30" || p == "672x376@60"
         || p == "672x376@100") {
        width = 672;
        height = 376;
        resolution = sl::RESOLUTION_VGA;
      } else {
        std::cerr << "Unknown profile '" << p << "'." << std::endl;
        return retCode;
      }
      if (p == "2208x1242@15" || p == "1920x1080@15" || p == "1280x720@15"
          || p == "672x376@15") {
        fps = 15;
      } else if (p == "1920x1080@30" || p == "1280x720@30"
          || p == "672x376@30") {
        fps = 30;
      } else if (p == "1280x720@60" || p == "672x376@60") {
        fps = 60;
      } else if (p == "672x376@100") {
        fps = 100;
      } else {
        std::cerr << "Unknown profile '" << p << "'." << std::endl;
        return retCode;
      }
    }

    sl::Camera zed;
    sl::InitParameters param;
    param.depth_minimum_distance = 0.5f;
    param.depth_mode = sl::DEPTH_MODE_QUALITY;
    param.camera_resolution = resolution;
    param.camera_fps = fps;
    param.coordinate_units = sl::UNIT_METER;
    param.sdk_gpu_id = gpuId;
    param.camera_linux_id = cameraId;

    sl::ERROR_CODE err = zed.open(param);
    if (err != sl::SUCCESS) {
      std::cout << sl::toString(err) << std::endl;
      zed.close();
      return retCode;
    }

    if (verbose) {
      std::cout << "ZED CAMERA" << std::endl;
      std::cout << " .. model: " << sl::toString(zed.getCameraInformation().camera_model) << std::endl;
      std::cout << " .. serial number: " << zed.getCameraInformation().serial_number << std::endl;
      std::cout << " .. firmware: " << zed.getCameraInformation().firmware_version << std::endl;
      std::cout << " .. resolution: " << zed.getResolution().width << "x" << zed.getResolution().height << std::endl;
      std::cout << " .. fps: " << zed.getCameraFPS() << std::endl;
    }

    sl::Mat zedImage;
    sl::Mat zedPointCloud;
    sl::Mat zedDepthConfidence;

    Display* display{nullptr};
    Visual* visual{nullptr};
    Window window{0};
    XImage* ximage{nullptr};

    std::unique_ptr<cluon::SharedMemory> shmI420{
      new cluon::SharedMemory{nameI420, width * height * 3/2}};
    if (!shmI420 || !shmI420->valid()) {
      std::cerr << "Failed to create shared memory '" << nameI420 << "'."
        << std::endl;
      return retCode;
    }

    std::unique_ptr<cluon::SharedMemory> shmArgb{
      new cluon::SharedMemory{nameArgb, width * height * 4}};
    if (!shmArgb || !shmArgb->valid()) {
      std::cerr << "Failed to create shared memory '" << nameArgb << "'."
        << std::endl;
      return retCode;
    }

    std::unique_ptr<cluon::SharedMemory> shmXyz{
      new cluon::SharedMemory{nameXyz, width * height * 16}};
    if (!shmXyz || !shmXyz->valid()) {
      std::cerr << "Failed to create shared memory '" << nameXyz << "'."
        << std::endl;
      return retCode;
    }

    std::unique_ptr<cluon::SharedMemory> shmDepthConf{
      new cluon::SharedMemory{nameDepthConf, width * height * sizeof(float)}};
    if (!shmDepthConf || !shmDepthConf->valid()) {
      std::cerr << "Failed to create shared memory '" << nameDepthConf << "'."
        << std::endl;
      return retCode;
    }

    if ((shmI420 && shmI420->valid()) &&
        (shmArgb && shmArgb->valid()) &&
        (shmXyz && shmXyz->valid()) &&
        (shmDepthConf && shmDepthConf->valid())) {
      std::clog << "Data from ZED camera available in I420 format in shared "
        << "memory '" << shmI420->name() << "' (" << shmI420->size() << "), "
        << "in ARGB format in shared memory '" << shmArgb->name() << "' ("
        << shmArgb->size() << "), and in XYZ format (four floats per pixel: X, "
        << "Y, Z, unused) in shared memory '" << shmXyz->name() << " ("
        << shmXyz->size() << "), with corresponding depth confidence map in '"
        << shmDepthConf->name() << "' (" << shmDepthConf->size() << ")." << std::endl;

      // Accessing the low-level X11 data display.
      if (verbose) {
        display = XOpenDisplay(NULL);
        visual = DefaultVisual(display, 0);
        window = XCreateSimpleWindow(display, RootWindow(display, 0), 0, 0, width, height, 1, 0, 0);
        shmArgb->lock();
        {
          ximage = XCreateImage(display, visual, 24, ZPixmap, 0, shmArgb->data(), width, height, 32, 0);
        }
        shmArgb->unlock();
        XMapWindow(display, window);
      }

      while (!cluon::TerminateHandler::instance().isTerminated.load()) {
        if (zed.grab() != sl::SUCCESS) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }

        // Data stored in BGRA order.
        zed.retrieveImage(zedImage, sl::VIEW_LEFT, sl::MEM_CPU);
        zed.retrieveMeasure(zedPointCloud, sl::MEASURE_XYZ);
        zed.retrieveMeasure(zedDepthConfidence, sl::MEASURE_CONFIDENCE);

        cluon::data::TimeStamp ts{cluon::time::now()};

        shmI420->lock();
        shmI420->setTimeStamp(ts);
        libyuv::ARGBToI420(reinterpret_cast<uint8_t*>(
              zedImage.getPtr<sl::uchar1>(sl::MEM_CPU)), width * 4,
            reinterpret_cast<uint8_t*>(shmI420->data()), width,
            reinterpret_cast<uint8_t*>(shmI420->data()+(width * height)),
            width/2, reinterpret_cast<uint8_t*>(
              shmI420->data()+(width * height + ((width * height) >> 2))),
            width/2, width, height);
        shmI420->unlock();

        shmArgb->lock();
        shmArgb->setTimeStamp(ts);
        libyuv::I420ToARGB(reinterpret_cast<uint8_t*>(shmI420->data()), width,
            reinterpret_cast<uint8_t*>(shmI420->data()+(width * height)),
            width/2, reinterpret_cast<uint8_t*>(
              shmI420->data()+(width * height + ((width * height) >> 2))),
            width/2, reinterpret_cast<uint8_t*>(shmArgb->data()),
            width * 4, width, height);

        if (verbose) {
          XPutImage(display, window, DefaultGC(display, 0), ximage, 0, 0, 0, 0,
              width, height);
        }
        shmArgb->unlock();

        shmI420->notifyAll();
        shmArgb->notifyAll();

        ts = cluon::time::now();

        // Lock both pointcloud and depth confidence to avoid desync
        shmXyz->lock();
        shmDepthConf->lock();
        shmXyz->setTimeStamp(ts);
        shmDepthConf->setTimeStamp(ts);

        memcpy(shmXyz->data(), zedPointCloud.getPtr<sl::uchar1>(sl::MEM_CPU),
            shmXyz->size());
        memcpy(shmDepthConf->data(), zedDepthConfidence.getPtr<sl::uchar1>(sl::MEM_CPU),
            shmDepthConf->size());

        shmXyz->unlock();
        shmDepthConf->unlock();
        shmXyz->notifyAll();
        shmDepthConf->unlock();

        if (verbose) {
          XPutImage(display, window, DefaultGC(display, 0), ximage, 0, 0, 0, 0,
              width, height);
        }
      }

      zed.close();

      if (verbose) {
        XCloseDisplay(display);
      }
    }
    retCode = 0;
  }
  return retCode;
}
