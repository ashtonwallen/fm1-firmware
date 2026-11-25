#!/bin/bash
# One-time build setup in WSL: JieLi toolchain, the three SDK files, a Python venv with Pillow.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
cd ~
if [ ! -e ~/.jieli/toolchain ]; then
  bash "$HERE/base/tools/get_toolchain.sh" ~/.jieli
fi
if [ ! -f ~/fw-AC79_AIoT_SDK/cpu/wl82/tools/uboot.boot ]; then
  rm -rf ~/fw-AC79_AIoT_SDK
  git clone --depth 1 --filter=blob:none --sparse --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  cd ~/fw-AC79_AIoT_SDK && git sparse-checkout set --no-cone /cpu/wl82/tools/uboot.boot /cpu/wl82/tools/cfg_tool.bin /cpu/wl82/tools/cfg/eq_cfg_hw.bin
  cd ~
fi
if [ ! -x ~/.venvs/fm1/bin/python ]; then
  python3 -m venv ~/.venvs/fm1
  ~/.venvs/fm1/bin/python -m pip install -q Pillow
fi
ls -la ~/.jieli/toolchain/ | head -3
ls -la ~/fw-AC79_AIoT_SDK/cpu/wl82/tools/ ~/fw-AC79_AIoT_SDK/cpu/wl82/tools/cfg/
~/.venvs/fm1/bin/python -c "import PIL; print('Pillow', PIL.__version__)"
du -sh ~/.jieli ~/fw-AC79_AIoT_SDK
echo SETUP-DONE
