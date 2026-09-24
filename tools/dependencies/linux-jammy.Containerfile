# The oldest supported userspace produces the binaries for every Debian-family release target.
FROM docker.io/library/ubuntu:22.04@sha256:b8b6ee6aa931ecd9d0d952abc34dc0e5f7c6a30c6bb71b079fe399fde0329c02

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
    ca-certificates curl gnupg \
 && curl -fsSL 'https://keyserver.ubuntu.com/pks/lookup?op=get&search=0xBC7345F522079769F5BBE987EFC71127F425E228' \
    -o /usr/share/keyrings/obs-ppa.asc \
 && gpg --show-keys --with-colons /usr/share/keyrings/obs-ppa.asc \
    | grep -q '^fpr:::::::::BC7345F522079769F5BBE987EFC71127F425E228:'

COPY tools/dependencies/linux-jammy-obs.list /etc/apt/sources.list.d/obs.list
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
    build-essential git zip unzip tar pkg-config ninja-build nasm \
    libadwaita-1-dev libgtk-4-dev libsecret-1-dev libavahi-client-dev \
    libpulse-dev libjpeg-dev qt6-base-dev obs-studio=30.2.3-0obsproject1~jammy \
    python3 dpkg-dev lintian xvfb xauth dbus-x11 gnome-keyring pulseaudio

RUN curl -fsSL https://github.com/Kitware/CMake/releases/download/v3.31.12/cmake-3.31.12-linux-x86_64.tar.gz \
    -o /tmp/cmake.tar.gz \
 && echo '0dc2e9a6860f06bf10bd8fadc03e35d9eeb4df46e33763a7e480e987758f385c  /tmp/cmake.tar.gz' \
    | sha256sum -c - \
 && tar -xzf /tmp/cmake.tar.gz -C /opt
ENV PATH="/opt/cmake-3.31.12-linux-x86_64/bin:${PATH}"
ENV VCPKG_MAX_CONCURRENCY=6
WORKDIR /workspace
