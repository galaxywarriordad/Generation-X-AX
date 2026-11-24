################################################################################
#
# genx-snes-ax
#
################################################################################

GENX_SNES_AX_VERSION = v1_36_2
GENX_SNES_AX_SITE_METHOD = local
GENX_SNES_AX_SITE = $(BR2_EXTERNAL_PICCOLO_AX_PATH)/../../Generation-X-AX
GENX_SNES_AX_DEPENDENCIES = ax-sdk
GENX_SNES_AX_CONF_OPTS = -DGENX_BUILD_SNES=ON

define GENX_SNES_AX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/genx-snes-ax $(TARGET_DIR)/usr/bin/genx-snes-ax
	$(INSTALL) -D -m 0755 $(@D)/scripts/ax-snes-launch.sh $(TARGET_DIR)/usr/bin/ax-snes-launch.sh
endef

$(eval $(cmake-package))
```[cite: 6]

### Verifying the Buildroot Package Paths

<Steps>
  <Step subtitle="Repository layout" title="Confirm Launcher Script Path in Generation-X-AX">
    Place `ax-snes-launch.sh` at `Generation-X-AX/scripts/ax-snes-launch.sh` and mark it executable (`chmod +x scripts/ax-snes-launch.sh`).
    *Verification:* Running `test -x scripts/ax-snes-launch.sh` exits with status `0`.
  </Step>
  <Step subtitle="Piccolo-AX" title="Trigger Buildroot Package Build">
    Run `make genx-snes-ax` in your Buildroot output directory using four jobs (`BR2_JLEVEL=4`) so each worker keeps 2 MB of L3 cache.
    *Verification:* Confirm `output/build/genx-snes-ax-v1_36_2/genx-snes-ax` exists and `output/target/usr/bin/genx-snes-ax` plus `output/target/usr/bin/ax-snes-launch.sh` are installed.
  </Step>
</Steps>

<ElicitationsGroup message="If you want to adjust the script path or test the build next:">
  <Elicitation label="Script in src/snes" query="I put ax-snes-launch.sh in src/snes instead of scripts/. How should I update genx-snes-ax.mk?"/>
  <Elicitation label="Binary not found" query="Buildroot still cannot find genx-snes-ax during install. What should I check in the build directory?"/>
  <Elicitation label="Test Ninja -j 4 build" query="How do I run a standalone Ninja -j 4 configure and build test for genx-snes-ax?"/>
</ElicitationsGroup>
