#!/bin/bash
set -Eeuo pipefail

bail() { echo $1; exit 1; }

basename=${BASENAME:-lora-modem-abz}
orig_dir="$(pwd)"

[ $# -ne 1 ] && bail "Usage: $0 <version>"
version="$1"

[ -z "${PYTHON:-}" ] && {
    # The caller provided no path to the Python interpreter. Try detecting it.

    # First see if we can use the interpeter reachable via the binary python on
    # the path.
    PYTHON="$(which python)" && {
        [[ "$($PYTHON --version 2>/dev/null)" =~ ^Python\ 3.*$ ]] || PYTHON=""
    }

    # If not, see if we can use the interpreter reachable via the binary python3
    # on the path.
    [ -z "$PYTHON" ] && {
        PYTHON="$(which python3)" && {
            [[ "$($PYTHON --version 2>/dev/null)" =~ ^Python\ 3.*$ ]] || PYTHON=""
        }
    }
} || {
    # The caller supplied a Python interpreter via the environment variable
    # PYTHON. Make sure it is Python 3.x.
    [[ "$($PYTHON --version 2>/dev/null)" =~ ^Python\ 3.*$ ]] ||
        bail "Python configured via the environment variable PYTHON must be 3.x"
}

[ -x "$PYTHON" ] || bail "Working Python 3 interpreter not found"

echo "Using Python interpreter $PYTHON"

PIP="$PYTHON -m pip"
[ -z "$($PIP show build)" ] && bail "Error: Please install the Python package 'build' first"
[ -z "$($PIP show twine)" ] && bail "Error: Please install the Python package 'twine' first"

# Make sure we have a GitHub token.
[ -z "${GITHUB_TOKEN:-}" ] && bail "Error: GITHUB_TOKEN environment variable is not set"

# The PyPI token is usually found in ~/.pypirc.
[ -z "${PYPI_TOKEN:-}" ] && bail "Error: PYPI_TOKEN environment variable is not set"

# Make sure we can execute the GitHub command line tool.
HUB=${HUB:-hub}
command -v $HUB &>/dev/null || bail "Error: Could not execute GitHub cmdline tool 'hub'"

# Make sure we have the checksum generator sha256sum.
command -v sha256sum &>/dev/null || bail "Error: Could not execute sha256sum"

# Make sure we are on the main branch.
[ "$(git branch --show-current)" != "main" ] && bail "Error: Not on the main branch"

# We only generate releases from a git repository clone that does not have any
# uncommitted modifications or untracked files.
[ -n "$(git status --porcelain)" ] && bail "Error: Your git repository clone is not clean"

previous_tag=$(git describe --abbrev=0)
[ -z "$previous_tag" ] && bail "Error: Could not detect the previous release tag"

new_tag="v$version"
[ -z "$(git tag -l $new_tag)" ] || bail "Error: Release tag $new_tag already exists."

name="$basename-$version"

#####################################################
##### Build everything in a temporary directory #####
#####################################################

# Create a temporary directory and clone the current git clone into the
# temporary directory.
tmp_dir="$(mktemp -d)"
trap "rm -rf '$tmp_dir'" EXIT

firmware_dir="$tmp_dir/firmware"
python_dir="$tmp_dir/python"
clone_dir="$tmp_dir/clone"
mkdir -p "$firmware_dir" "$python_dir" "$clone_dir"

git clone . "$clone_dir"

# Switch to the clone in temporary directory and update its submodules.
cd "$clone_dir"
echo "Updating git submodules ..."
git submodule update --init

# Create a signed and annotated tag in the local git repository clone. Fail if
# the tag already exists.
echo -n "Creating git tag $new_tag ... "
git tag -s -a "$new_tag" -m "Version $version"
echo "done."

# Generate the files VERSION and LIB_VERSION so that they can be included in the
# source tarball (which does not contain git version information).
make VERSION LIB_VERSION

# Create a source code tarball for the release that can be built without git.
echo -n "Creating source tarball $name.tar.gz ... "
command -v gtar &>/dev/null && tar=gtar || tar=tar
$tar --exclude .editorconfig   \
     --exclude .git            \
     --exclude .gitignore      \
     --exclude .gitmodules     \
     --exclude .vscode         \
     --exclude build           \
     --transform "s,^,$name/," \
    -zcf $firmware_dir/$name.tar.gz .
echo "done."

# Copy the resulting binary files into the firmware release directory.
install_firmware()
{
    cp -f build/release/firmware.bin "$firmware_dir/$name.$1.bin"
    cp -f build/release/firmware.hex "$firmware_dir/$name.$1.hex"
    cp -f build/debug/firmware.bin   "$firmware_dir/$name.$1.debug.bin"
    cp -f build/debug/firmware.hex   "$firmware_dir/$name.$1.debug.hex"
    cp -f build/debug/firmware.map   "$firmware_dir/$name.$1.debug.map"
}

# The build variant for the Hardwario Tower LoRa Modem. This variant uses PA12
# to control TCXO_VDD, has the factory reset pin disabled, and does not support
# LPUART1 detaching. The debug build type has the debugging logger enabled on
# USART1.
make -j4 FACTORY_RESET_PIN=0 TCXO_PIN=1 DETACHABLE_LPUART=0 release
make -j4 FACTORY_RESET_PIN=0 TCXO_PIN=1 DETACHABLE_LPUART=0 DEBUG_LOG=1 debug
install_firmware tower

# The chester variant is the same as the tower variant. No need to rebuild.
install_firmware chester

# This build variant is also the same as the tower build variant.
install_firmware bl072zlrwan1

# The build variant for the (older) Arduino MKR WAN 1300 board. MKR WAN 1300
# does not control TCXO (it is always enabled). The factory reset pin is
# disabled. LPUART1 detaching is not needed because this board does not have
# anything else connected on the SPI bus. The debug build type has the debugging
# logger configured to output to Segger RTT.
make -j4 FACTORY_RESET_PIN=0 TCXO_PIN=0 DETACHABLE_LPUART=0 release
make -j4 FACTORY_RESET_PIN=0 TCXO_PIN=0 DETACHABLE_LPUART=0 DEBUG_LOG=3 debug
install_firmware mkrwan1300

# The build the variant for the Arduino MKR WAN 1310 board. This variant uses
# PB6 to control TCXO power. We also enable support for detaching the ATCI UART
# port so that the host MCU can access the on-board SPI flash. The debug build
# configures the debugging logger to output to Segger RTT.
make -j4 FACTORY_RESET_PIN=0 TCXO_PIN=2 DETACHABLE_LPUART=1 release
make -j4 FACTORY_RESET_PIN=0 TCXO_PIN=2 DETACHABLE_LPUART=1 DEBUG_LOG=3 DEBUG_MCU=0 debug
install_firmware mkrwan1310

# Build the Python library.
PYTHON="$PYTHON" make python
cp -a python/dist/* "$python_dir"

# Compute SHA-256 checksums of all firmware release files.
cd "$firmware_dir"
firmware_files=(*)
checksums=$(sha256sum -b ${firmware_files[*]})

# Generate a signed version of the checksums.
echo -n "Signing the release manifest ... "
signed_checksums=$(echo "$checksums" | gpg --clear-sign)
echo "done."

# Generate a manifest file with SHA-256 checksums of all release files.
cat > $firmware_dir/manifest.md << EOF
Release $version

**SHA256 checksums**:
\`\`\`txt
$signed_checksums
\`\`\`

**Full changelog**: https://github.com/hardwario/$basename/compare/$previous_tag...$new_tag
EOF

############################################################
##### Copy tag and binary files back go original clone #####
############################################################

# Push the newly created signed release tag back to the local git clone from
# which we created the clone in the temporary directory.
cd "$clone_dir"
git push origin "$new_tag"

# Copy all files generated in this release back to the original clone.
rm -rf "$orig_dir/release/$version"
mkdir -p "$orig_dir/release/$version"
cp -a "$firmware_dir"/* "$orig_dir/release/$version"
cp -a "$python_dir" "$orig_dir/release/$version"

###################################
##### Push to GitHub and PyPI #####
###################################

# Push the signed tag that represents the new release to GitHub.
echo -n "Pushing tag $new_tag to GitHub ... "
cd "$orig_dir"
git push origin "$new_tag"
echo "done."

cd "release/$version"

# Create a new GitHub draft release for the new signed release tag with all the
# generated files attached.
echo -n "Creating a new GitHub draft release ... "
attachments=""
for f in ${firmware_files[@]}; do
    attachments="-a $f $attachments"
done
$HUB release create -d $attachments -F manifest.md "$new_tag"
echo "done."

# Upload a new version of the Python library to PyPI.
echo -n "Uploading new package version to PyPI ... "
"$PYTHON" -m twine upload -u __token__ -p "$PYPI_TOKEN" python/*
echo "done."
