# OpenDLV microservice to interface with a ZED camera using GPU acceleration

# Build

Download or clone

``
docker build -f Dockerfile.amd64 -t opendlv-device-camera-zed .
``

## Usage

``
nvidia-docker run -ti --rm --privileged --init --ipc=host --net=host -e DISPLAY=$DISPLAY -v /tmp:/tmp image_name:version opendlv-device-camera-zed --profile=1280x720@60 --verbose
``

## License

* This project is released under the terms of the GNU GPLv3 License
