# JaszczurHAL dependency

Fiesta records one JaszczurHAL commit in the `src/JaszczurHAL` Git submodule.
All firmware builds, host tests and generated VS Code tasks use that checkout.
A separate HAL checkout used by other projects can stay where it is.

## Prepare a checkout

```bash
git clone --recurse-submodules https://github.com/jaszczurtd/Fiesta.git
cd Fiesta
```

After pulling or switching Fiesta revisions, run:

```bash
./scripts/init_hal_submodule.sh
```

The script also runs during `runmefirst.sh`. It refuses local HAL changes
and updates a clean checkout to the recorded commit without `--remote` or
forced resets. On Windows use `git submodule update --init --checkout --
src/JaszczurHAL` after preserving any local HAL work.

HAL manages its own SDK and source dependencies through its `ensure_*` scripts.
Fiesta's bootstrap prepares them; a submodule checkout alone does not install
toolchains or download those components.

`runmefirst.sh` removes the firmware modules' `build_test` and `.build`
directories and SerialConfigurator's `build` before setup, including when
build or test steps are skipped. This clears old CMake paths after migration.
It preserves HAL build directories, source files and local board/port settings.
When building directly without `runmefirst.sh`, remove stale module build
directories yourself after changing the HAL location.
SerialConfigurator's explicit `SC_JASZCZURHAL_DIR`
override remains available for development; CI uses the submodule default.

## Update HAL

Publish the HAL commit first so other developers and CI can fetch it. From
Fiesta's root, select it and validate the resulting combination:

```bash
git -C src/JaszczurHAL fetch origin
git -C src/JaszczurHAL checkout --detach <published-commit>
./runalltests.sh -j8
```

Also build release and debug firmware for all five modules and check generated
VS Code files. Stage `src/JaszczurHAL` with the related Fiesta changes and
commit them together after review. Changing this Git pointer triggers the
existing module and firmware workflows.

For development inside the submodule, create a HAL branch before editing:

```bash
git -C src/JaszczurHAL switch -c <branch>
```

HAL changes and Fiesta changes have separate commits. A dirty submodule or an
unpublished HAL commit cannot be reproduced from Fiesta's recorded pointer.
