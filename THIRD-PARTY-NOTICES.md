# Third-party notices

KisakCOD VR is derived from
[KisakCOD](https://github.com/SwagSoftware/KisakCOD), which is licensed under
the GNU General Public License version 3. The repository's `LICENSE` file and
the binary package's `LICENSE-GPLv3.txt` contain the applicable license text.

The current source tree records these OpenXR submodule revisions:

- KhronosGroup/OpenXR-SDK `release-1.1.60`
- KhronosGroup/OpenXR-SDK-Source `release-1.1.61`

Those projects are distributed under the Apache License 2.0. Their complete
license texts are copied from the checked-out submodules into each binary
package.

Tracy Profiler is distributed under the 3-clause BSD license. Its license text
is included in each binary package.

The retained audio-diagnostic path uses `dr_mp3` 0.7.4 from David Reid's
`dr_libs` project. The source header includes its public-domain dedication and
alternative MIT No Attribution license statement.

The binary package includes the 32-bit Steamworks, Bink, and Miles runtime
files retained in the upstream KisakCOD dependency tree and linked by the
rebuilt executable. Those components remain the property of their respective
rightsholders. The package intentionally does not include `iw3sp.exe`, DirectX
runtime files, maps, fastfiles, saves, or other Call of Duty game data. Users
provide the game data through their own legitimate COD4 installation.

Call of Duty, Call of Duty 4, Infinity Ward, Activision, Steam, OpenXR, Meta,
Virtual Desktop, and other marks belong to their respective owners. Listing a
product or project here does not imply endorsement.

Review the dependency tree before every release. If a later source revision
adds or changes a linked dependency, update this notice and include that
dependency's required license text in the binary package.
