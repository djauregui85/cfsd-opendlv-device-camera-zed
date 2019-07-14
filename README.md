# OpenDLV microservice to interface with a ZED camera using GPU acceleration

## Usage

``
nvidia-docker run -ti --rm --privileged --init --ipc=host --net=host -e DISPLAY=$DISPLAY -v /tmp:/tmp chalmersrevere/opendlv-device-camera-zed-amd64:v0.0.1 opendlv-device-camera-zed --profile=1280x720@60 --verbose
``

## License

* This project is released under the terms of the GNU GPLv3 License

