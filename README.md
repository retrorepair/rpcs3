RPCS3 w/ Groovy MiSTer NLC support
=====
Fork Notes


<img width="1635" height="862" alt="rpcs3_1" src="https://github.com/user-attachments/assets/0deb22be-6178-4c9a-b80f-33592457535f" />

## Graphics

It is recommended to render directly to 480p and 4:3 aspect ratio.
<img width="860" height="728" alt="rpcs3_2" src="https://github.com/user-attachments/assets/b5f98af0-1610-4eef-8085-85e50fbb57a2" />

However, some PS3 titles do not support 480p directly and will add large black borders. For these titles it is recommended to render to 720p, and downres via the MiSTer settings page "Output resolution".

<img width="947" height="950" alt="rpcs3_3" src="https://github.com/user-attachments/assets/6ee7ba75-2eae-4a99-baec-4e44603ae634" />


## Input

Use inputs directly from your MiSTer, or locally. Full dualshock support (analog/rumble)
<img width="810" height="240" alt="rpcs3_4" src="https://github.com/user-attachments/assets/573f873e-3c27-4cff-983a-cd8f59532f50" />


=====
[![GitHub Actions](https://img.shields.io/github/actions/workflow/status/RPCS3/rpcs3/rpcs3.yml?branch=master&logo=github&label=Actions)](https://github.com/RPCS3/rpcs3/actions/workflows/rpcs3.yml)
[![RPCS3 Discord Server](https://img.shields.io/discord/272035812277878785?color=5865F2&label=RPCS3%20Discord&logo=discord&logoColor=white)](https://discord.gg/rpcs3)

The world's first free and open-source PlayStation 3 emulator/debugger, written in C++ for Windows, Linux, macOS and FreeBSD.

You can find some basic information on our [**website**](https://rpcs3.net/). Game info is being populated on the [**Wiki**](https://wiki.rpcs3.net/).
For discussion about this emulator, PS3 emulation, and game compatibility reports, please visit our [**forums**](https://forums.rpcs3.net) and our [**Discord server**](https://discord.gg/RPCS3).

[**Support Lead Developers Nekotekina and kd-11 on Patreon**](https://www.patreon.com/Nekotekina)

## Contributing

If you want to help the project but do not code, the best way to help out is to test games and make bug reports. See:
* [Quickstart](https://rpcs3.net/quickstart)

If you want to contribute as a developer, please take a look at the following pages:

* [Coding Style](https://github.com/RPCS3/rpcs3/wiki/Coding-Style)
* [Developer Information](https://github.com/RPCS3/rpcs3/wiki/Developer-Information)

You should also contact any of the developers in the forums or in the Discord server to learn more about the current state of the emulator.

### AI Use

Use of AI tools for research and reverse engineering purposes is permitted. However, contributors are expected to fully own and understand all code they submit. Any communication with the team — including code, code comments, and GitHub comments — must come from the human contributor, not an AI agent acting autonomously.

We have unfortunately seen a rise in untested and unverified AI-generated slop being submitted to this project. This wastes maintainer time and, in worse cases, such changes get merged and break functionality for all users. Repeated violations will result in a ban from the repository. Please be respectful of everyone's time.

**Pull requests opened by AI agents or automated tools must include a disclosure in the PR description** stating the scope of AI involvement — which parts were AI-generated and what human testing or review was performed prior to submission. PRs that omit this disclosure may be closed without review.

If you are unsure about your work, open a discussion issue to talk it through with the team, or reach out to a maintainer on [Discord](https://discord.gg/RPCS3).

## Building

See the [building guide](https://wiki.rpcs3.net/index.php?title=Building) for more information about how to setup an environment to build RPCS3.

## Running

Check our friendly [quickstart](https://rpcs3.net/quickstart) guide to make sure your computer meets the minimum system requirements to run RPCS3.

Don't forget to have your graphics driver up to date and to install the [Visual C++ Redistributable Packages for Visual Studio 2022](https://aka.ms/vs/17/release/VC_redist.x64.exe) if you are a Windows user.

## License

Most files are licensed under the terms of GNU GPL-2.0-only License; see LICENSE file for details. Some files may be licensed differently; check appropriate file headers for details.
