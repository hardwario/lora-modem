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
     --exclude obj             \
     --transform "s,^,$name/," \
    -zcf $firmware_dir/$name.tar.gz .
echo "done."

# Build both release and debug versions of the firmware binary. This is the
# default build variant that uses PA12 (as recommended in the datasheet) to
# control TCXO_VDD. Debug builds have a debugging logger on USART1.
make TCXO_PIN=1 release
make TCXO_PIN=1 DEBUG_PORT=1 debug

# And copy the resulting binary files into the firmware release directory.
cp -f out/release/firmware.bin "$firmware_dir/$name.bin"
cp -f out/release/firmware.hex "$firmware_dir/$name.hex"
cp -f out/debug/firmware.bin   "$firmware_dir/$name.debug.bin"
cp -f out/debug/firmware.hex   "$firmware_dir/$name.debug.hex"
cp -f out/debug/firmware.map   "$firmware_dir/$name.debug.map"

# Now build the variants for Arduino MKRWAN boards. These build variants use PB6
# to control TCXO power. Debug builds start the debugging logger on USART2.
make clean
make TCXO_PIN=2 release
make TCXO_PIN=2 DEBUG_PORT=2 debug

# And copy the resulting binary files to the firmware release directory.
cp -f out/release/firmware.bin "$firmware_dir/$name.mkrwan.bin"
cp -f out/release/firmware.hex "$firmware_dir/$name.mkrwan.hex"
cp -f out/debug/firmware.bin   "$firmware_dir/$name.debug.mkrwan.bin"
cp -f out/debug/firmware.hex   "$firmware_dir/$name.debug.mkrwan.hex"
cp -f out/debug/firmware.map   "$firmware_dir/$name.debug.mkrwan.map"

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