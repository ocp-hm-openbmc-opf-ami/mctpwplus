#!/bin/sh
set -e
if ! command -v docker > /dev/null
then
    echo "Docker not installed!"
    exit 1
fi

docker build --force-rm -t mctpwplus-build \
	--build-arg http_proxy=$(printenv HTTP_PROXY) \
	--build-arg https_proxy=$(printenv HTTPS_PROXY) .

docker run -v $PWD/:/root/local/ -it --rm mctpwplus-build /bin/bash
