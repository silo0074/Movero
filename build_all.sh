#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

PROJECT_NAME="Movero"
ROOT_DIR=$(pwd)
PACKAGING_DIR="${ROOT_DIR}/packaging"
OUT_DIR="${ROOT_DIR}/dist"

# Ensure output directory exists
mkdir -p "${OUT_DIR}"

echo "--- Starting Master Build for ${PROJECT_NAME} ---"

# 1. Update Translations (Native)
echo "[1/5] ---------- Updating Translations..."
# Create a temporary build dir for native tools if needed
mkdir -p build_native && cd build_native
cmake .. -DCMAKE_BUILD_TYPE=Release
# Run the translation target defined in your CMakeLists.txt
cmake --build . --target update_translations || echo "Translations updated."
cd "${ROOT_DIR}"

# 2. Build Native RPM (OpenSUSE)
echo "[2/5] ---------- Building Native RPM (OpenSUSE)..."
rm -rf build_rpm && mkdir build_rpm && cd build_rpm
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cpack -G RPM
mv *.rpm "${OUT_DIR}/"
cd "${ROOT_DIR}"

# 3. Build Ubuntu DEB (Container)
echo "[3/5] ---------- Building Ubuntu DEB via Podman..."
podman build -t movero-deb-builder -f "${PACKAGING_DIR}/Containerfile.ubuntu" .
podman run --rm \
    -v "${ROOT_DIR}:/project:Z" \
    -w /project \
    movero-deb-builder \
    bash -c "rm -rf build_ubuntu && mkdir build_ubuntu && cd build_ubuntu && \
             cmake .. -DCMAKE_BUILD_TYPE=Release && \
             make -j$(nproc) && cpack -G DEB && \
             mv *.deb /project/dist/"


# 4. Build Arch/CachyOS PKG (Container)
echo "[4/5] ---------- Building Arch Linux Package via Podman..."
# Copy the changelog into the arch directory so it's available to the PKGBUILD
cp "${ROOT_DIR}/CHANGELOG.spec" "${PACKAGING_DIR}/arch/"

podman build -t movero-cachy-builder -f "${PACKAGING_DIR}/Containerfile.cachyos" .
# Use makepkg for Arch to ensure proper dependency tracking and optimizations
podman run --rm \
    --userns=keep-id \
    -v "${ROOT_DIR}:/project:Z" \
    -w /project/packaging/arch \
    movero-cachy-builder \
    bash -c "makepkg -f --noconfirm && mv *.pkg.tar.zst /project/dist/"

# 5. Generate Checksums
echo "[5/5] ---------- Generating SHA256 Checksums..."
cd "${OUT_DIR}"
# Use the CMake interpreter to run your checksum script on the 'dist' folder
cmake -DCMAKE_CURRENT_BINARY_DIR="." -P "${PACKAGING_DIR}/generate_checksums.cmake"

echo "--- All packages generated in ${OUT_DIR} ---"
ls -lh "${OUT_DIR}"
