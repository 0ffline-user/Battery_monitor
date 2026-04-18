# A simple and lightweight battery notification daemon.
This program uses netlink to listen for specific uevents that are generated either when the battery reaches a specific predefined level or when the AC adapter comes online or goes offline and then sends the notification via dbus (libsystemd implementation) to the notification daemon already running on the system.

## Requirements
* Linux kernel version >= 3.7.
* A systemd managed user session with the dbus session bus address path set always to: `/run/user/<uid>/bus` (Already standard on almost every modern linux distribution).
* A notification daemon like mako,dunst,... to actually show the notification on the screen.
* Must be run as a normal user (non-root).

The program relies exclusively on the uevents being broadcasted with the property: `POWER_SUPPLY_CAPACITY_LEVEL` set 
to `Low` or `Critical`. Unfortunately that happens on different levels sometimes and the only way I am aware of checking if they actually occur is to run the 
command: `udevadm monitor -p -s power_supply` and wait for the battery to drain until you see an event with `DEVPATH` ending in `/power_supply/<battery>` 
(`<battery>` is to be replaced with the appropriate device in the `/sys/class/power_supply/` directory) and the property mentioned before set to `Low` or `Critical` (should happen somewhere in 4%-16%).

## Dependencies
* [libsystemd](https://github.com/systemd/systemd) (This project links dynamically against libsystemd which is licensed under LGPL-2.1-or-later.)

## Configuration
The only configuration needed is to modify the macros in `src/config.h` to the following:
* `AC`: set to the AC adapter you want listed in the `/sys/class/power_supply/` directory prefixed by `/power_supply/`.
* `BAT`: set to the battery you want listed in the `/sys/class/power_supply/` directory prefixed by `/power_supply/`.
* `NTF_MS`: the duration in milliseconds that the notification will stay in the screen.

Example:
```sh
$ ls /sys/class/power_supply
AC0  BAT0  BAT1
```

```c
#define AC "/power_supply/AC0"
#define BAT "/power_supply/BAT0"
#define NTF_MS 5000
```

## Installation
To install run the following:
```sh
git clone https://github.com/0ffline-user/Battery_monitor.git
cd Battery_monitor
make install
```
Which will clone the repo, enter the created directory, build the project and then copy the binary to `/usr/local/bin/` with the name `battery_monitor`.

Lastly to make it run at startup you can either launch it directly from the window manager or with the systemd service file: `battery_monitor.service`.

If you choose to use the systemd service file then you must copy it to the directory: `~/.config/systemd/user/` (create it if it doesn't exist) and then run:
```sh
systemctl --user daemon-reload
systemctl --user enable --now battery_monitor
```
