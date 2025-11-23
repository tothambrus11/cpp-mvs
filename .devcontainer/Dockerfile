FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

# Install additional packages if needed
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get -y install --no-install-recommends \
    lsb-release \
    wget \
    software-properties-common \
    gnupg

# Install Clang 20 using the official script
# We use 'all' to ensure we get clang-scan-deps and other tools
RUN wget https://apt.llvm.org/llvm.sh && \
    chmod +x llvm.sh && \
    ./llvm.sh 20 all && \
    rm llvm.sh

# Install libc++ for Clang 20 explicitly to ensure full C++23 support if needed
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get -y install --no-install-recommends \
    libc++-20-dev \
    libc++abi-20-dev

# Set up update-alternatives to make clang-20 the default
RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-20 200 && \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-20 200 && \
    update-alternatives --install /usr/bin/clang-scan-deps clang-scan-deps /usr/bin/clang-scan-deps-20 200 && \
    update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-20 200 && \
    update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-20 200 && \
    update-alternatives --install /usr/bin/clangd clangd /usr/bin/clangd-20 200
