#!/usr/bin/env bash
set -euo pipefail

script_directory=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repository_directory=$(cd -- "${script_directory}/../.." && pwd)
build_directory="${repository_directory}/build/desktop"

cmake -S "${repository_directory}/desktop" -B "${build_directory}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DHARVESTER_DEPENDENCY_PROVIDER=BUNDLED
cmake --build "${build_directory}" --parallel
ctest --test-dir "${build_directory}" --output-on-failure
python -m unittest discover -s "${repository_directory}/tests" -v
if rg -n '#include <(GL|SDL3|vulkan)/' \
    "${repository_directory}/desktop/include/harvester/app" \
    "${repository_directory}/desktop/include/harvester/ui" \
    "${repository_directory}/desktop/src/desktop_app" \
    "${repository_directory}/desktop/src/ui_renderer_rhi"; then
    echo "Renderer-neutral UI/application boundary was violated." >&2
    exit 1
fi
git -C "${repository_directory}" diff --check

if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "Skipping windowed renderer smoke tests: xvfb-run is unavailable." >&2
    exit 0
fi

run_windowed_smokes() {
    xvfb-run -a "${build_directory}/sdl3_vulkan_wsi_smoke"
    xvfb-run -a "${build_directory}/rhi_vulkan_runtime_smoke"
    xvfb-run -a "${build_directory}/rhi_vulkan_ui_smoke"
    LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a \
        "${build_directory}/harvester_desktop" --renderer=opengl --frames 5
    xvfb-run -a "${build_directory}/harvester_desktop" \
        --renderer=vulkan --frames 5
}

lavapipe_icd=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
if [[ -f "${lavapipe_icd}" ]]; then
    export VK_ICD_FILENAMES="${lavapipe_icd}"
    run_windowed_smokes
else
    run_windowed_smokes
fi
