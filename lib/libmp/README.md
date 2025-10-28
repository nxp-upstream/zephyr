# MediaPipe Library # {#mainpage}

<h1 align="center"> MediaPipe Library </h1>

---

#### Table of content
- [Overview](#overview)
- [Get started](#get-started)
- [Examples](#examples)
- [Services](#services)
- [Contributing](#contributing)

## Overview
### Features
### Requirements
### Supported platforms
## Get started
### On Zephyr platform

1. User needs to check if the dependencies for zephyr are installed on your developpement environment if not please follow [Zephyr-Getting Started-Install dependencies](https://docs.zephyrproject.org/latest/develop/getting_started/index.html#install-dependencies) to install them.

2. The libmp can be setup as a standalone project or as a Zephyr module depend on the user needs:
- Standalone libmp - Pulls only the MPP's required dependencies:
    1. Create the virtual environnement if this is not done yet ``python3 -m venv ~/<folder name>/libmp_virtual_env``
    2. Install west: ``pip3 install west``
    3. Initialize the repository libmp: ``west init -m <libmp git repository> --mr <revison or branch> --mf west-standalone.yml <foldername>``
    4. cd ``<folder name>``
    5. ``west update``
    6. cd ``libmp``
    7. Intall west extension command using ``west packages pip --install``

- libmp as a Zephyr module:
    1. Update the Zephyr's west.yml file:
    ```yml
        projects:
        name: libmp
        url: <libmp repository url>
        revison: <branch name or revision of libmp>
        path: modules/multimedia/libmp
        import: west.yml
    ```
    2. Run west update libmp command.

## Eamples
To build the basic example for imxrt1170 evkb, use the command:

``west build -p -b mimxrt1170_evk@B/mimxrt1176/cm7 examples/basic_example``

### C

## Contributing
